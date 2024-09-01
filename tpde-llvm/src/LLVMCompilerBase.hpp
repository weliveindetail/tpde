// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/CompilerBase.hpp"

#include "LLVMAdaptor.hpp"

namespace tpde_llvm {

template <typename Adaptor, typename Derived, typename Config>
struct LLVMCompilerBase : tpde::CompilerBase<LLVMAdaptor, Derived, Config> {
    // TODO
    using Base = tpde::CompilerBase<LLVMAdaptor, Derived, Config>;

    using IRValueRef   = typename Base::IRValueRef;
    using ScratchReg   = typename Base::ScratchReg;
    using ValuePartRef = typename Base::ValuePartRef;
    using ValLocalIdx  = typename Base::ValLocalIdx;

    enum class IntBinaryOp {
        add,
        sub,
        mul,
        udiv,
        sdiv,
        urem,
        srem,
        land,
        lor,
        lxor,
        shl,
        shr,
        ashr,
    };

    // using AsmOperand = typename Derived::AsmOperand;

    LLVMCompilerBase(LLVMAdaptor *adaptor, const bool generate_obj)
        : Base{adaptor, generate_obj} {
        static_assert(tpde::Compiler<Derived, Config>);
        static_assert(std::is_same_v<Adaptor, LLVMAdaptor>);
    }

    Derived *derived() noexcept { return static_cast<Derived *>(this); }

    const Derived *derived() const noexcept {
        return static_cast<Derived *>(this);
    }

    // TODO(ts): check if it helps to check this
    static bool cur_func_may_emit_calls() noexcept { return true; }

    static bool try_force_fixed_assignment(IRValueRef) noexcept {
        return false;
    }

    std::optional<ValuePartRef> val_ref_special(ValLocalIdx local_idx,
                                                u32         part) noexcept;

    IRValueRef llvm_val_idx(const llvm::Value *) const noexcept;
    IRValueRef llvm_val_idx(const llvm::Instruction *) const noexcept;

    bool compile_inst(IRValueRef) noexcept;

    bool compile_ret(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_load(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_store(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_int_binary_op(IRValueRef,
                               llvm::Instruction *,
                               IntBinaryOp op) noexcept;
};

template <typename Adaptor, typename Derived, typename Config>
std::optional<typename LLVMCompilerBase<Adaptor, Derived, Config>::ValuePartRef>
    LLVMCompilerBase<Adaptor, Derived, Config>::val_ref_special(
        ValLocalIdx local_idx, u32 part) noexcept {
    const auto *val = this->adaptor->values[static_cast<u32>(local_idx)].val;
    const auto *const_val = llvm::dyn_cast<llvm::Constant>(val);

    if (const_val == nullptr || llvm::isa<llvm::GlobalValue>(val)) {
        return {};
    }

    const auto val_ref_simple =
        [](const llvm::Constant  *const_val,
           const LLVMBasicValType ty,
           const u32              part) -> std::optional<ValuePartRef> {
        if (const auto *const_int =
                llvm::dyn_cast<llvm::ConstantInt>(const_val);
            const_int != nullptr) {
            switch (ty) {
                using enum LLVMBasicValType;
            case i1:
            case i8:
                return ValuePartRef(static_cast<u8>(const_int->getZExtValue()),
                                    Config::GP_BANK,
                                    1);
            case i16:
                return ValuePartRef(static_cast<u16>(const_int->getZExtValue()),
                                    Config::GP_BANK,
                                    2);
            case i32:
                return ValuePartRef(static_cast<u32>(const_int->getZExtValue()),
                                    Config::GP_BANK,
                                    4);
            case i64:
            case ptr:
                return ValuePartRef(
                    const_int->getZExtValue(), Config::GP_BANK, 8);
            case i128:
                return ValuePartRef(
                    const_int->getValue().extractBitsAsZExtValue(64, 64 * part),
                    Config::GP_BANK,
                    8);
            default: assert(0); exit(1);
            }
        }

        if (const auto *const_ptr =
                llvm::dyn_cast<llvm::ConstantPointerNull>(const_val);
            const_ptr != nullptr) {
            return ValuePartRef(0, Config::GP_BANK, 8);
        }

        if (const auto *const_fp = llvm::dyn_cast<llvm::ConstantFP>(const_val);
            const_fp != nullptr) {
            switch (ty) {
                using enum LLVMBasicValType;
            case f32:
            case v32:
                return ValuePartRef(
                    static_cast<u32>(
                        const_fp->getValue().bitcastToAPInt().getZExtValue()),
                    Config::FP_BANK,
                    4);
            case f64:
            case v64:
                return ValuePartRef(
                    const_fp->getValue().bitcastToAPInt().getZExtValue(),
                    Config::FP_BANK,
                    8);
                // TODO(ts): support the rest
            default: assert(0); exit(1);
            }
        }

        if (llvm::isa<llvm::PoisonValue>(const_val)
            || llvm::isa<llvm::UndefValue>(const_val)
            || llvm::isa<llvm::ConstantAggregateZero>(const_val)) {
            switch (ty) {
                using enum LLVMBasicValType;
            case i1:
            case i8: return ValuePartRef(0, Config::GP_BANK, 1);
            case i16: return ValuePartRef(0, Config::GP_BANK, 2);
            case i32: return ValuePartRef(0, Config::GP_BANK, 4);
            case ptr:
            case i128:
            case i64: return ValuePartRef(0, Config::GP_BANK, 8);
            case f32:
            case v32: return ValuePartRef(0, Config::FP_BANK, 4);
            case f64:
            case v64:
                return ValuePartRef(0, Config::FP_BANK, 8);
                // TODO(ts): support larger constants
            default: assert(0); exit(1);
            }
        }

        return {};
    };

    const auto ty = this->adaptor->values[static_cast<u32>(local_idx)].type;
    if (auto opt = val_ref_simple(const_val, ty, part); opt) {
        return opt;
    }

    if (ty == LLVMBasicValType::complex
        && (llvm::isa<llvm::PoisonValue>(const_val)
            || llvm::isa<llvm::UndefValue>(const_val)
            || llvm::isa<llvm::ConstantAggregateZero>(const_val))) {
        u32        size = 0, bank = 0;
        const auto part_ty =
            this->adaptor
                ->complex_part_types[this->adaptor
                                         ->values[static_cast<u32>(local_idx)]
                                         .complex_part_tys_idx
                                     + part];
        switch (part_ty) {
            using enum LLVMBasicValType;
        case i1:
        case i8: size = 1; break;
        case i16: size = 2; break;
        case i32: size = 4; break;
        case i64:
        case ptr:
        case i128: size = 8; break;
        case f32:
        case v32:
            size = 4;
            bank = Config::FP_BANK;
            break;
        case f64:
        case v64:
            size = 8;
            bank = Config::FP_BANK;
            break;
        // TODO(ts): support larger constants
        default: assert(0); exit(1);
        }

        return ValuePartRef(0, bank, size);
    }


    if (llvm::isa<llvm::ConstantVector>(const_val)) {
        // TODO(ts): check how to handle this
        assert(0);
        exit(1);
    }

    if (const auto *const_agg =
            llvm::dyn_cast<llvm::ConstantAggregate>(const_val);
        const_agg != nullptr) {
        assert(this->adaptor->values[static_cast<u32>(local_idx)].type
               == LLVMBasicValType::complex);
        auto complex_part_idx =
            this->adaptor->values[static_cast<u32>(local_idx)]
                .complex_part_tys_idx;
        u32       cur_val_part = 0;
        const u32 num_ops      = const_agg->getNumOperands();
        for (; complex_part_idx < num_ops; ++complex_part_idx) {
            if (cur_val_part == part) {
                const auto part_ty =
                    this->adaptor->complex_part_types[complex_part_idx];
                auto opt = val_ref_simple(
                    const_agg->getOperand(complex_part_idx), part_ty, 0);
                assert(opt);
                return opt;
            }

            if (this->adaptor->complex_part_types[complex_part_idx]
                == LLVMBasicValType::i128) {
                ++cur_val_part;

                if (cur_val_part == part) {
                    auto opt =
                        val_ref_simple(const_agg->getOperand(complex_part_idx),
                                       LLVMBasicValType::i128,
                                       1);
                    assert(opt);
                    return opt;
                }
            }
            ++cur_val_part;
        }

        assert(0);
        exit(1);
    }

    TPDE_LOG_ERR("Encountered unknown constant type");
    assert(0);
    exit(1);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::IRValueRef
    LLVMCompilerBase<Adaptor, Derived, Config>::llvm_val_idx(
        const llvm::Value *val) const noexcept {
    return this->adaptor->val_lookup_idx(val);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::IRValueRef
    LLVMCompilerBase<Adaptor, Derived, Config>::llvm_val_idx(
        const llvm::Instruction *inst) const noexcept {
    return this->adaptor->inst_lookup_idx(inst);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_inst(
    IRValueRef val_idx) noexcept {
    auto *i =
        llvm::dyn_cast<llvm::Instruction>(this->adaptor->values[val_idx].val);
    const auto opcode = i->getOpcode();
    // TODO(ts): loads are next
    switch (opcode) {
    case llvm::Instruction::Ret: return compile_ret(val_idx, i);
    case llvm::Instruction::Load: return compile_load(val_idx, i);
    case llvm::Instruction::Store: return compile_store(val_idx, i);
    case llvm::Instruction::Add:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::add);
    case llvm::Instruction::Sub:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::sub);
    case llvm::Instruction::Mul:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::mul);
    case llvm::Instruction::UDiv:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::udiv);
    case llvm::Instruction::SDiv:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::sdiv);
    case llvm::Instruction::URem:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::urem);
    case llvm::Instruction::SRem:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::srem);
    case llvm::Instruction::And:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::land);
    case llvm::Instruction::Or:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::lor);
    case llvm::Instruction::Xor:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::lxor);
    case llvm::Instruction::Shl:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::shl);
    case llvm::Instruction::LShr:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::shr);
    case llvm::Instruction::AShr:
        return compile_int_binary_op(val_idx, i, IntBinaryOp::ashr);

    default: {
        TPDE_LOG_ERR("Encountered unknown instruction opcode {}: {}",
                     opcode,
                     i->getOpcodeName());
        assert(0);
        exit(1);
    }
    }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ret(
    IRValueRef, llvm::Instruction *ret) noexcept {
    assert(llvm::isa<llvm::ReturnInst>(ret));

    if (ret->getNumOperands() != 0) {
        assert(ret->getNumOperands() == 1);
        derived()->move_val_to_ret_regs(ret->getOperand(0));
    }

    derived()->gen_func_epilog();
    this->release_regs_after_return();
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_load(
    const IRValueRef load_idx, llvm::Instruction *inst) noexcept {
    assert(llvm::isa<llvm::LoadInst>(inst));
    auto *load = llvm::cast<llvm::LoadInst>(inst);
    assert(!load->isAtomic());

    auto ptr_ref = this->val_ref(llvm_val_idx(load->getPointerOperand()), 0);

    auto res = this->result_ref_lazy(load_idx, 0);

    ScratchReg res_scratch{this};
    switch (this->adaptor->values[load_idx].type) {
        using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64: {
        assert(load->getType()->isIntegerTy());
        const auto bit_width = load->getType()->getIntegerBitWidth();
        switch (bit_width) {
        case 1:
        case 8: derived()->encode_load8(std::move(ptr_ref), res_scratch); break;
        case 16:
            derived()->encode_load16(std::move(ptr_ref), res_scratch);
            break;
        case 24:
            derived()->encode_load24(std::move(ptr_ref), res_scratch);
            break;
        case 32:
            derived()->encode_load32(std::move(ptr_ref), res_scratch);
            break;
        case 40:
            derived()->encode_load40(std::move(ptr_ref), res_scratch);
            break;
        case 48:
            derived()->encode_load48(std::move(ptr_ref), res_scratch);
            break;
        case 56:
            derived()->encode_load56(std::move(ptr_ref), res_scratch);
            break;
        case 64:
            derived()->encode_load64(std::move(ptr_ref), res_scratch);
            break;
        default: assert(0); return false;
        }
        break;
    }
    case ptr: derived()->encode_load64(std::move(ptr_ref), res_scratch); break;
    case i128: {
        ScratchReg res_scratch_high{derived()};
        res.inc_ref_count();
        auto res_high = this->result_ref_lazy(load_idx, 1);

        derived()->encode_load128(
            std::move(ptr_ref), res_scratch, res_scratch_high);
        this->set_value(res, res_scratch);
        this->set_value(res_high, res_scratch_high);
        return true;
    }
    case v32:
    case f32: derived()->encode_loadf32(std::move(ptr_ref), res_scratch); break;
    case v64:
    case f64: derived()->encode_loadf64(std::move(ptr_ref), res_scratch); break;
    case v128:
        derived()->encode_loadv128(std::move(ptr_ref), res_scratch);
        break;
    case v256:
        if (!derived()->encode_loadv256(std::move(ptr_ref), res_scratch)) {
            return false;
        }
        break;
    case v512:
        if (!derived()->encode_loadv512(std::move(ptr_ref), res_scratch)) {
            return false;
        }
        break;
    case complex: {
        res.reset_without_refcount();

        // TODO: suffering, postponed, think about hwo to represent
        // parts that need multiple parts in complex types
        auto *load_ty = load->getType();
        assert(load_ty->isAggregateType());
        const auto *ty_sl = this->adaptor->mod.getDataLayout().getStructLayout(
            llvm::cast<llvm::StructType>(load_ty));

        const auto part_count = load_ty->getNumContainedTypes();
        const auto ty_idx =
            this->adaptor->values[load_idx].complex_part_tys_idx;
        u32 res_part_idx = 0;

        ScratchReg ptr_scratch{derived()};
        auto       ptr_reg = this->val_as_reg(ptr_ref, ptr_scratch);
        for (u32 part_idx = 0; part_idx < part_count;
             ++part_idx, ++res_part_idx) {
            const auto part_ty =
                this->adaptor->complex_part_types[ty_idx + part_idx];

            auto part_addr = typename Derived::AsmOperand::Address{
                ptr_reg,
                static_cast<tpde::i32>(
                    ty_sl->getElementOffset(part_idx).getFixedValue())};

            auto part_ref = this->result_ref_lazy(load_idx, res_part_idx);
            switch (part_ty) {
            case i1:
            case i8:
            case i16:
            case i32:
            case i64: {
                assert(load_ty->getContainedType(part_idx)->isIntegerTy());
                const auto bit_width =
                    load_ty->getContainedType(part_idx)->getIntegerBitWidth();
                switch (bit_width) {
                case 1:
                case 8: derived()->encode_load8(part_addr, res_scratch); break;
                case 16:
                    derived()->encode_load16(part_addr, res_scratch);
                    break;
                case 24:
                    derived()->encode_load24(part_addr, res_scratch);
                    break;
                case 32:
                    derived()->encode_load32(part_addr, res_scratch);
                    break;
                case 40:
                    derived()->encode_load40(part_addr, res_scratch);
                    break;
                case 48:
                    derived()->encode_load48(part_addr, res_scratch);
                    break;
                case 56:
                    derived()->encode_load56(part_addr, res_scratch);
                    break;
                case 64:
                    derived()->encode_load64(part_addr, res_scratch);
                    break;
                default: assert(0); return false;
                }
                break;
            }
            case ptr: derived()->encode_load64(part_addr, res_scratch); break;
            case i128: {
                ScratchReg res_scratch_high{derived()};
                derived()->encode_load128(
                    part_addr, res_scratch, res_scratch_high);

                auto part_ref_high =
                    this->result_ref_lazy(load_idx, ++res_part_idx);
                this->set_value(part_ref_high, res_scratch_high);
                part_ref_high.reset_without_refcount();
                break;
            }
            case v32:
            case f32: derived()->encode_loadf32(part_addr, res_scratch); break;
            case v64:
            case f64: derived()->encode_loadf64(part_addr, res_scratch); break;
            case v128:
                derived()->encode_loadv128(part_addr, res_scratch);
                break;
            case v256:
                if (!derived()->encode_loadv256(part_addr, res_scratch)) {
                    return false;
                }
                break;
            case v512:
                if (!derived()->encode_loadv512(part_addr, res_scratch)) {
                    return false;
                }
                break;
            default: assert(0); return false;
            }

            this->set_value(part_ref, res_scratch);

            if (part_idx != part_count - 1) {
                part_ref.reset_without_refcount();
            }
        }
        return true;
    }
    default: assert(0); return false;
    }

    this->set_value(res, res_scratch);

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_store(
    const IRValueRef, llvm::Instruction *inst) noexcept {
    assert(llvm::isa<llvm::StoreInst>(inst));
    auto *store = llvm::cast<llvm::StoreInst>(inst);
    assert(!store->isAtomic());

    auto ptr_ref = this->val_ref(llvm_val_idx(store->getPointerOperand()), 0);

    const auto *op_val = store->getValueOperand();
    const auto  op_idx = llvm_val_idx(op_val);
    auto        op_ref = this->val_ref(op_idx, 0);


    switch (this->adaptor->values[op_idx].type) {
        using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64: {
        assert(op_val->getType()->isIntegerTy());
        const auto bit_width = op_val->getType()->getIntegerBitWidth();
        switch (bit_width) {
        case 1:
        case 8:
            derived()->encode_store8(std::move(ptr_ref), std::move(op_ref));
            break;
        case 16:
            derived()->encode_store16(std::move(ptr_ref), std::move(op_ref));
            break;
        case 24:
            derived()->encode_store24(std::move(ptr_ref), std::move(op_ref));
            break;
        case 32:
            derived()->encode_store32(std::move(ptr_ref), std::move(op_ref));
            break;
        case 40:
            derived()->encode_store40(std::move(ptr_ref), std::move(op_ref));
            break;
        case 48:
            derived()->encode_store48(std::move(ptr_ref), std::move(op_ref));
            break;
        case 56:
            derived()->encode_store56(std::move(ptr_ref), std::move(op_ref));
            break;
        case 64:
            derived()->encode_store64(std::move(ptr_ref), std::move(op_ref));
            break;
        default: assert(0); return false;
        }
        break;
    }
    case ptr:
        derived()->encode_store64(std::move(ptr_ref), std::move(op_ref));
        break;
    case i128: {
        op_ref.inc_ref_count();
        auto op_ref_high = this->val_ref(op_idx, 1);

        derived()->encode_store128(
            std::move(ptr_ref), std::move(op_ref), std::move(op_ref_high));
        break;
    }
    case v32:
    case f32:
        derived()->encode_storef32(std::move(ptr_ref), std::move(op_ref));
        break;
    case v64:
    case f64:
        derived()->encode_storef64(std::move(ptr_ref), std::move(op_ref));
        break;
    case v128:
        derived()->encode_storev128(std::move(ptr_ref), std::move(op_ref));
        break;
    case v256:
        if (!derived()->encode_storev256(std::move(ptr_ref),
                                         std::move(op_ref))) {
            return false;
        }
        break;
    case v512:
        if (!derived()->encode_storev512(std::move(ptr_ref),
                                         std::move(op_ref))) {
            return false;
        }
        break;
    case complex: {
        op_ref.reset_without_refcount();

        // TODO: suffering, postponed, think about hwo to represent
        // parts that need multiple parts in complex types
        auto *store_ty = op_val->getType();
        assert(store_ty->isAggregateType());
        const auto *ty_sl = this->adaptor->mod.getDataLayout().getStructLayout(
            llvm::cast<llvm::StructType>(store_ty));

        const auto part_count = store_ty->getNumContainedTypes();
        const auto ty_idx = this->adaptor->values[op_idx].complex_part_tys_idx;
        u32        res_part_idx = 0;

        ScratchReg ptr_scratch{derived()};
        auto       ptr_reg = this->val_as_reg(ptr_ref, ptr_scratch);
        for (u32 part_idx = 0; part_idx < part_count;
             ++part_idx, ++res_part_idx) {
            const auto part_ty =
                this->adaptor->complex_part_types[ty_idx + part_idx];

            auto part_addr = typename Derived::AsmOperand::Address{
                ptr_reg,
                static_cast<tpde::i32>(
                    ty_sl->getElementOffset(part_idx).getFixedValue())};

            auto part_ref = this->val_ref(op_idx, res_part_idx);
            switch (part_ty) {
            case i1:
            case i8:
            case i16:
            case i32:
            case i64: {
                assert(store_ty->getContainedType(part_idx)->isIntegerTy());
                const auto bit_width =
                    store_ty->getContainedType(part_idx)->getIntegerBitWidth();
                switch (bit_width) {
                case 1:
                case 8: derived()->encode_store8(part_addr, part_ref); break;
                case 16: derived()->encode_store16(part_addr, part_ref); break;
                case 24: derived()->encode_store24(part_addr, part_ref); break;
                case 32: derived()->encode_store32(part_addr, part_ref); break;
                case 40: derived()->encode_store40(part_addr, part_ref); break;
                case 48: derived()->encode_store48(part_addr, part_ref); break;
                case 56: derived()->encode_store56(part_addr, part_ref); break;
                case 64: derived()->encode_store64(part_addr, part_ref); break;
                default: assert(0); return false;
                }
                break;
            }
            case ptr: derived()->encode_store64(part_addr, part_ref); break;
            case i128: {
                auto part_ref_high = this->val_ref(op_idx, ++res_part_idx);
                derived()->encode_store128(part_addr, part_ref, part_ref_high);

                part_ref_high.reset_without_refcount();
                break;
            }
            case v32:
            case f32: derived()->encode_storef32(part_addr, part_ref); break;
            case v64:
            case f64: derived()->encode_storef64(part_addr, part_ref); break;
            case v128: derived()->encode_storev128(part_addr, part_ref); break;
            case v256:
                if (!derived()->encode_storev256(part_addr, part_ref)) {
                    return false;
                }
                break;
            case v512:
                if (!derived()->encode_storev512(part_addr, part_ref)) {
                    return false;
                }
                break;
            default: assert(0); return false;
            }

            if (part_idx != part_count - 1) {
                part_ref.reset_without_refcount();
            }
        }
        return true;
    }
    default: assert(0); return false;
    }


    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_binary_op(
    IRValueRef         inst_idx,
    llvm::Instruction *inst,
    const IntBinaryOp  op) noexcept {
    using AsmOperand = typename Derived::AsmOperand;

    auto *inst_ty = inst->getType();

    if (inst_ty->isVectorTy()) {
        assert(0);
        exit(1);
    }

    assert(inst_ty->isIntegerTy());

    const auto int_width = inst_ty->getIntegerBitWidth();
    if (int_width == 128) {
        assert(0);
        exit(1);
    }

    // encode functions for 32/64 bit operations
    std::array<
        std::array<void (Derived::*)(AsmOperand, AsmOperand, ScratchReg &), 2>,
        13>
        encode_ptrs = {
            {
             {&Derived::encode_add32, &Derived::encode_add64},
             {&Derived::encode_sub32, &Derived::encode_sub64},
             {&Derived::encode_mul32, &Derived::encode_mul64},
             {&Derived::encode_udiv32, &Derived::encode_udiv64},
             {&Derived::encode_sdiv32, &Derived::encode_sdiv64},
             {&Derived::encode_urem32, &Derived::encode_urem64},
             {&Derived::encode_srem32, &Derived::encode_srem64},
             {&Derived::encode_land32, &Derived::encode_land64},
             {&Derived::encode_lor32, &Derived::encode_lor64},
             {&Derived::encode_lxor32, &Derived::encode_lxor64},
             {&Derived::encode_shl32, &Derived::encode_shl64},
             {&Derived::encode_shr32, &Derived::encode_shr64},
             {&Derived::encode_ashr32, &Derived::encode_ashr64},
             }
    };

    const auto is_64          = int_width > 32;
    const auto is_exact_width = int_width == 32 || int_width == 64;

    auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

    auto lhs_op = AsmOperand{std::move(lhs)};
    auto rhs_op = AsmOperand{std::move(rhs)};
    if (!is_exact_width
        && (op == IntBinaryOp::udiv || op == IntBinaryOp::sdiv
            || op == IntBinaryOp::urem || op == IntBinaryOp::srem
            || op == IntBinaryOp::shl || op == IntBinaryOp::shr
            || op == IntBinaryOp::ashr)) {
        if (op == IntBinaryOp::sdiv || op == IntBinaryOp::srem
            || op == IntBinaryOp::ashr) {
            // need to sign-extend lhs
            ScratchReg tmp{derived()};
            if (int_width == 8) {
                derived()->encode_sext_8_to_32(std::move(lhs_op), tmp);
            } else if (int_width == 16) {
                derived()->encode_sext_16_to_32(std::move(lhs_op), tmp);
            } else if (int_width < 32) {
                derived()->encode_sext_smaller32(std::move(lhs_op), tmp);
            } else {
                derived()->encode_sext_smaller64(std::move(lhs_op), tmp);
            }
            lhs_op = std::move(tmp);
        } else if (op == IntBinaryOp::udiv || op == IntBinaryOp::urem) {
            // need to zero-extend lhs (if it is not an immediate)
            if (!lhs.is_const) {
                ScratchReg tmp{derived()};
                u64        mask = (1ull << int_width) - 1;
                if (int_width <= 32) {
                    derived()->encode_land32(
                        std::move(lhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 4},
                        tmp);
                } else {
                    derived()->encode_land64(
                        std::move(lhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 8},
                        tmp);
                }
                lhs_op = std::move(tmp);
            }
        }

        if (op == IntBinaryOp::sdiv || op == IntBinaryOp::srem) {
            // need to sign-extend rhs
            ScratchReg tmp{derived()};
            if (int_width == 8) {
                derived()->encode_sext_8_to_32(std::move(rhs_op), tmp);
            } else if (int_width == 16) {
                derived()->encode_sext_16_to_32(std::move(rhs_op), tmp);
            } else if (int_width < 32) {
                derived()->encode_sext_smaller32(std::move(rhs_op), tmp);
            } else {
                derived()->encode_sext_smaller64(std::move(rhs_op), tmp);
            }
            rhs_op = std::move(tmp);
        } else {
            // need to zero-extend rhs (if it is not an immediate since then
            // this is guaranteed by LLVM)
            if (!rhs.is_const) {
                ScratchReg tmp{derived()};
                u64        mask = (1ull << int_width) - 1;
                if (int_width <= 32) {
                    derived()->encode_land32(
                        std::move(rhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 4},
                        tmp);
                } else {
                    derived()->encode_land64(
                        std::move(rhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 8},
                        tmp);
                }
                rhs_op = std::move(tmp);
            }
        }
    }

    auto res = this->result_ref_lazy(inst_idx, 0);

    auto res_scratch = ScratchReg{derived()};

    (derived()->*(encode_ptrs[static_cast<u32>(op)][is_64 ? 1 : 0]))(
        std::move(lhs_op), std::move(rhs_op), res_scratch);

    this->set_value(res, res_scratch);

    return true;
}
} // namespace tpde_llvm
