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

    using Assembler = typename Base::Assembler;
    using SymRef    = typename Assembler::SymRef;

    using AsmReg = typename Base::AsmReg;

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

    enum class FloatBinaryOp {
        add,
        sub,
        mul,
        div,
        rem
    };

    // using AsmOperand = typename Derived::AsmOperand;

    SymRef sym_fmod  = Assembler::INVALID_SYM_REF;
    SymRef sym_fmodf = Assembler::INVALID_SYM_REF;

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

    SymRef get_or_create_sym_ref(SymRef          &sym,
                                 std::string_view name,
                                 bool             local = false) noexcept;

    bool compile_inst(IRValueRef) noexcept;

    bool compile_ret(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_load(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_store(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_int_binary_op(IRValueRef,
                               llvm::Instruction *,
                               IntBinaryOp op) noexcept;
    bool compile_float_binary_op(IRValueRef,
                                 llvm::Instruction *,
                                 FloatBinaryOp op) noexcept;
    bool compile_fneg(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_float_ext_trunc(IRValueRef,
                                 llvm::Instruction *,
                                 bool trunc) noexcept;
    bool compile_float_to_int(IRValueRef,
                              llvm::Instruction *,
                              bool sign) noexcept;
    bool compile_int_to_float(IRValueRef,
                              llvm::Instruction *,
                              bool sign) noexcept;
    bool compile_int_trunc(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_int_ext(IRValueRef, llvm::Instruction *, bool sign) noexcept;
    bool compile_ptr_to_int(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_int_to_ptr(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_bitcast(IRValueRef, llvm::Instruction *) noexcept;
    bool compile_cmpxchg(IRValueRef, llvm::Instruction *) noexcept;
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
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::get_or_create_sym_ref(
        SymRef &sym, std::string_view name, const bool local) noexcept {
    if (sym != Assembler::INVALID_SYM_REF) {
        return sym;
    }

    sym = this->assembler.sym_add_undef(name, local);
    return sym;
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
    case llvm::Instruction::FAdd:
        return compile_float_binary_op(val_idx, i, FloatBinaryOp::add);
    case llvm::Instruction::FSub:
        return compile_float_binary_op(val_idx, i, FloatBinaryOp::sub);
    case llvm::Instruction::FMul:
        return compile_float_binary_op(val_idx, i, FloatBinaryOp::mul);
    case llvm::Instruction::FDiv:
        return compile_float_binary_op(val_idx, i, FloatBinaryOp::div);
    case llvm::Instruction::FRem:
        return compile_float_binary_op(val_idx, i, FloatBinaryOp::rem);
    case llvm::Instruction::FNeg: return compile_fneg(val_idx, i);
    case llvm::Instruction::FPExt:
        return compile_float_ext_trunc(val_idx, i, false);
    case llvm::Instruction::FPTrunc:
        return compile_float_ext_trunc(val_idx, i, true);
    case llvm::Instruction::FPToSI:
        return compile_float_to_int(val_idx, i, true);
    case llvm::Instruction::FPToUI:
        return compile_float_to_int(val_idx, i, false);
    case llvm::Instruction::SIToFP:
        return compile_int_to_float(val_idx, i, true);
    case llvm::Instruction::UIToFP:
        return compile_int_to_float(val_idx, i, false);
    case llvm::Instruction::Trunc: return compile_int_trunc(val_idx, i);
    case llvm::Instruction::SExt: return compile_int_ext(val_idx, i, true);
    case llvm::Instruction::ZExt: return compile_int_ext(val_idx, i, false);
    case llvm::Instruction::PtrToInt: return compile_ptr_to_int(val_idx, i);
    case llvm::Instruction::IntToPtr: return compile_int_to_ptr(val_idx, i);
    case llvm::Instruction::BitCast: return compile_bitcast(val_idx, i);
    case llvm::Instruction::AtomicCmpXchg: return compile_cmpxchg(val_idx, i);

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
        case 8:
            derived()->encode_loadi8(std::move(ptr_ref), res_scratch);
            break;
        case 16:
            derived()->encode_loadi16(std::move(ptr_ref), res_scratch);
            break;
        case 24:
            derived()->encode_loadi24(std::move(ptr_ref), res_scratch);
            break;
        case 32:
            derived()->encode_loadi32(std::move(ptr_ref), res_scratch);
            break;
        case 40:
            derived()->encode_loadi40(std::move(ptr_ref), res_scratch);
            break;
        case 48:
            derived()->encode_loadi48(std::move(ptr_ref), res_scratch);
            break;
        case 56:
            derived()->encode_loadi56(std::move(ptr_ref), res_scratch);
            break;
        case 64:
            derived()->encode_loadi64(std::move(ptr_ref), res_scratch);
            break;
        default: assert(0); return false;
        }
        break;
    }
    case ptr: derived()->encode_loadi64(std::move(ptr_ref), res_scratch); break;
    case i128: {
        ScratchReg res_scratch_high{derived()};
        res.inc_ref_count();
        auto res_high = this->result_ref_lazy(load_idx, 1);

        derived()->encode_loadi128(
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
                this->adaptor->complex_part_types[ty_idx + res_part_idx];

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
                case 8: derived()->encode_loadi8(part_addr, res_scratch); break;
                case 16:
                    derived()->encode_loadi16(part_addr, res_scratch);
                    break;
                case 24:
                    derived()->encode_loadi24(part_addr, res_scratch);
                    break;
                case 32:
                    derived()->encode_loadi32(part_addr, res_scratch);
                    break;
                case 40:
                    derived()->encode_loadi40(part_addr, res_scratch);
                    break;
                case 48:
                    derived()->encode_loadi48(part_addr, res_scratch);
                    break;
                case 56:
                    derived()->encode_loadi56(part_addr, res_scratch);
                    break;
                case 64:
                    derived()->encode_loadi64(part_addr, res_scratch);
                    break;
                default: assert(0); return false;
                }
                break;
            }
            case ptr: derived()->encode_loadi64(part_addr, res_scratch); break;
            case i128: {
                ScratchReg res_scratch_high{derived()};
                derived()->encode_loadi128(
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
            derived()->encode_storei8(std::move(ptr_ref), std::move(op_ref));
            break;
        case 16:
            derived()->encode_storei16(std::move(ptr_ref), std::move(op_ref));
            break;
        case 24:
            derived()->encode_storei24(std::move(ptr_ref), std::move(op_ref));
            break;
        case 32:
            derived()->encode_storei32(std::move(ptr_ref), std::move(op_ref));
            break;
        case 40:
            derived()->encode_storei40(std::move(ptr_ref), std::move(op_ref));
            break;
        case 48:
            derived()->encode_storei48(std::move(ptr_ref), std::move(op_ref));
            break;
        case 56:
            derived()->encode_storei56(std::move(ptr_ref), std::move(op_ref));
            break;
        case 64:
            derived()->encode_storei64(std::move(ptr_ref), std::move(op_ref));
            break;
        default: assert(0); return false;
        }
        break;
    }
    case ptr:
        derived()->encode_storei64(std::move(ptr_ref), std::move(op_ref));
        break;
    case i128: {
        op_ref.inc_ref_count();
        auto op_ref_high = this->val_ref(op_idx, 1);

        derived()->encode_storei128(
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
                this->adaptor->complex_part_types[ty_idx + res_part_idx];

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
                case 8: derived()->encode_storei8(part_addr, part_ref); break;
                case 16: derived()->encode_storei16(part_addr, part_ref); break;
                case 24: derived()->encode_storei24(part_addr, part_ref); break;
                case 32: derived()->encode_storei32(part_addr, part_ref); break;
                case 40: derived()->encode_storei40(part_addr, part_ref); break;
                case 48: derived()->encode_storei48(part_addr, part_ref); break;
                case 56: derived()->encode_storei56(part_addr, part_ref); break;
                case 64: derived()->encode_storei64(part_addr, part_ref); break;
                default: assert(0); return false;
                }
                break;
            }
            case ptr: derived()->encode_storei64(part_addr, part_ref); break;
            case i128: {
                auto part_ref_high = this->val_ref(op_idx, ++res_part_idx);
                derived()->encode_storei128(part_addr, part_ref, part_ref_high);

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
    using EncodeImm  = typename Derived::EncodeImm;

    auto *inst_ty = inst->getType();

    if (inst_ty->isVectorTy()) {
        assert(0);
        exit(1);
    }

    assert(inst_ty->isIntegerTy());

    const auto int_width = inst_ty->getIntegerBitWidth();
    if (int_width == 128) {
        auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
        auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

        // TODO(ts): better salvaging
        lhs.inc_ref_count();
        rhs.inc_ref_count();

        auto lhs_high = this->val_ref(llvm_val_idx(inst->getOperand(0)), 1);
        auto rhs_high = this->val_ref(llvm_val_idx(inst->getOperand(1)), 1);

        if ((op == IntBinaryOp::add || op == IntBinaryOp::mul
             || op == IntBinaryOp::land || op == IntBinaryOp::lor
             || op == IntBinaryOp::lxor)
            && lhs.is_const && lhs_high.is_const && !rhs.is_const
            && !rhs_high.is_const) {
            // TODO(ts): this is a hack since the encoder can currently not do
            // commutable operations so we reorder immediates manually here
            std::swap(lhs, rhs);
            std::swap(lhs_high, rhs_high);
        }

        auto res_low = this->result_ref_lazy(inst_idx, 0);
        res_low.inc_ref_count();
        auto res_high = this->result_ref_lazy(inst_idx, 1);

        ScratchReg scratch_low{derived()}, scratch_high{derived()};


        std::array<bool (Derived::*)(AsmOperand,
                                     AsmOperand,
                                     AsmOperand,
                                     AsmOperand,
                                     ScratchReg &,
                                     ScratchReg &),
                   10>
            encode_ptrs = {
                {
                 &Derived::encode_addi128,
                 &Derived::encode_subi128,
                 &Derived::encode_muli128,
#if 0
                 &Derived::encode_udivi128,
                 &Derived::encode_sdivi128,
                 &Derived::encode_uremi128,
                 &Derived::encode_sremi128,
#else
                 nullptr, nullptr,
                 nullptr, nullptr,
#endif
                 &Derived::encode_landi128,
                 &Derived::encode_lori128,
                 &Derived::encode_lxori128,
                 }
        };

        if (op == IntBinaryOp::udiv || op == IntBinaryOp::sdiv
            || op == IntBinaryOp::urem || op == IntBinaryOp::srem) {
            // TODO(ts): the autoencoder can currently not generate calls to
            // globals which LLVM generates for 128 bit division so we need to
            // fix that or do the calls ourselves (which is probably easier rn)
            llvm::errs() << "Division/Remainder for 128bit integers currently "
                            "unimplemented\n";
            return false;
        }

        switch (op) {
        case IntBinaryOp::shl:
        case IntBinaryOp::shr:
        case IntBinaryOp::ashr:
            rhs_high.reset();
            if (rhs.is_const) {
                auto shift_amount =
                    static_cast<u32>(rhs.state.c.const_u64 & 0b111'1111);
                if (shift_amount < 64) {
                    if (op == IntBinaryOp::shl) {
                        derived()->encode_shli128_lt64(
                            lhs, lhs_high, rhs, scratch_low, scratch_high);
                    } else if (op == IntBinaryOp::shr) {
                        derived()->encode_shri128_lt64(
                            lhs, lhs_high, rhs, scratch_low, scratch_high);
                    } else {
                        assert(op == IntBinaryOp::ashr);
                        derived()->encode_ashri128_lt64(
                            lhs, lhs_high, rhs, scratch_low, scratch_high);
                    }
                } else {
                    shift_amount -= 64;
                    if (op == IntBinaryOp::shl) {
                        derived()->encode_shli128_ge64(lhs,
                                                       EncodeImm{shift_amount},
                                                       scratch_low,
                                                       scratch_high);
                    } else if (op == IntBinaryOp::shr) {
                        derived()->encode_shri128_ge64(lhs_high,
                                                       EncodeImm{shift_amount},
                                                       scratch_low,
                                                       scratch_high);
                    } else {
                        assert(op == IntBinaryOp::ashr);
                        derived()->encode_ashri128_ge64(lhs_high,
                                                        EncodeImm{shift_amount},
                                                        scratch_low,
                                                        scratch_high);
                    }
                }
                break;
            }
            if (op == IntBinaryOp::shl) {
                derived()->encode_shli128(std::move(lhs),
                                          std::move(lhs_high),
                                          std::move(rhs),
                                          scratch_low,
                                          scratch_high);
            } else if (op == IntBinaryOp::shr) {
                derived()->encode_shri128(std::move(lhs),
                                          std::move(lhs_high),
                                          std::move(rhs),
                                          scratch_low,
                                          scratch_high);
            } else {
                assert(op == IntBinaryOp::ashr);
                derived()->encode_ashri128(std::move(lhs),
                                           std::move(lhs_high),
                                           std::move(rhs),
                                           scratch_low,
                                           scratch_high);
            }
            break;
        default:
            (derived()->*(encode_ptrs[static_cast<u32>(op)]))(
                std::move(lhs),
                std::move(lhs_high),
                std::move(rhs),
                std::move(rhs_high),
                scratch_low,
                scratch_high);
            break;
        }

        this->set_value(res_low, scratch_low);
        this->set_value(res_high, scratch_high);

        return true;
    }
    assert(int_width <= 64);

    // encode functions for 32/64 bit operations
    std::array<
        std::array<bool (Derived::*)(AsmOperand, AsmOperand, ScratchReg &), 2>,
        13>
        encode_ptrs = {
            {
             {&Derived::encode_addi32, &Derived::encode_addi64},
             {&Derived::encode_subi32, &Derived::encode_subi64},
             {&Derived::encode_muli32, &Derived::encode_muli64},
             {&Derived::encode_udivi32, &Derived::encode_udivi64},
             {&Derived::encode_sdivi32, &Derived::encode_sdivi64},
             {&Derived::encode_uremi32, &Derived::encode_uremi64},
             {&Derived::encode_sremi32, &Derived::encode_sremi64},
             {&Derived::encode_landi32, &Derived::encode_landi64},
             {&Derived::encode_lori32, &Derived::encode_lori64},
             {&Derived::encode_lxori32, &Derived::encode_lxori64},
             {&Derived::encode_shli32, &Derived::encode_shli64},
             {&Derived::encode_shri32, &Derived::encode_shri64},
             {&Derived::encode_ashri32, &Derived::encode_ashri64},
             }
    };

    const auto is_64          = int_width > 32;
    const auto is_exact_width = int_width == 32 || int_width == 64;

    auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

    if ((op == IntBinaryOp::add || op == IntBinaryOp::mul
         || op == IntBinaryOp::land || op == IntBinaryOp::lor
         || op == IntBinaryOp::lxor)
        && lhs.is_const && !rhs.is_const) {
        // TODO(ts): this is a hack since the encoder can currently not do
        // commutable operations so we reorder immediates manually here
        std::swap(lhs, rhs);
    }

    // TODO(ts): optimize div/rem by constant to a shift?
    const auto lhs_const = lhs.is_const;
    const auto rhs_const = rhs.is_const;
    auto       lhs_op    = AsmOperand{std::move(lhs)};
    auto       rhs_op    = AsmOperand{std::move(rhs)};
    if (!is_exact_width
        && (op == IntBinaryOp::udiv || op == IntBinaryOp::sdiv
            || op == IntBinaryOp::urem || op == IntBinaryOp::srem
            || op == IntBinaryOp::shl || op == IntBinaryOp::shr
            || op == IntBinaryOp::ashr)) {
        if (op == IntBinaryOp::sdiv || op == IntBinaryOp::srem
            || op == IntBinaryOp::ashr) {
            // need to sign-extend lhs
            // TODO(ts): if lhs is constant, sign-extend as constant
            ScratchReg tmp{derived()};
            if (int_width == 8) {
                derived()->encode_sext_8_to_32(std::move(lhs_op), tmp);
            } else if (int_width == 16) {
                derived()->encode_sext_16_to_32(std::move(lhs_op), tmp);
            } else if (int_width < 32) {
                derived()->encode_sext_arbitrary_to_32(
                    std::move(lhs_op), EncodeImm{32u - int_width}, tmp);
            } else {
                derived()->encode_sext_arbitrary_to_64(
                    std::move(lhs_op), EncodeImm{64u - int_width}, tmp);
            }
            lhs_op = std::move(tmp);
        } else if (op == IntBinaryOp::udiv || op == IntBinaryOp::urem
                   || op == IntBinaryOp::shr) {
            // need to zero-extend lhs (if it is not an immediate)
            if (!lhs_const) {
                ScratchReg tmp{derived()};
                u64        mask = (1ull << int_width) - 1;
                if (int_width == 8) {
                    derived()->encode_zext_8_to_32(std::move(lhs_op), tmp);
                } else if (int_width == 16) {
                    derived()->encode_zext_16_to_32(std::move(lhs_op), tmp);
                } else if (int_width <= 32) {
                    derived()->encode_landi32(
                        std::move(lhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 4},
                        tmp);
                } else {
                    derived()->encode_landi64(
                        std::move(lhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 8},
                        tmp);
                }
                lhs_op = std::move(tmp);
            }
        }

        if (op == IntBinaryOp::sdiv || op == IntBinaryOp::srem) {
            // need to sign-extend rhs
            // TODO(ts): if rhs is constant, sign-extend as constant
            ScratchReg tmp{derived()};
            if (int_width == 8) {
                derived()->encode_sext_8_to_32(std::move(rhs_op), tmp);
            } else if (int_width == 16) {
                derived()->encode_sext_16_to_32(std::move(rhs_op), tmp);
            } else if (int_width < 32) {
                derived()->encode_sext_arbitrary_to_32(
                    std::move(lhs_op), EncodeImm{32u - int_width}, tmp);
            } else {
                derived()->encode_sext_arbitrary_to_64(
                    std::move(lhs_op), EncodeImm{64u - int_width}, tmp);
            }
            rhs_op = std::move(tmp);
        } else {
            // need to zero-extend rhs (if it is not an immediate since then
            // this is guaranteed by LLVM)
            if (!rhs_const) {
                ScratchReg tmp{derived()};
                u64        mask = (1ull << int_width) - 1;
                if (int_width == 8) {
                    derived()->encode_zext_8_to_32(std::move(rhs_op), tmp);
                } else if (int_width == 16) {
                    derived()->encode_zext_16_to_32(std::move(rhs_op), tmp);
                } else if (int_width <= 32) {
                    derived()->encode_landi32(
                        std::move(rhs_op),
                        ValuePartRef{mask, Config::GP_BANK, 4},
                        tmp);
                } else {
                    derived()->encode_landi64(
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

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_binary_op(
    IRValueRef inst_idx, llvm::Instruction *inst, FloatBinaryOp op) noexcept {
    using AsmOperand = typename Derived::AsmOperand;

    auto *inst_ty = inst->getType();

    if (inst_ty->isVectorTy()) {
        assert(0);
        exit(1);
    }

    const bool is_double = inst_ty->isDoubleTy();
    assert(inst_ty->isFloatTy() || inst_ty->isDoubleTy());

    auto res = this->result_ref_lazy(inst_idx, 0);

    if (op == FloatBinaryOp::rem) {
        // TODO(ts): encodegen cannot encode calls atm
        derived()->create_frem_calls(llvm_val_idx(inst->getOperand(0)),
                                     llvm_val_idx(inst->getOperand(1)),
                                     std::move(res),
                                     is_double);
        return true;
    }

    std::array<
        std::array<bool (Derived::*)(AsmOperand, AsmOperand, ScratchReg &), 2>,
        4>
        encode_ptrs = {
            {
             {&Derived::encode_addf32, &Derived::encode_addf64},
             {&Derived::encode_subf32, &Derived::encode_subf64},
             {&Derived::encode_mulf32, &Derived::encode_mulf64},
             {&Derived::encode_divf32, &Derived::encode_divf64},
             }
    };

    auto       lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto       rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    ScratchReg res_scratch{derived()};

    if (!(derived()->*(encode_ptrs[static_cast<u32>(op)][is_double ? 1 : 0]))(
            std::move(lhs), std::move(rhs), res_scratch)) {
        return false;
    }

    this->set_value(res, res_scratch);

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_fneg(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    const auto is_double = inst->getType()->isDoubleTy();

    assert(inst->getType()->isDoubleTy() || inst->getType()->isFloatTy());

    auto src_ref     = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto res_ref     = this->result_ref_lazy(inst_idx, 0);
    auto res_scratch = ScratchReg{derived()};

    if (is_double) {
        derived()->encode_fnegf64(std::move(src_ref), res_scratch);
    } else {
        derived()->encode_fnegf32(std::move(src_ref), res_scratch);
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_ext_trunc(
    IRValueRef inst_idx, llvm::Instruction *inst, const bool trunc) noexcept {
    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();

    assert(src_ty->isFloatTy() || src_ty->isDoubleTy());

    const auto src_double = src_ty->isDoubleTy();
    const auto dst_double = inst->getType()->isDoubleTy();

    auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);

    auto       res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res_scratch{derived()};
    if (trunc) {
        assert(src_double && !dst_double);
        derived()->encode_f64tof32(std::move(src_ref), res_scratch);
    } else {
        assert(!src_double && dst_double);
        derived()->encode_f32tof64(std::move(src_ref), res_scratch);
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_to_int(
    IRValueRef inst_idx, llvm::Instruction *inst, const bool sign) noexcept {
    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();
    assert(src_ty->isFloatTy() || src_ty->isDoubleTy());

    auto *dst_ty = inst->getType();
    assert(dst_ty->isIntegerTy());

    const auto bit_width = dst_ty->getIntegerBitWidth();
    if (bit_width != 64 && bit_width != 32) {
        assert(0);
        return false;
    }

    const auto src_double = src_ty->isDoubleTy();

    auto src_ref     = this->val_ref(llvm_val_idx(src_val), 0);
    auto res_ref     = this->result_ref_lazy(inst_idx, 0);
    auto res_scratch = ScratchReg{derived()};
    if (sign) {
        if (src_double) {
            if (bit_width == 64) {
                derived()->encode_f64toi64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width == 32);
                derived()->encode_f64toi32(std::move(src_ref), res_scratch);
            }
        } else {
            if (bit_width == 64) {
                derived()->encode_f32toi64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width == 32);
                derived()->encode_f32toi32(std::move(src_ref), res_scratch);
            }
        }
    } else {
        if (src_double) {
            if (bit_width == 64) {
                derived()->encode_f64tou64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width == 32);
                derived()->encode_f64tou32(std::move(src_ref), res_scratch);
            }
        } else {
            if (bit_width == 64) {
                derived()->encode_f32tou64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width == 32);
                derived()->encode_f32tou32(std::move(src_ref), res_scratch);
            }
        }
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_to_float(
    IRValueRef inst_idx, llvm::Instruction *inst, const bool sign) noexcept {
    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();
    assert(src_ty->isIntegerTy());

    auto *dst_ty = inst->getType();
    assert(dst_ty->isFloatTy() || dst_ty->isDoubleTy());

    const auto bit_width = src_ty->getIntegerBitWidth();
    if (bit_width != 64 && bit_width != 32 && bit_width != 16
        && bit_width != 8) {
        assert(0);
        return false;
    }

    const auto dst_double = dst_ty->isDoubleTy();

    auto src_ref     = this->val_ref(llvm_val_idx(src_val), 0);
    auto res_ref     = this->result_ref_lazy(inst_idx, 0);
    auto res_scratch = ScratchReg{derived()};

    if (sign) {
        if (bit_width == 64) {
            if (dst_double) {
                derived()->encode_i64tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_i64tof32(std::move(src_ref), res_scratch);
            }
        } else if (bit_width == 32) {
            if (dst_double) {
                derived()->encode_i32tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_i32tof32(std::move(src_ref), res_scratch);
            }
        } else if (bit_width == 16) {
            if (dst_double) {
                derived()->encode_i16tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_i16tof32(std::move(src_ref), res_scratch);
            }
        } else {
            assert(bit_width == 8);
            if (dst_double) {
                derived()->encode_i8tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_i8tof32(std::move(src_ref), res_scratch);
            }
        }
    } else {
        if (bit_width == 64) {
            if (dst_double) {
                derived()->encode_u64tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_u64tof32(std::move(src_ref), res_scratch);
            }
        } else if (bit_width == 32) {
            if (dst_double) {
                derived()->encode_u32tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_u32tof32(std::move(src_ref), res_scratch);
            }
        } else if (bit_width == 16) {
            if (dst_double) {
                derived()->encode_u16tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_u16tof32(std::move(src_ref), res_scratch);
            }
        } else {
            assert(bit_width == 8);
            if (dst_double) {
                derived()->encode_u8tof64(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_u8tof32(std::move(src_ref), res_scratch);
            }
        }
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_trunc(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    // this is a no-op since every operation that depends on it will
    // zero/sign-extend the value anyways
    auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);

    AsmReg orig;
    auto   res_ref = this->result_ref_salvage_with_original(
        inst_idx, 0, std::move(src_ref), orig);
    if (orig != res_ref.cur_reg()) {
        derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_ext(
    IRValueRef inst_idx, llvm::Instruction *inst, bool sign) noexcept {
    using EncodeImm = typename Derived::EncodeImm;

    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();
    assert(src_ty->isIntegerTy());
    const auto src_width = src_ty->getIntegerBitWidth();

    auto *dst_ty = inst->getType();
    assert(dst_ty->isIntegerTy());
    const auto dst_width = dst_ty->getIntegerBitWidth();
    assert(dst_width >= src_width);

    auto       src_ref = this->val_ref(llvm_val_idx(src_val), 0);
    ScratchReg res_scratch{derived()};

    if (src_width == 8) {
        if (sign) {
            if (dst_width <= 32) {
                derived()->encode_sext_8_to_32(std::move(src_ref), res_scratch);
            } else {
                derived()->encode_sext_8_to_64(std::move(src_ref), res_scratch);
            }
        } else {
            // works because both on ARM and x64 zext to 32 zeroes the upper
            // bits
            derived()->encode_zext_8_to_32(std::move(src_ref), res_scratch);
        }
    } else if (src_width == 16) {
        if (sign) {
            if (dst_width <= 32) {
                derived()->encode_sext_16_to_32(std::move(src_ref),
                                                res_scratch);
            } else {
                derived()->encode_sext_16_to_64(std::move(src_ref),
                                                res_scratch);
            }
        } else {
            // works because both on ARM and x64 zext to 32 zeroes the upper
            // bits
            derived()->encode_zext_16_to_32(std::move(src_ref), res_scratch);
        }
    } else if (src_width != 32 && src_width != 64) {
        if (sign) {
            if (dst_width <= 32) {
                derived()->encode_sext_arbitrary_to_32(
                    std::move(src_ref),
                    EncodeImm{32u - src_width},
                    res_scratch);
            } else {
                derived()->encode_sext_arbitrary_to_64(
                    std::move(src_ref),
                    EncodeImm{64u - src_width},
                    res_scratch);
            }
        } else {
            u64 mask = (1ull << src_width) - 1;
            if (src_width <= 32) {
                // works because both on ARM and x64 zext to 32 zeroes the upper
                // bits
                derived()->encode_landi32(
                    std::move(src_ref), EncodeImm{mask}, res_scratch);
            } else {
                derived()->encode_landi64(
                    std::move(src_ref), EncodeImm{mask}, res_scratch);
            }
        }
    } else if (src_width == 32) {
        if (sign) {
            derived()->encode_sext_32_to_64(std::move(src_ref), res_scratch);
        } else {
            derived()->encode_zext_32_to_64(std::move(src_ref), res_scratch);
        }
    } else {
        assert(src_width == 64);
        if (src_ref.can_salvage()) {
            if (!src_ref.assignment().register_valid()) {
                src_ref.alloc_reg(true);
            }
            res_scratch.alloc_specific(src_ref.salvage());
        } else {
            ScratchReg tmp{derived()};
            auto       src = this->val_as_reg(src_ref, tmp);

            derived()->mov(res_scratch.alloc_gp(), src, 8);
        }
    }

    auto res_ref = this->result_ref_lazy(inst_idx, 0);

    if (dst_width == 128) {
        assert(src_width <= 64);
        res_ref.inc_ref_count();
        auto       res_ref_high = this->result_ref_lazy(inst_idx, 1);
        ScratchReg scratch_high{derived()};

        if (sign) {
            derived()->encode_fill_with_sign64(res_scratch.cur_reg,
                                               scratch_high);
        } else {
            std::array<u8, 64> data{};
            derived()->materialize_constant(
                data, Config::GP_BANK, 8, scratch_high.alloc_gp());
        }

        this->set_value(res_ref_high, scratch_high);
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ptr_to_int(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    // this is a no-op since every operation that depends on it will
    // zero/sign-extend the value anyways
    auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);

    AsmReg orig;
    auto   res_ref = this->result_ref_salvage_with_original(
        inst_idx, 0, std::move(src_ref), orig);
    if (orig != res_ref.cur_reg()) {
        derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_to_ptr(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    using EncodeImm = typename Derived::EncodeImm;

    // zero-extend the value
    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();
    assert(src_ty->isIntegerTy());
    const auto bit_width = src_ty->getIntegerBitWidth();

    assert(bit_width <= 64);

    auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);
    if (bit_width == 64) {
        // no-op
        AsmReg orig;
        auto   res_ref = this->result_ref_salvage_with_original(
            inst_idx, 0, std::move(src_ref), orig);
        if (orig != res_ref.cur_reg()) {
            derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
        }
        this->set_value(res_ref, res_ref.cur_reg());

        return true;
    }

    auto res_ref     = this->result_ref_lazy(inst_idx, 0);
    auto res_scratch = ScratchReg{derived()};

    if (bit_width == 32) {
        derived()->encode_zext_32_to_64(std::move(src_ref), res_scratch);
    } else if (bit_width == 8) {
        derived()->encode_zext_8_to_32(std::move(src_ref), res_scratch);
    } else if (bit_width == 16) {
        derived()->encode_zext_16_to_32(std::move(src_ref), res_scratch);
    } else if (bit_width < 32) {
        u64 mask = (1ull << bit_width) - 1;
        derived()->encode_landi32(
            std::move(src_ref), EncodeImm{mask}, res_scratch);
    } else {
        assert(bit_width > 32 && bit_width < 64);
        u64 mask = (1ull << bit_width) - 1;
        derived()->encode_landi64(
            std::move(src_ref), EncodeImm{mask}, res_scratch);
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_bitcast(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    // at most this should be fine to implement as a copy operation
    // as the values cannot be aggregates
    const auto src_idx = llvm_val_idx(inst->getOperand(0));

    // TODO(ts): support 128bit values
    if (derived()->val_part_count(src_idx) != 1
        || derived()->val_part_count(inst_idx) != 1) {
        return false;
    }

    auto src_ref = this->val_ref(src_idx, 0);

    ScratchReg   orig_scratch{derived()};
    AsmReg       orig;
    ValuePartRef res_ref;
    if (derived()->val_part_bank(src_idx, 0)
        == derived()->val_part_bank(inst_idx, 0)) {
        res_ref = this->result_ref_salvage_with_original(
            inst_idx, 0, std::move(src_ref), orig);
    } else {
        res_ref = this->result_ref_eager(inst_idx, 0);
        orig    = this->val_as_reg(src_ref, orig_scratch);
    }

    if (orig != res_ref.cur_reg()) {
        derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_cmpxchg(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    using AsmOperand = typename Derived::AsmOperand;

    auto *cmpxchg = llvm::cast<llvm::AtomicCmpXchgInst>(inst);

    const auto succ_order = cmpxchg->getSuccessOrdering();
    const auto fail_order = cmpxchg->getFailureOrdering();

    // ptr, cmp, new_val, old_val, success
    bool (Derived::*encode_ptr)(
        AsmOperand, AsmOperand, AsmOperand, ScratchReg &, ScratchReg &) =
        nullptr;

    if (succ_order == llvm::AtomicOrdering::Monotonic) {
        assert(fail_order == llvm::AtomicOrdering::Monotonic);
        encode_ptr = &Derived::encode_cmpxchg_u64_monotonic_monotonic;
    } else if (succ_order == llvm::AtomicOrdering::Acquire) {
        if (fail_order == llvm::AtomicOrdering::Acquire) {
            encode_ptr = &Derived::encode_cmpxchg_u64_acquire_acquire;
        } else {
            assert(fail_order == llvm::AtomicOrdering::Monotonic);
            encode_ptr = &Derived::encode_cmpxchg_u64_acquire_monotonic;
        }
    } else if (succ_order == llvm::AtomicOrdering::Release) {
        if (fail_order == llvm::AtomicOrdering::Acquire) {
            encode_ptr = &Derived::encode_cmpxchg_u64_release_acquire;
        } else {
            assert(fail_order == llvm::AtomicOrdering::Monotonic);
            encode_ptr = &Derived::encode_cmpxchg_u64_release_monotonic;
        }
    } else if (succ_order == llvm::AtomicOrdering::AcquireRelease) {
        if (fail_order == llvm::AtomicOrdering::Acquire) {
            encode_ptr = &Derived::encode_cmpxchg_u64_acqrel_acquire;
        } else {
            assert(fail_order == llvm::AtomicOrdering::Monotonic);
            encode_ptr = &Derived::encode_cmpxchg_u64_acqrel_monotonic;
        }
    } else if (succ_order == llvm::AtomicOrdering::SequentiallyConsistent) {
        if (fail_order == llvm::AtomicOrdering::SequentiallyConsistent) {
            encode_ptr = &Derived::encode_cmpxchg_u64_seqcst_seqcst;
        } else if (fail_order == llvm::AtomicOrdering::Acquire) {
            encode_ptr = &Derived::encode_cmpxchg_u64_seqcst_acquire;
        } else {
            assert(fail_order == llvm::AtomicOrdering::Monotonic);
            encode_ptr = &Derived::encode_cmpxchg_u64_seqcst_monotonic;
        }
    }

    auto *ptr_val = cmpxchg->getPointerOperand();
    assert(ptr_val->getType()->isPointerTy());
    auto ptr_ref = this->val_ref(llvm_val_idx(ptr_val), 0);

    auto *cmp_val = cmpxchg->getCompareOperand();
    auto *new_val = cmpxchg->getNewValOperand();

    assert(new_val->getType()->isIntegerTy(64)
           || new_val->getType()->isPointerTy());

    auto cmp_ref = this->val_ref(llvm_val_idx(cmp_val), 0);
    auto new_ref = this->val_ref(llvm_val_idx(new_val), 0);

    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    res_ref.inc_ref_count();
    auto res_ref_high = this->result_ref_lazy(inst_idx, 1);

    ScratchReg orig_scratch{derived()};
    ScratchReg succ_scratch{derived()};

    assert(encode_ptr != nullptr);
    if (!(derived()->*encode_ptr)(std::move(ptr_ref),
                                  std::move(cmp_ref),
                                  std::move(new_ref),
                                  orig_scratch,
                                  succ_scratch)) {
        return false;
    }

    this->set_value(res_ref, orig_scratch);
    this->set_value(res_ref, succ_scratch);

    // clang-format off
    // TODO(ts): fusing with subsequent extractvalues + br's
    // e.g. clang generates
    // %4 = cmpxchg ptr %0, i64 %3, i64 1 seq_cst seq_cst, align 8
    // %5 = extractvalue { i64, i1 } %4, 1
    // %6 = extractvalue { i64, i1 } %4, 0
    // br i1 %5, label %7, label %2, !llvm.loop !3
    // clang-format on

    return true;
}
} // namespace tpde_llvm
