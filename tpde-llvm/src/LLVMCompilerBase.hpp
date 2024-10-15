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

    using IRValueRef        = typename Base::IRValueRef;
    using IRBlockRef        = typename Base::IRBlockRef;
    using IRFuncRef         = typename Base::IRFuncRef;
    using ScratchReg        = typename Base::ScratchReg;
    using ValuePartRef      = typename Base::ValuePartRef;
    using AssignmentPartRef = typename Base::AssignmentPartRef;
    using ValLocalIdx       = typename Base::ValLocalIdx;
    using InstRange         = typename Base::InstRange;

    using Assembler = typename Base::Assembler;
    using SymRef    = typename Assembler::SymRef;

    using AsmReg = typename Base::AsmReg;

    struct RelocInfo {
        enum RELOC_TYPE : uint8_t {
            RELOC_ABS,
            RELOC_PC32,
        };

        uint32_t   off;
        int32_t    addend;
        SymRef     sym;
        RELOC_TYPE type = RELOC_ABS;
    };

    struct ResolvedGEP {
        ValuePartRef                base;
        std::optional<ValuePartRef> index;
        u64                         scale;
        i64                         displacement;
    };

    struct VarRefInfo {
        IRValueRef val;
        bool       alloca;
        bool       local;
        u32        alloca_frame_off;
    };

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

    enum class OverflowOp {
        uadd,
        sadd,
        usub,
        ssub,
        umul,
        smul
    };

    // <is_func, idx>
    tsl::hopscotch_map<const llvm::GlobalValue *, std::pair<bool, u32>>
                        global_sym_lookup{};
    // TODO(ts): SmallVector?
    std::vector<SymRef> global_syms;

    tpde::util::SmallVector<VarRefInfo, 16>                    variable_refs{};
    tpde::util::SmallVector<std::pair<IRValueRef, SymRef>, 16> type_info_syms;

    SymRef sym_fmod    = Assembler::INVALID_SYM_REF;
    SymRef sym_fmodf   = Assembler::INVALID_SYM_REF;
    SymRef sym_floorf  = Assembler::INVALID_SYM_REF;
    SymRef sym_floor   = Assembler::INVALID_SYM_REF;
    SymRef sym_ceilf   = Assembler::INVALID_SYM_REF;
    SymRef sym_ceil    = Assembler::INVALID_SYM_REF;
    SymRef sym_roundf  = Assembler::INVALID_SYM_REF;
    SymRef sym_round   = Assembler::INVALID_SYM_REF;
    SymRef sym_memcpy  = Assembler::INVALID_SYM_REF;
    SymRef sym_memset  = Assembler::INVALID_SYM_REF;
    SymRef sym_memmove = Assembler::INVALID_SYM_REF;
    SymRef sym_resume  = Assembler::INVALID_SYM_REF;
    SymRef sym_powisf2 = Assembler::INVALID_SYM_REF;
    SymRef sym_powidf2 = Assembler::INVALID_SYM_REF;
    SymRef sym_trunc   = Assembler::INVALID_SYM_REF;
    SymRef sym_truncf  = Assembler::INVALID_SYM_REF;

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

    void define_func_idx(IRFuncRef func, const u32 idx) noexcept;
    bool hook_post_func_sym_init() noexcept;
    bool global_init_to_data(const llvm::Value                     *reloc_base,
                             tpde::util::SmallVector<u8, 64>       &data,
                             tpde::util::SmallVector<RelocInfo, 8> &relocs,
                             const llvm::DataLayout                &layout,
                             const llvm::Constant                  *constant,
                             u32 off) noexcept;
    bool
        global_const_expr_to_data(const llvm::Value               *reloc_base,
                                  tpde::util::SmallVector<u8, 64> &data,
                                  tpde::util::SmallVector<RelocInfo, 8> &relocs,
                                  const llvm::DataLayout                &layout,
                                  llvm::Instruction                     *expr,
                                  u32 off) noexcept;

    IRValueRef llvm_val_idx(const llvm::Value *) const noexcept;
    IRValueRef llvm_val_idx(const llvm::Instruction *) const noexcept;

    SymRef get_or_create_sym_ref(SymRef          &sym,
                                 std::string_view name,
                                 bool             local = false) noexcept;
    SymRef global_sym(const llvm::GlobalValue *global) const noexcept;

    void setup_var_ref_assignments() noexcept;

    bool compile();

    bool compile_inst(IRValueRef, InstRange) noexcept;

    bool   compile_ret(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_load(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_store(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_int_binary_op(IRValueRef,
                                 llvm::Instruction *,
                                 IntBinaryOp op) noexcept;
    bool   compile_float_binary_op(IRValueRef,
                                   llvm::Instruction *,
                                   FloatBinaryOp op) noexcept;
    bool   compile_fneg(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_float_ext_trunc(IRValueRef,
                                   llvm::Instruction *,
                                   bool trunc) noexcept;
    bool   compile_float_to_int(IRValueRef,
                                llvm::Instruction *,
                                bool sign) noexcept;
    bool   compile_int_to_float(IRValueRef,
                                llvm::Instruction *,
                                bool sign) noexcept;
    bool   compile_int_trunc(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_int_ext(IRValueRef, llvm::Instruction *, bool sign) noexcept;
    bool   compile_ptr_to_int(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_int_to_ptr(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_bitcast(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_extract_value(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_insert_value(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_cmpxchg(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_phi(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_freeze(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_call(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_select(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_gep(IRValueRef, llvm::Instruction *, InstRange) noexcept;
    bool   compile_fcmp(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_switch(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_invoke(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_landing_pad(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_resume(IRValueRef, llvm::Instruction *) noexcept;
    SymRef lookup_type_info_sym(IRValueRef value) noexcept;
    bool   compile_intrin(IRValueRef,
                          llvm::Instruction *,
                          llvm::Function *) noexcept;
    bool   compile_is_fpclass(IRValueRef, llvm::Instruction *) noexcept;
    bool   compile_overflow_intrin(IRValueRef,
                                   llvm::Instruction *,
                                   OverflowOp) noexcept;

    bool compile_unreachable(IRValueRef, llvm::Instruction *) noexcept {
        return false;
    }

    bool compile_alloca(IRValueRef, llvm::Instruction *) noexcept {
        return false;
    }

    bool compile_br(IRValueRef, llvm::Instruction *) noexcept { return false; }

    bool compile_call_inner(IRValueRef,
                            llvm::CallInst *,
                            std::variant<SymRef, ValuePartRef> &,
                            bool) noexcept {
        return false;
    }

    bool compile_icmp(IRValueRef, llvm::Instruction *, InstRange) noexcept {
        return false;
    }

    void create_helper_call(std::span<IRValueRef>   args,
                            std::span<ValuePartRef> results,
                            SymRef                  sym) {
        (void)args;
        (void)results;
        (void)sym;
        assert(0);
    }

    bool handle_intrin(IRValueRef,
                       llvm::Instruction *,
                       llvm::Function *) noexcept {
        return false;
    }
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
            default: return {};
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
        u32   size = 0, bank = 0;
        auto *val_ty = val->getType();
        assert(val_ty->isAggregateType());

        const auto llvm_part_count = val_ty->getNumContainedTypes();
        const auto ty_idx = this->adaptor->values[static_cast<u32>(local_idx)]
                                .complex_part_tys_idx;
        u32 real_part_count = 0;
        for (u32 i = 0; i < llvm_part_count; ++i) {
            const auto part_ty =
                this->adaptor->complex_part_types[ty_idx + real_part_count];
            const auto part_count = derived()->basic_ty_part_count(
                this->adaptor->complex_part_types[ty_idx + real_part_count]);

            if (i <= part && i + part_count > part) {
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
            }

            // skip over duplicate entries when LLVM part has multiple TPDE
            // parts
            real_part_count += part_count;
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
void LLVMCompilerBase<Adaptor, Derived, Config>::define_func_idx(
    IRFuncRef func, const u32 idx) noexcept {
    global_sym_lookup[func] = std::make_pair(true, idx);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::
    hook_post_func_sym_init() noexcept {
    // create global symbols and their definitions
    const auto &llvm_mod    = this->adaptor->mod;
    auto       &data_layout = llvm_mod.getDataLayout();

    global_sym_lookup.reserve(2 * llvm_mod.global_size());

    // create the symbols first so that later relocations don't try to look up
    // non-existant symbols
    for (auto it = llvm_mod.global_begin(); it != llvm_mod.global_end(); ++it) {
        const llvm::GlobalVariable *gv = &*it;
        if (gv->hasAppendingLinkage()) [[unlikely]] {
            if (gv->getName() != "llvm.global_ctors"
                && gv->getName() != "llvm.global_dtors") {
                TPDE_LOG_ERR("Unknown global with appending linkage: {}\n",
                             static_cast<std::string_view>(gv->getName()));
                assert(0);
                return false;
            }
            continue;
        }

        // TODO(ts): we ignore weak linkage here, should emit a weak symbol for
        // it in the data section and place an undef symbol in the symbol
        // lookup
        if (gv->hasInitializer()) {
            auto sym = this->assembler.sym_predef_data(
                gv->getName(),
                gv->hasLocalLinkage() || gv->hasPrivateLinkage(),
                gv->hasLinkOnceODRLinkage() || gv->hasCommonLinkage());

            const auto idx = global_syms.size();
            global_syms.push_back(sym);
            global_sym_lookup.insert_or_assign(gv, std::make_pair(false, idx));
        } else {
            // TODO(ts): should we use getValueName here?
            auto sym = this->assembler.sym_add_undef(gv->getName(), false);
            const auto idx = global_syms.size();
            global_syms.push_back(sym);
            global_sym_lookup.insert_or_assign(gv, std::make_pair(false, idx));
        }
    }

    for (auto it = llvm_mod.alias_begin(); it != llvm_mod.alias_end(); ++it) {
        const llvm::GlobalAlias *ga  = &*it;
        const auto               sym = this->assembler.sym_add_undef(
            ga->getName(), ga->hasLocalLinkage() || ga->hasPrivateLinkage());
        const auto idx = global_syms.size();
        global_syms.push_back(sym);
        global_sym_lookup.insert_or_assign(ga, std::make_pair(false, idx));
    }

    // since the adaptor exposes all functions in the module to TPDE,
    // all function symbols are already added

    // now we can initialize the global data
    tpde::util::SmallVector<u8, 64>       data;
    tpde::util::SmallVector<RelocInfo, 8> relocs;
    for (auto it = llvm_mod.global_begin(); it != llvm_mod.global_end(); ++it) {
        auto *gv = &*it;
        if (!gv->hasInitializer()) {
            continue;
        }

        auto *init = gv->getInitializer();
        if (gv->hasAppendingLinkage()) [[unlikely]] {
            tpde::util::SmallVector<std::pair<SymRef, u32>, 16> functions;
            assert(gv->getName() == "llvm.global_ctors"
                   || gv->getName() == "llvm.global_dtors");
            if (llvm::isa<llvm::ConstantAggregateZero>(init)) {
                continue;
            }

            auto *array = llvm::dyn_cast<llvm::ConstantArray>(init);
            assert(array);

            u32 max = array->getNumOperands();
            // see
            // https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable
            for (auto i = 0u; i < max; ++i) {
                auto *entry = array->getOperand(i);
                auto *prio  = llvm::dyn_cast<llvm::ConstantInt>(
                    entry->getAggregateElement(static_cast<unsigned>(0)));
                assert(prio);
                auto *ptr = llvm::dyn_cast<llvm::GlobalValue>(
                    entry->getAggregateElement(1));
                assert(ptr);
                // we should not need to care about the third element
                assert(global_sym_lookup.contains(ptr));
                functions.emplace_back(global_sym(ptr), prio->getZExtValue());
            }

            const auto is_ctor = (gv->getName() == "llvm.global_ctors");
            u32        off;
            // TODO(ts): this hardcodes the ELF assembler
            if (is_ctor) {
                std::sort(functions.begin(),
                          functions.end(),
                          [](auto &lhs, auto &rhs) {
                              return lhs.second < rhs.second;
                          });
                auto &sec = this->assembler.sec_init_array;
                off       = sec.data.size();
                sec.data.resize(sec.data.size()
                                + functions.size() * sizeof(uint64_t));
            } else {
                std::sort(functions.begin(),
                          functions.end(),
                          [](auto &lhs, auto &rhs) {
                              return lhs.second > rhs.second;
                          });
                auto &sec = this->assembler.sec_fini_array;
                off       = sec.data.size();
                sec.data.resize(sec.data.size()
                                + functions.size() * sizeof(uint64_t));
            }

            for (auto i = 0u; i < functions.size(); ++i) {
                this->assembler.reloc_abs_init(
                    functions[i].first, is_ctor, off + i * sizeof(u64), 0);
            }
            continue;
        }

        // check if the data value is a zero aggregate and put into bss if that
        // is the case
        if (llvm::isa<llvm::ConstantAggregateZero>(init)) {
            auto sym = global_sym(gv);
            this->assembler.sym_def_predef_bss(
                sym,
                data_layout.getTypeAllocSize(init->getType()),
                gv->getAlign().valueOrOne().value());
            continue;
        }

        data.clear();
        relocs.clear();

        data.resize(data_layout.getTypeAllocSize(init->getType()));
        if (!global_init_to_data(gv, data, relocs, data_layout, init, 0)) {
            return false;
        }

        u32  off;
        auto read_only = gv->isConstant();
        auto sym       = global_sym(gv);
        this->assembler.sym_def_predef_data(sym,
                                            read_only,
                                            !relocs.empty(),
                                            data,
                                            gv->getAlign().valueOrOne().value(),
                                            &off);
        for (auto &[inner_off, addend, target, type] : relocs) {
            if (type == RelocInfo::RELOC_ABS) {
                this->assembler.reloc_data_abs(
                    target, read_only, off + inner_off, addend);
            } else {
                assert(type == RelocInfo::RELOC_PC32);
                this->assembler.reloc_data_pc32(
                    target, read_only, off + inner_off, addend);
            }
        }
    }

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::global_init_to_data(
    const llvm::Value                     *reloc_base,
    tpde::util::SmallVector<u8, 64>       &data,
    tpde::util::SmallVector<RelocInfo, 8> &relocs,
    const llvm::DataLayout                &layout,
    const llvm::Constant                  *constant,
    u32                                    off) noexcept {
    const auto alloc_size = layout.getTypeAllocSize(constant->getType());
    assert(off + alloc_size <= data.size());
    // can't do this since for floats -0 is special
    // if (constant->isZeroValue()) {
    //    return true;
    //}

    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(constant); CI) {
        // TODO: endianess?
        llvm::StoreIntToMemory(CI->getValue(), data.data() + off, alloc_size);
        return true;
    }
    if (auto *CF = llvm::dyn_cast<llvm::ConstantFP>(constant); CF) {
        // TODO: endianess?
        llvm::StoreIntToMemory(
            CF->getValue().bitcastToAPInt(), data.data() + off, alloc_size);
        return true;
    }
    if (auto *CDS = llvm::dyn_cast<llvm::ConstantDataSequential>(constant);
        CDS) {
        auto d = CDS->getRawDataValues();
        assert(d.size() <= alloc_size);
        std::copy(d.bytes_begin(), d.bytes_end(), data.begin() + off);
        return true;
    }
    if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(constant); CA) {
        const auto num_elements = CA->getType()->getNumElements();
        const auto element_size =
            layout.getTypeAllocSize(CA->getType()->getElementType());
        for (auto i = 0u; i < num_elements; ++i) {
            global_init_to_data(reloc_base,
                                data,
                                relocs,
                                layout,
                                CA->getAggregateElement(i),
                                off + i * element_size);
        }
        return true;
    }
    if (auto *CA = llvm::dyn_cast<llvm::ConstantAggregate>(constant); CA) {
        const auto num_elements = CA->getType()->getStructNumElements();
        auto      &ctx          = constant->getContext();

        auto *ty = CA->getType();
        auto  c0 = llvm::ConstantInt::get(ctx, llvm::APInt(32, 0, false));

        for (auto i = 0u; i < num_elements; ++i) {
            auto idx =
                llvm::ConstantInt::get(ctx, llvm::APInt(32, (u64)i, false));
            auto agg_off = layout.getIndexedOffsetInType(ty, {c0, idx});
            global_init_to_data(reloc_base,
                                data,
                                relocs,
                                layout,
                                CA->getAggregateElement(i),
                                off + agg_off);
        }
        return true;
    }
    if (auto *ud = llvm::dyn_cast<llvm::UndefValue>(constant); ud) {
        // just leave it at 0
        return true;
    }
    if (auto *zero = llvm::dyn_cast<llvm::ConstantAggregateZero>(constant);
        zero) {
        // leave at 0
        return true;
    }
    if (auto *null = llvm::dyn_cast<llvm::ConstantPointerNull>(constant);
        null) {
        // leave at 0
        return true;
    }
    if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(constant); GV) {
        assert(alloc_size == 8);
        const auto sym = global_sym(GV);
        assert(sym != Assembler::INVALID_SYM_REF);
        relocs.push_back({off, 0, sym});
        return true;
    }
    if (auto *GA = llvm::dyn_cast<llvm::GlobalAlias>(constant); GA) {
        assert(alloc_size == 8);
        const auto sym = global_sym(GA);
        assert(sym != Assembler::INVALID_SYM_REF);
        relocs.push_back({off, 0, sym});
        return true;
    }
    if (auto *FN = llvm::dyn_cast<llvm::Function>(constant); FN) {
        // TODO: we create more work for the linker than we need so we should
        // fix this sometime
        assert(!FN->isIntrinsic());
        assert(alloc_size == 8);
        const auto sym = global_sym(FN);
        assert(sym != Assembler::INVALID_SYM_REF);
        relocs.push_back({off, 0, sym});
        return true;
    }
    if (auto *CE = llvm::dyn_cast<llvm::ConstantExpr>(constant); CE) {
        auto      *inst = CE->getAsInstruction();
        const auto res  = global_const_expr_to_data(
            reloc_base, data, relocs, layout, inst, off);
        inst->deleteValue();
        return res;
    }

    TPDE_LOG_ERR("Encountered unknown constant in global initializer");
#ifdef TPDE_DEBUG
    constant->print(llvm::errs(), true);
    llvm::errs() << "\n";
#endif

    assert(0);
    return false;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::global_const_expr_to_data(
    const llvm::Value                     *reloc_base,
    tpde::util::SmallVector<u8, 64>       &data,
    tpde::util::SmallVector<RelocInfo, 8> &relocs,
    const llvm::DataLayout                &layout,
    llvm::Instruction                     *expr,
    u32                                    off) noexcept {
    // idk about this design, currently just hardcoding stuff i see
    // in theory i think this needs a new data buffer so we can recursively call
    // parseConstIntoByteArray
    switch (expr->getOpcode()) {
    case llvm::Instruction::IntToPtr: {
        auto *op = expr->getOperand(0);
        if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(op); CI) {
            auto alloc_size = layout.getTypeAllocSize(expr->getType());
            // TODO: endianess?
            llvm::StoreIntToMemory(
                CI->getValue(), data.data() + off, alloc_size);
            return true;
        } else {
            TPDE_LOG_ERR("Operand to IntToPtr is not a constant int");
            assert(0);
            return false;
        }
    }
    case llvm::Instruction::GetElementPtr: {
        auto  *gep = llvm::cast<llvm::GetElementPtrInst>(expr);
        auto  *ptr = gep->getPointerOperand();
        SymRef ptr_sym;
        if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(ptr); GV) {
            ptr_sym = global_sym(GV);
        } else {
            assert(0);
            return false;
        }

        auto indices = tpde::util::SmallVector<llvm::Value *, 8>{};
        for (auto &idx : gep->indices()) {
            indices.push_back(idx.get());
        }

        const auto ty_off = layout.getIndexedOffsetInType(
            gep->getSourceElementType(),
            llvm::ArrayRef{indices.data(), indices.size()});
        relocs.push_back({off, static_cast<int32_t>(ty_off), ptr_sym});

        return true;
    }
    case llvm::Instruction::Trunc: {
        // recognize a truncation pattern where we need to emit PC32 relocations
        // i32 trunc (i64 sub (i64 ptrtoint (ptr <someglobal> to i64), i64
        // ptrtoint (ptr <relocBase> to i64)))
        if (expr->getType()->isIntegerTy(32)) {
            if (auto *sub =
                    llvm::dyn_cast<llvm::ConstantExpr>(expr->getOperand(0));
                sub && sub->getOpcode() == llvm::Instruction::Sub
                && sub->getType()->isIntegerTy(64)) {
                if (auto *lhs_ptr_to_int =
                        llvm::dyn_cast<llvm::ConstantExpr>(sub->getOperand(0)),
                    *rhs_ptr_to_int =
                        llvm::dyn_cast<llvm::ConstantExpr>(sub->getOperand(1));
                    lhs_ptr_to_int && rhs_ptr_to_int
                    && lhs_ptr_to_int->getOpcode()
                           == llvm::Instruction::PtrToInt
                    && rhs_ptr_to_int->getOpcode()
                           == llvm::Instruction::PtrToInt) {
                    if (rhs_ptr_to_int->getOperand(0) == reloc_base
                        && llvm::isa<llvm::GlobalVariable>(
                            lhs_ptr_to_int->getOperand(0))) {
                        auto ptr_sym =
                            global_sym(llvm::dyn_cast<llvm::GlobalValue>(
                                lhs_ptr_to_int->getOperand(0)));

                        relocs.push_back({off,
                                          static_cast<int32_t>(off),
                                          ptr_sym,
                                          RelocInfo::RELOC_PC32});
                        return true;
                    }
                }
            }
        }
    }
    default: {
        TPDE_LOG_ERR("Unknown constant expression in global initializer");
        assert(0);
        return false;
    }
    }

    return false;
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
    if (sym != Assembler::INVALID_SYM_REF) [[likely]] {
        return sym;
    }

    sym = this->assembler.sym_add_undef(name, local);
    return sym;
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::global_sym(
        const llvm::GlobalValue *global) const noexcept {
    assert(global != nullptr);
    auto it = global_sym_lookup.find(global);
    if (it == global_sym_lookup.end()) {
        assert(0);
        return Assembler::INVALID_SYM_REF;
    }
    return it->second.first ? this->func_syms[it->second.second]
                            : global_syms[it->second.second];
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::
    setup_var_ref_assignments() noexcept {
    using AssignmentPartRef = typename Base::AssignmentPartRef;
    variable_refs.clear();

    variable_refs.resize(this->adaptor->initial_stack_slot_indices.size()
                         + this->adaptor->global_idx_end
                         + this->adaptor->funcs_as_operands.size());


    u32        cur_idx         = 0;
    const auto init_assignment = [this, &cur_idx](IRValueRef v) {
        auto *assignment = this->allocate_assignment(1, true);
        assignment->initialize(cur_idx++,
                               Config::PLATFORM_POINTER_SIZE,
                               0,
                               Config::PLATFORM_POINTER_SIZE);
        this->assignments.value_ptrs[this->adaptor->val_local_idx(v)] =
            assignment;

        auto ap = AssignmentPartRef{assignment, 0};
        ap.set_bank(Config::GP_BANK);
        ap.set_variable_ref(true);
        ap.set_part_size(Config::PLATFORM_POINTER_SIZE);
    };

    // Allocate registers for TPDE's stack slots
    for (auto v : this->adaptor->cur_static_allocas()) {
        variable_refs[cur_idx].val    = v;
        variable_refs[cur_idx].alloca = true;
        // static allocas don't need to be compiled later
        this->adaptor->val_set_fused(v, true);

        auto size = this->adaptor->val_alloca_size(v);
        size = tpde::util::align_up(size, this->adaptor->val_alloca_align(v));
        // round up stack allocation to next power of two
        if (size == 3) {
            size = 4;
        } else if (size > 4 && size < 8) {
            size = 8;
        } else if (size > 8 && size < 16) {
            size = 16;
        }

        const auto frame_off = this->allocate_stack_slot(size);

        variable_refs[cur_idx].alloca_frame_off = frame_off;
        init_assignment(v);
    }

    // Allocate regs for globals
    for (u32 v = 0; v < this->adaptor->global_idx_end; ++v) {
        variable_refs[cur_idx].val    = v;
        variable_refs[cur_idx].alloca = false;
        variable_refs[cur_idx].local =
            !llvm::dyn_cast<llvm::GlobalValue>(this->adaptor->values[v].val)
                 ->hasExternalLinkage();

        init_assignment(v);
    }
    for (auto v : this->adaptor->funcs_as_operands) {
        variable_refs[cur_idx].val    = v;
        variable_refs[cur_idx].alloca = false;
        variable_refs[cur_idx].local =
            !llvm::dyn_cast<llvm::GlobalValue>(this->adaptor->values[v].val)
                 ->hasExternalLinkage();
        init_assignment(v);
    }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile() {
    if (!Base::compile()) {
        return false;
    }

    // copy alias symbol definitions
    for (auto it = this->adaptor->mod.alias_begin();
         it != this->adaptor->mod.alias_end();
         ++it) {
        llvm::GlobalAlias *ga = &*it;
        auto              *alias_target =
            llvm::dyn_cast<llvm::GlobalValue>(ga->getAliasee());
        if (alias_target == nullptr) {
            assert(0);
            continue;
        }
        auto dst_sym  = global_sym(ga);
        auto from_sym = global_sym(alias_target);

        this->assembler.sym_copy(dst_sym,
                                 from_sym,
                                 ga->hasLocalLinkage()
                                     || ga->hasPrivateLinkage(),
                                 ga->hasLinkOnceODRLinkage());
    }

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_inst(
    IRValueRef val_idx, InstRange remaining) noexcept {
    TPDE_LOG_TRACE("Compiling inst {} ({})",
                   val_idx,
                   this->adaptor->value_fmt_ref(val_idx));

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
    case llvm::Instruction::ExtractValue:
        return compile_extract_value(val_idx, i);
    case llvm::Instruction::InsertValue:
        return compile_insert_value(val_idx, i);
    case llvm::Instruction::AtomicCmpXchg: return compile_cmpxchg(val_idx, i);
    case llvm::Instruction::PHI: return compile_phi(val_idx, i);
    case llvm::Instruction::Freeze: return compile_freeze(val_idx, i);
    case llvm::Instruction::Unreachable:
        return derived()->compile_unreachable(val_idx, i);
    case llvm::Instruction::Alloca:
        return derived()->compile_alloca(val_idx, i);
    case llvm::Instruction::Br: return derived()->compile_br(val_idx, i);
    case llvm::Instruction::Call: return compile_call(val_idx, i);
    case llvm::Instruction::Select: return compile_select(val_idx, i);
    case llvm::Instruction::GetElementPtr:
        return compile_gep(val_idx, i, remaining);
    case llvm::Instruction::ICmp:
        return derived()->compile_icmp(val_idx, i, remaining);
    case llvm::Instruction::FCmp: return compile_fcmp(val_idx, i);
    case llvm::Instruction::Switch: return compile_switch(val_idx, i);
    case llvm::Instruction::Invoke: return compile_invoke(val_idx, i);
    case llvm::Instruction::LandingPad: return compile_landing_pad(val_idx, i);
    case llvm::Instruction::Resume: return compile_resume(val_idx, i);

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
    using AsmOperand = typename Derived::AsmOperand;

    assert(llvm::isa<llvm::LoadInst>(inst));
    auto *load = llvm::cast<llvm::LoadInst>(inst);


    auto res = this->result_ref_lazy(load_idx, 0);
    // TODO(ts): if the ref-count is <= 1, then skip emitting the load as LLVM
    // does that, too. at least on ARM

    const auto calc_ptr_op = [this, load]() -> AsmOperand {
        auto ptr_ref =
            this->val_ref(llvm_val_idx(load->getPointerOperand()), 0);
        if (!ptr_ref.is_const && ptr_ref.assignment().variable_ref()) {
            const auto ref_idx = ptr_ref.state.v.assignment->var_ref_custom_idx;
            if (this->variable_refs[ref_idx].alloca) {
                return derived()->create_addr_for_alloca(ref_idx);
            }
        }

        return std::move(ptr_ref);
    };

    if (load->isAtomic()) {
        using AsmOperand = typename Derived::AsmOperand;
        u32 width        = 64;
        if (load->getType()->isIntegerTy()) {
            width = load->getType()->getIntegerBitWidth();
            if (width != 8 && width != 16 && width != 32 && width != 64) {
                TPDE_LOG_ERR(
                    "atomic loads not of i8/i16/i32/i64/ptr not supported");
                return false;
            }
        } else if (!load->getType()->isPointerTy()) {
            TPDE_LOG_ERR(
                "atomic loads not of i8/i16/i32/i64/ptr not supported");
            return false;
        }
        u32 needed_align = 1;
        switch (width) {
        case 16: needed_align = 2; break;
        case 32: needed_align = 4; break;
        case 64: needed_align = 8; break;
        }
        if (load->getAlign().value() < needed_align) {
            TPDE_LOG_ERR(
                "atomic load of width {} has alignment {} which is too small",
                width,
                load->getAlign().value());
            return false;
        }

        const auto order     = load->getOrdering();
        using EncodeFnTy     = bool (Derived::*)(AsmOperand, ScratchReg &);
        EncodeFnTy encode_fn = nullptr;
        if (order == llvm::AtomicOrdering::Monotonic) {
            switch (width) {
            case 8: encode_fn = &Derived::encode_atomic_load_u8_mono; break;
            case 16: encode_fn = &Derived::encode_atomic_load_u16_mono; break;
            case 32: encode_fn = &Derived::encode_atomic_load_u32_mono; break;
            case 64: encode_fn = &Derived::encode_atomic_load_u64_mono; break;
            default: __builtin_unreachable();
            }
        } else if (order == llvm::AtomicOrdering::Acquire) {
            switch (width) {
            case 8: encode_fn = &Derived::encode_atomic_load_u8_acq; break;
            case 16: encode_fn = &Derived::encode_atomic_load_u16_acq; break;
            case 32: encode_fn = &Derived::encode_atomic_load_u32_acq; break;
            case 64: encode_fn = &Derived::encode_atomic_load_u64_acq; break;
            default: __builtin_unreachable();
            }
        } else {
            assert(order == llvm::AtomicOrdering::SequentiallyConsistent);
            switch (width) {
            case 8: encode_fn = &Derived::encode_atomic_load_u8_seqcst; break;
            case 16: encode_fn = &Derived::encode_atomic_load_u16_seqcst; break;
            case 32: encode_fn = &Derived::encode_atomic_load_u32_seqcst; break;
            case 64: encode_fn = &Derived::encode_atomic_load_u64_seqcst; break;
            default: __builtin_unreachable();
            }
        }

        ScratchReg res_scratch{derived()};
        auto       ptr_op = calc_ptr_op();
        if (!(derived()->*encode_fn)(std::move(ptr_op), res_scratch)) {
            return false;
        }

        this->set_value(res, res_scratch);
        return true;
    }

    ScratchReg res_scratch{this};
    switch (this->adaptor->values[load_idx].type) {
        using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64: {
        assert(load->getType()->isIntegerTy());
        auto       ptr_op    = calc_ptr_op();
        const auto bit_width = load->getType()->getIntegerBitWidth();
        switch (bit_width) {
        case 1:
        case 8: derived()->encode_loadi8(std::move(ptr_op), res_scratch); break;
        case 16:
            derived()->encode_loadi16(std::move(ptr_op), res_scratch);
            break;
        case 24:
            derived()->encode_loadi24(std::move(ptr_op), res_scratch);
            break;
        case 32:
            derived()->encode_loadi32(std::move(ptr_op), res_scratch);
            break;
        case 40:
            derived()->encode_loadi40(std::move(ptr_op), res_scratch);
            break;
        case 48:
            derived()->encode_loadi48(std::move(ptr_op), res_scratch);
            break;
        case 56:
            derived()->encode_loadi56(std::move(ptr_op), res_scratch);
            break;
        case 64:
            derived()->encode_loadi64(std::move(ptr_op), res_scratch);
            break;
        default: assert(0); return false;
        }
        break;
    }
    case ptr: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_loadi64(std::move(ptr_op), res_scratch);
        break;
    }
    case i128: {
        auto       ptr_op = calc_ptr_op();
        ScratchReg res_scratch_high{derived()};
        res.inc_ref_count();
        auto res_high = this->result_ref_lazy(load_idx, 1);

        derived()->encode_loadi128(
            std::move(ptr_op), res_scratch, res_scratch_high);
        this->set_value(res, res_scratch);
        this->set_value(res_high, res_scratch_high);
        return true;
    }
    case v32:
    case f32: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_loadf32(std::move(ptr_op), res_scratch);
        break;
    }
    case v64:
    case f64: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_loadf64(std::move(ptr_op), res_scratch);
        break;
    }
    case v128: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_loadv128(std::move(ptr_op), res_scratch);
        break;
    }
    case v256: {
        auto ptr_op = calc_ptr_op();
        if (!derived()->encode_loadv256(std::move(ptr_op), res_scratch)) {
            return false;
        }
        break;
    }
    case v512: {
        auto ptr_op = calc_ptr_op();
        if (!derived()->encode_loadv512(std::move(ptr_op), res_scratch)) {
            return false;
        }
        break;
    }
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

        auto ptr_ref =
            this->val_ref(llvm_val_idx(load->getPointerOperand()), 0);
        ScratchReg ptr_scratch{derived()};
        auto       ptr_reg = this->val_as_reg(ptr_ref, ptr_scratch);
        for (u32 part_idx = 0; part_idx < part_count;
             ++part_idx, ++res_part_idx) {
            const auto part_ty =
                this->adaptor->complex_part_types[ty_idx + res_part_idx];

            auto part_addr = typename Derived::AsmOperand::ArbitraryAddress{
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
                case 8:
                    derived()->encode_loadi8(std::move(part_addr), res_scratch);
                    break;
                case 16:
                    derived()->encode_loadi16(std::move(part_addr),
                                              res_scratch);
                    break;
                case 24:
                    derived()->encode_loadi24(std::move(part_addr),
                                              res_scratch);
                    break;
                case 32:
                    derived()->encode_loadi32(std::move(part_addr),
                                              res_scratch);
                    break;
                case 40:
                    derived()->encode_loadi40(std::move(part_addr),
                                              res_scratch);
                    break;
                case 48:
                    derived()->encode_loadi48(std::move(part_addr),
                                              res_scratch);
                    break;
                case 56:
                    derived()->encode_loadi56(std::move(part_addr),
                                              res_scratch);
                    break;
                case 64:
                    derived()->encode_loadi64(std::move(part_addr),
                                              res_scratch);
                    break;
                default: assert(0); return false;
                }
                break;
            }
            case ptr:
                derived()->encode_loadi64(std::move(part_addr), res_scratch);
                break;
            case i128: {
                ScratchReg res_scratch_high{derived()};
                derived()->encode_loadi128(
                    std::move(part_addr), res_scratch, res_scratch_high);

                auto part_ref_high =
                    this->result_ref_lazy(load_idx, ++res_part_idx);
                this->set_value(part_ref_high, res_scratch_high);
                part_ref_high.reset_without_refcount();
                break;
            }
            case v32:
            case f32:
                derived()->encode_loadf32(std::move(part_addr), res_scratch);
                break;
            case v64:
            case f64:
                derived()->encode_loadf64(std::move(part_addr), res_scratch);
                break;
            case v128:
                derived()->encode_loadv128(std::move(part_addr), res_scratch);
                break;
            case v256:
                if (!derived()->encode_loadv256(std::move(part_addr),
                                                res_scratch)) {
                    return false;
                }
                break;
            case v512:
                if (!derived()->encode_loadv512(std::move(part_addr),
                                                res_scratch)) {
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
    using AsmOperand = typename Derived::AsmOperand;
    assert(llvm::isa<llvm::StoreInst>(inst));
    auto *store = llvm::cast<llvm::StoreInst>(inst);
    assert(!store->isAtomic());

    const auto calc_ptr_op = [this, store]() -> AsmOperand {
        auto ptr_ref =
            this->val_ref(llvm_val_idx(store->getPointerOperand()), 0);
        if (!ptr_ref.is_const && ptr_ref.assignment().variable_ref()) {
            const auto ref_idx = ptr_ref.state.v.assignment->var_ref_custom_idx;
            if (this->variable_refs[ref_idx].alloca) {
                return derived()->create_addr_for_alloca(ref_idx);
            }
        }

        return std::move(ptr_ref);
    };

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
        auto       ptr_op    = calc_ptr_op();
        switch (bit_width) {
        case 1:
        case 8:
            derived()->encode_storei8(std::move(ptr_op), std::move(op_ref));
            break;
        case 16:
            derived()->encode_storei16(std::move(ptr_op), std::move(op_ref));
            break;
        case 24:
            derived()->encode_storei24(std::move(ptr_op), std::move(op_ref));
            break;
        case 32:
            derived()->encode_storei32(std::move(ptr_op), std::move(op_ref));
            break;
        case 40:
            derived()->encode_storei40(std::move(ptr_op), std::move(op_ref));
            break;
        case 48:
            derived()->encode_storei48(std::move(ptr_op), std::move(op_ref));
            break;
        case 56:
            derived()->encode_storei56(std::move(ptr_op), std::move(op_ref));
            break;
        case 64:
            derived()->encode_storei64(std::move(ptr_op), std::move(op_ref));
            break;
        default: assert(0); return false;
        }
        break;
    }
    case ptr: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_storei64(std::move(ptr_op), std::move(op_ref));
        break;
    }
    case i128: {
        auto ptr_op = calc_ptr_op();
        op_ref.inc_ref_count();
        auto op_ref_high = this->val_ref(op_idx, 1);

        derived()->encode_storei128(
            std::move(ptr_op), std::move(op_ref), std::move(op_ref_high));
        break;
    }
    case v32:
    case f32: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_storef32(std::move(ptr_op), std::move(op_ref));
        break;
    }
    case v64:
    case f64: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_storef64(std::move(ptr_op), std::move(op_ref));
        break;
    }
    case v128: {
        auto ptr_op = calc_ptr_op();
        derived()->encode_storev128(std::move(ptr_op), std::move(op_ref));
        break;
    }
    case v256: {
        auto ptr_op = calc_ptr_op();
        if (!derived()->encode_storev256(std::move(ptr_op),
                                         std::move(op_ref))) {
            return false;
        }
        break;
    }
    case v512: {
        auto ptr_op = calc_ptr_op();
        if (!derived()->encode_storev512(std::move(ptr_op),
                                         std::move(op_ref))) {
            return false;
        }
        break;
    }
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

        auto ptr_ref =
            this->val_ref(llvm_val_idx(store->getPointerOperand()), 0);
        ScratchReg ptr_scratch{derived()};
        auto       ptr_reg = this->val_as_reg(ptr_ref, ptr_scratch);
        for (u32 part_idx = 0; part_idx < part_count;
             ++part_idx, ++res_part_idx) {
            const auto part_ty =
                this->adaptor->complex_part_types[ty_idx + res_part_idx];

            auto part_addr = typename Derived::AsmOperand::ArbitraryAddress{
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
                case 8:
                    derived()->encode_storei8(std::move(part_addr), part_ref);
                    break;
                case 16:
                    derived()->encode_storei16(std::move(part_addr), part_ref);
                    break;
                case 24:
                    derived()->encode_storei24(std::move(part_addr), part_ref);
                    break;
                case 32:
                    derived()->encode_storei32(std::move(part_addr), part_ref);
                    break;
                case 40:
                    derived()->encode_storei40(std::move(part_addr), part_ref);
                    break;
                case 48:
                    derived()->encode_storei48(std::move(part_addr), part_ref);
                    break;
                case 56:
                    derived()->encode_storei56(std::move(part_addr), part_ref);
                    break;
                case 64:
                    derived()->encode_storei64(std::move(part_addr), part_ref);
                    break;
                default: assert(0); return false;
                }
                break;
            }
            case ptr:
                derived()->encode_storei64(std::move(part_addr), part_ref);
                break;
            case i128: {
                auto part_ref_high = this->val_ref(op_idx, ++res_part_idx);
                derived()->encode_storei128(
                    std::move(part_addr), part_ref, part_ref_high);

                part_ref_high.reset_without_refcount();
                break;
            }
            case v32:
            case f32:
                derived()->encode_storef32(std::move(part_addr), part_ref);
                break;
            case v64:
            case f64:
                derived()->encode_storef64(std::move(part_addr), part_ref);
                break;
            case v128:
                derived()->encode_storev128(std::move(part_addr), part_ref);
                break;
            case v256:
                if (!derived()->encode_storev256(std::move(part_addr),
                                                 part_ref)) {
                    return false;
                }
                break;
            case v512:
                if (!derived()->encode_storev512(std::move(part_addr),
                                                 part_ref)) {
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
    (void)src_double;
    const auto dst_double = inst->getType()->isDoubleTy();
    (void)dst_double;

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
    if (bit_width != 64 && bit_width != 32 && bit_width != 16
        && bit_width != 8) {
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
                assert(bit_width <= 32);
                derived()->encode_f64toi32(std::move(src_ref), res_scratch);
            }
        } else {
            if (bit_width == 64) {
                derived()->encode_f32toi64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width <= 32);
                derived()->encode_f32toi32(std::move(src_ref), res_scratch);
            }
        }
    } else {
        if (src_double) {
            if (bit_width == 64) {
                derived()->encode_f64tou64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width <= 32);
                derived()->encode_f64tou32(std::move(src_ref), res_scratch);
            }
        } else {
            if (bit_width == 64) {
                derived()->encode_f32tou64(std::move(src_ref), res_scratch);
            } else {
                assert(bit_width <= 32);
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
    using AsmOperand = typename Derived::AsmOperand;
    using EncodeImm  = typename Derived::EncodeImm;

    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();
    assert(src_ty->isIntegerTy());

    auto *dst_ty = inst->getType();
    assert(dst_ty->isFloatTy() || dst_ty->isDoubleTy());

    auto bit_width = src_ty->getIntegerBitWidth();
    if (bit_width > 64) {
        assert(0);
        return false;
    }

    const auto dst_double = dst_ty->isDoubleTy();

    AsmOperand src_op      = this->val_ref(llvm_val_idx(src_val), 0);
    auto       res_ref     = this->result_ref_lazy(inst_idx, 0);
    auto       res_scratch = ScratchReg{derived()};

    if (bit_width != 8 && bit_width != 16 && bit_width != 32
        && bit_width != 64) {
        ScratchReg scratch{derived()};
        auto       res = false;
        if (bit_width <= 32) {
            if (sign) {
                res = derived()->encode_sext_arbitrary_to_32(
                    std::move(src_op), EncodeImm{32 - bit_width}, scratch);
            } else {
                res =
                    derived()->encode_landi32(std::move(src_op),
                                              EncodeImm{(1u << bit_width) - 1},
                                              scratch);
            }
            bit_width = 32;
        } else {
            if (sign) {
                res = derived()->encode_sext_arbitrary_to_64(
                    std::move(src_op), EncodeImm{64 - bit_width}, scratch);
            } else {
                u64 mask = (1ull << bit_width) - 1;
                res      = derived()->encode_landi64(
                    std::move(src_op), EncodeImm{mask}, scratch);
            }
            bit_width = 64;
        }
        if (!res) {
            return false;
        }
        src_op = std::move(scratch);
    }

    if (sign) {
        if (bit_width == 64) {
            if (dst_double) {
                derived()->encode_i64tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_i64tof32(std::move(src_op), res_scratch);
            }
        } else if (bit_width == 32) {
            if (dst_double) {
                derived()->encode_i32tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_i32tof32(std::move(src_op), res_scratch);
            }
        } else if (bit_width == 16) {
            if (dst_double) {
                derived()->encode_i16tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_i16tof32(std::move(src_op), res_scratch);
            }
        } else {
            assert(bit_width == 8);
            if (dst_double) {
                derived()->encode_i8tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_i8tof32(std::move(src_op), res_scratch);
            }
        }
    } else {
        if (bit_width == 64) {
            if (dst_double) {
                derived()->encode_u64tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_u64tof32(std::move(src_op), res_scratch);
            }
        } else if (bit_width == 32) {
            if (dst_double) {
                derived()->encode_u32tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_u32tof32(std::move(src_op), res_scratch);
            }
        } else if (bit_width == 16) {
            if (dst_double) {
                derived()->encode_u16tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_u16tof32(std::move(src_op), res_scratch);
            }
        } else {
            assert(bit_width == 8);
            if (dst_double) {
                derived()->encode_u8tof64(std::move(src_op), res_scratch);
            } else {
                derived()->encode_u8tof32(std::move(src_op), res_scratch);
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
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_extract_value(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    auto *extract = llvm::cast<llvm::ExtractValueInst>(inst);
    assert(extract->getNumIndices() == 1);

    auto *src_val = inst->getOperand(0);
    auto *src_ty  = src_val->getType();
    auto  src_idx = llvm_val_idx(src_val);
    assert(src_ty->isAggregateType());

    const auto target_idx = extract->getIndices()[0];

    const auto llvm_part_count = src_ty->getNumContainedTypes();
    const auto ty_idx = this->adaptor->values[src_idx].complex_part_tys_idx;

    u32 part_idx = 0;
    for (u32 llvm_part_idx = 0; llvm_part_idx < llvm_part_count;
         ++llvm_part_idx) {
        const auto part_ty =
            this->adaptor->complex_part_types[ty_idx + part_idx];
        assert(part_ty != LLVMBasicValType::complex);
        if (llvm_part_idx != target_idx) {
            part_idx += derived()->basic_ty_part_count(part_ty);
            continue;
        }

        auto part_ref = this->val_ref(src_idx, part_idx);

        if (part_ty == LLVMBasicValType::i128) {
            auto part_ref_high = this->val_ref(src_idx, part_idx + 1);
            part_ref_high.inc_ref_count();

            AsmReg orig;
            auto   res_ref_high = this->result_ref_salvage_with_original(
                inst_idx, 1, std::move(part_ref_high), orig, 2);

            if (orig != res_ref_high.cur_reg()) {
                derived()->mov(res_ref_high.cur_reg(), orig, 8);
            }
            this->set_value(res_ref_high, res_ref_high.cur_reg());
            res_ref_high.reset_without_refcount();
        }

        AsmReg orig;
        auto   res_ref = this->result_ref_salvage_with_original(
            inst_idx, 0, std::move(part_ref), orig);

        if (orig != res_ref.cur_reg()) {
            derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
        }
        this->set_value(res_ref, res_ref.cur_reg());
        return true;
    }

    return false;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_insert_value(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    auto *insert = llvm::cast<llvm::InsertValueInst>(inst);
    assert(insert->getNumIndices() == 1);

    auto      *agg_val = insert->getAggregateOperand();
    auto      *agg_ty  = agg_val->getType();
    const auto agg_idx = llvm_val_idx(agg_val);

    auto      *ins_val = insert->getInsertedValueOperand();
    const auto ins_idx = llvm_val_idx(ins_val);

    auto target_idx = insert->getIndices()[0];

    const auto llvm_part_count = agg_ty->getNumContainedTypes();
    const auto ty_idx = this->adaptor->values[agg_idx].complex_part_tys_idx;

    u32 part_idx = 0;
    for (u32 llvm_part_idx = 0; llvm_part_idx < llvm_part_count;
         ++llvm_part_idx) {
        const auto part_ty =
            this->adaptor->complex_part_types[ty_idx + part_idx];
        assert(part_ty != LLVMBasicValType::complex);

        const auto part_count = derived()->basic_ty_part_count(part_ty);
        // TODO(ts): I'd really like to always not do refcounting in the loop
        // and then refcount once when the loop is done
        for (u32 inner_part_idx = 0; inner_part_idx < part_count;
             ++inner_part_idx) {
            const auto copy_or_salvage =
                [this,
                 inst_idx,
                 part_idx,
                 inner_part_idx,
                 part_ty,
                 llvm_part_count,
                 part_count,
                 llvm_part_idx](ValuePartRef &&src_ref) {
                    AsmReg orig;
                    auto   res_ref = this->result_ref_salvage_with_original(
                        inst_idx,
                        part_idx + inner_part_idx,
                        std::move(src_ref),
                        orig,
                        (inner_part_idx != part_count - 1) ? 2 : 1);
                    if (orig != res_ref.cur_reg()) {
                        derived()->mov(res_ref.cur_reg(),
                                       orig,
                                       derived()->basic_ty_part_size(part_ty));
                    }
                    this->set_value(res_ref, res_ref.cur_reg());
                    if (llvm_part_idx != llvm_part_count - 1
                        || inner_part_idx != part_count - 1) {
                        res_ref.reset_without_refcount();
                    }
                };
            if (llvm_part_idx == target_idx) {
                assert(derived()->val_part_count(ins_idx) == part_count);
                auto ins_ref = this->val_ref(ins_idx, inner_part_idx);
                if (inner_part_idx != part_count - 1) {
                    ins_ref.inc_ref_count();
                }
                copy_or_salvage(std::move(ins_ref));
            } else {
                auto agg_ref =
                    this->val_ref(agg_idx, part_idx + inner_part_idx);
                if (inner_part_idx != part_count - 1
                    || (llvm_part_idx
                        != llvm_part_count
                               - (target_idx == llvm_part_count - 1 ? 2 : 1))) {
                    agg_ref.inc_ref_count();
                }
                copy_or_salvage(std::move(agg_ref));
            }
        }

        part_idx += part_count;
    }

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

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_phi(
    IRValueRef inst_idx, llvm::Instruction *) noexcept {
    // need to just ref-count the value
    this->val_ref(inst_idx, 0);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_freeze(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    // essentially a no-op
    auto      *src_val = inst->getOperand(0);
    const auto src_idx = llvm_val_idx(src_val);

    const auto part_count = derived()->val_part_count(src_idx);

    for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
        const auto last_part = (part_idx == part_count - 1);
        auto       src_ref   = this->val_ref(src_idx, part_idx);
        if (!last_part) {
            src_ref.inc_ref_count();
        }
        AsmReg orig;
        auto   res_ref = this->result_ref_salvage_with_original(
            inst_idx, part_idx, std::move(src_ref), orig, last_part ? 1 : 2);
        if (orig != res_ref.cur_reg()) {
            derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
        }
        this->set_value(res_ref, res_ref.cur_reg());

        if (!last_part) {
            res_ref.reset_without_refcount();
        }
    }

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_call(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    auto *call = llvm::cast<llvm::CallBase>(inst);

    std::variant<SymRef, ValuePartRef> call_target;
    auto                               var_arg = false;

    if (auto *fn = call->getCalledFunction(); fn) {
        if (fn->isIntrinsic()) {
            return compile_intrin(inst_idx, inst, fn);
        }

        // this is a direct call
        call_target = global_sym(fn);
        var_arg     = fn->getFunctionType()->isVarArg();
    } else {
        // either indirect call or call with mismatch of arguments
        var_arg  = call->getFunctionType()->isVarArg();
        auto *op = call->getCalledOperand();
        if (auto *fn = llvm::dyn_cast<llvm::Function>(op); fn) {
            call_target = global_sym(fn);
        } else if (auto *ga = llvm::dyn_cast<llvm::GlobalAlias>(op); fn) {
            // aliases also show up here
            // TODO(ts): do we need to check if the alias target is a function
            // and not a variable?
            call_target = global_sym(ga);
        } else {
            // indirect call
            auto target_ref = this->val_ref(llvm_val_idx(op), 0);
            call_target     = std::move(target_ref);
        }
    }

    return derived()->compile_call_inner(inst_idx, call, call_target, var_arg);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_select(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    auto ty = inst->getType();

    auto cond = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto lhs  = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    auto rhs  = this->val_ref(llvm_val_idx(inst->getOperand(2)), 0);

    ScratchReg res_scratch{derived()};
    auto       res_ref     = this->result_ref_lazy(inst_idx, 0);
    auto       pretend_128 = false;
    if (ty->isStructTy() && ty->getStructNumElements() == 2) {
        auto *ty0 = ty->getContainedType(0);
        auto *ty1 = ty->getContainedType(1);
        if (((ty0->isIntegerTy() && ty0->getIntegerBitWidth() <= 64)
             || ty0->isPointerTy())
            && ((ty1->isIntegerTy() && ty1->getIntegerBitWidth() <= 64)
                || ty1->isPointerTy())) {
            // for this special case we simply preten it is an 128 bit value to
            // get better codegen and I can skip supporting the full complex
            // case for now
            // TODO(ts): support full complex types using branches
            pretend_128 = true;
        }
    }

    if (ty->isIntegerTy() || ty->isPointerTy() || pretend_128) {
        auto width = 64u;
        if (ty->isIntegerTy()) {
            width = ty->getIntegerBitWidth();
        }

        if (width == 128 || pretend_128) {
            lhs.inc_ref_count();
            rhs.inc_ref_count();
            auto lhs_high = this->val_ref(llvm_val_idx(inst->getOperand(1)), 1);
            auto rhs_high = this->val_ref(llvm_val_idx(inst->getOperand(2)), 1);

            ScratchReg res_scratch_high{derived()};
            auto       res_ref_high = this->result_ref_lazy(inst_idx, 1);

            if (!derived()->encode_select_i128(std::move(cond),
                                               std::move(lhs),
                                               std::move(lhs_high),
                                               std::move(rhs),
                                               std::move(rhs_high),
                                               res_scratch,
                                               res_scratch_high)) {
                TPDE_LOG_ERR("Failed to encode select for i128");
                return false;
            }
            this->set_value(res_ref, res_scratch);
            this->set_value(res_ref_high, res_scratch_high);
            res_ref_high.reset_without_refcount();
            return true;
        }
        if (width <= 32) {
            if (!derived()->encode_select_i32(std::move(cond),
                                              std::move(lhs),
                                              std::move(rhs),
                                              res_scratch)) {
                TPDE_LOG_ERR("Failed to encode select for i32");
                return false;
            }
        } else if (width <= 64) {
            if (!derived()->encode_select_i64(std::move(cond),
                                              std::move(lhs),
                                              std::move(rhs),
                                              res_scratch)) {
                TPDE_LOG_ERR("Failed to encode select for i64");
                return false;
            }
        } else {
            TPDE_LOG_ERR("Invalid select width {}", width);
            return false;
        }
    } else if (ty->isFloatTy()) {
        if (!derived()->encode_select_f32(
                std::move(cond), std::move(lhs), std::move(rhs), res_scratch)) {
            TPDE_LOG_ERR("Failed to encode select for f32");
            return false;
        }
    } else if (ty->isDoubleTy()) {
        if (!derived()->encode_select_f64(
                std::move(cond), std::move(lhs), std::move(rhs), res_scratch)) {
            TPDE_LOG_ERR("Failed to encode select for f64");
            return false;
        }
    } else {
        TPDE_LOG_ERR("Invalid type for select");
        return false;
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_gep(
    IRValueRef         inst_idx,
    llvm::Instruction *inst,
    InstRange          remaining) noexcept {
    auto *gep = llvm::cast<llvm::GetElementPtrInst>(inst);

    auto                                      ptr = gep->getPointerOperand();
    tpde::util::SmallVector<llvm::Value *, 8> indices;
    std::optional<IRValueRef>                 variable_off = {};

    if (gep->hasIndices()) {
        auto idx_begin = gep->idx_begin(), idx_end = gep->idx_end();
        if (llvm::dyn_cast<llvm::Constant>(idx_begin->get()) == nullptr) {
            variable_off = llvm_val_idx(idx_begin->get());
            indices.push_back(llvm::ConstantInt::get(
                llvm::IntegerType::getInt64Ty(this->adaptor->context), 0));
            ++idx_begin;
        }

        for (auto it = idx_begin; it != idx_end; ++it) {
            assert(llvm::dyn_cast<llvm::Constant>(it->get()) != nullptr);
            indices.push_back(it->get());
        }
    }

    struct GEPTypeRange {
        llvm::Type *ty;
        uint32_t    start;
        uint32_t    end;
    };

    tpde::util::SmallVector<GEPTypeRange, 8> types;
    types.push_back({.ty    = gep->getSourceElementType(),
                     .start = 0,
                     .end   = (u32)indices.size()});

    // fuse geps
    // TODO: use llvm statistic or analyzer liveness stat?
    auto final_idx = inst_idx;
    while (gep->hasOneUser() && remaining.from != remaining.to) {
        auto  next_inst_idx = *remaining.from;
        auto *next_val      = this->adaptor->values[next_inst_idx].val;
        auto *next_gep      = llvm::dyn_cast<llvm::GetElementPtrInst>(next_val);
        if (!next_gep || next_gep->getPointerOperand() != gep
            || gep->getResultElementType() != next_gep->getResultElementType()
            || !next_gep->hasAllConstantIndices()) {
            break;
        }

        if (next_gep->hasIndices()) {
            // TODO: we should be able to fuse as long as this is a constant int
            auto *idx =
                llvm::cast<llvm::ConstantInt>(next_gep->idx_begin()->get());
            if (!idx->isZero()) {
                break;
            }

            u32 start = indices.size();
            indices.append(next_gep->idx_begin(), next_gep->idx_end());
            types.push_back({.ty    = next_gep->getSourceElementType(),
                             .start = start,
                             .end   = (u32)indices.size()});
        }

        this->adaptor->val_set_fused(next_inst_idx, true);
        final_idx = next_inst_idx; // we set the result for nextInst
        gep       = next_gep;
        ++remaining.from;
    }

    auto resolved = ResolvedGEP{};
    resolved.base = this->val_ref(llvm_val_idx(ptr), 0);

    auto &data_layout = this->adaptor->mod.getDataLayout();

    if (variable_off) {
        resolved.index          = this->val_ref(*variable_off, 0);
        const auto base_ty_size = data_layout.getTypeAllocSize(types[0].ty);
        resolved.scale          = base_ty_size;
    }

    i64 disp = 0;
    for (auto &[ty, start, end] : types) {
        if (start == end) {
            continue;
        }

        assert(start < indices.size());
        assert(end <= indices.size());
        disp += data_layout.getIndexedOffsetInType(
            ty,
            llvm::ArrayRef<llvm::Value *>{indices.data() + start,
                                          indices.data() + end});
    }
    resolved.displacement = disp;

    // TODO(ts): fusing

    auto addr    = derived()->resolved_gep_to_addr(resolved);
    auto res_ref = this->result_ref_lazy(final_idx, 0);

    ScratchReg res_scratch{derived()};
    derived()->addr_to_reg(std::move(addr), res_scratch);

    this->set_value(res_ref, res_scratch);

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_fcmp(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    using AsmOperand = typename Derived::AsmOperand;

    auto *cmp    = llvm::cast<llvm::FCmpInst>(inst);
    auto *cmp_ty = cmp->getOperand(0)->getType();
    assert(cmp_ty->isFloatTy() || cmp_ty->isDoubleTy());
    const auto is_double = cmp_ty->isDoubleTy();
    const auto pred      = cmp->getPredicate();

    if (pred == llvm::CmpInst::FCMP_FALSE || pred == llvm::CmpInst::FCMP_TRUE) {
        auto res_ref   = this->result_ref_eager(inst_idx, 0);
        auto const_ref = ValuePartRef{
            (pred == llvm::CmpInst::FCMP_FALSE ? 0u : 1u), Config::GP_BANK, 1};
        derived()->materialize_constant(const_ref, res_ref.cur_reg());
        this->set_value(res_ref, res_ref.cur_reg());
        return true;
    }

    using EncodeFnTy = bool (Derived::*)(AsmOperand, AsmOperand, ScratchReg &);
    using Pred       = llvm::CmpInst::Predicate;

// disable Wpedantic here since these are compiler extensions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wc99-designator"
    constexpr u32 F               = Pred::FIRST_FCMP_PREDICATE;
    EncodeFnTy encode_fns[Pred::LAST_FCMP_PREDICATE - Pred::FIRST_FCMP_PREDICATE
                          + 1][2] = {
        [Pred::FCMP_FALSE - F] = {                        nullptr,nullptr                                                                  },
        [Pred::FCMP_TRUE - F]  = {                        nullptr, nullptr},
        [Pred::FCMP_OEQ - F]   = {&Derived::encode_fcmp_oeq_float,
                                  &Derived::encode_fcmp_oeq_double        },
        [Pred::FCMP_OGT - F]   = {&Derived::encode_fcmp_ogt_float,
                                  &Derived::encode_fcmp_ogt_double        },
        [Pred::FCMP_OGE - F]   = {&Derived::encode_fcmp_oge_float,
                                  &Derived::encode_fcmp_oge_double        },
        [Pred::FCMP_OLT - F]   = {&Derived::encode_fcmp_olt_float,
                                  &Derived::encode_fcmp_olt_double        },
        [Pred::FCMP_OLE - F]   = {&Derived::encode_fcmp_ole_float,
                                  &Derived::encode_fcmp_ole_double        },
        [Pred::FCMP_ONE - F]   = {&Derived::encode_fcmp_one_float,
                                  &Derived::encode_fcmp_one_double        },
        [Pred::FCMP_ORD - F]   = {&Derived::encode_fcmp_ord_float,
                                  &Derived::encode_fcmp_ord_double        },
        [Pred::FCMP_UEQ - F]   = {&Derived::encode_fcmp_ueq_float,
                                  &Derived::encode_fcmp_ueq_double        },
        [Pred::FCMP_UGT - F]   = {&Derived::encode_fcmp_ugt_float,
                                  &Derived::encode_fcmp_ugt_double        },
        [Pred::FCMP_UGE - F]   = {&Derived::encode_fcmp_uge_float,
                                  &Derived::encode_fcmp_uge_double        },
        [Pred::FCMP_ULT - F]   = {&Derived::encode_fcmp_ult_float,
                                  &Derived::encode_fcmp_ult_double        },
        [Pred::FCMP_ULE - F]   = {&Derived::encode_fcmp_ule_float,
                                  &Derived::encode_fcmp_ule_double        },
        [Pred::FCMP_UNE - F]   = {&Derived::encode_fcmp_une_float,
                                  &Derived::encode_fcmp_une_double        },
        [Pred::FCMP_UNO - F]   = {&Derived::encode_fcmp_uno_float,
                                  &Derived::encode_fcmp_uno_double        },
    };
#pragma GCC diagnostic pop

    AsmOperand lhs_op = this->val_ref(llvm_val_idx(cmp->getOperand(0)), 0);
    AsmOperand rhs_op = this->val_ref(llvm_val_idx(cmp->getOperand(1)), 0);
    ScratchReg res_scratch{derived()};
    auto       res_ref = this->result_ref_lazy(inst_idx, 0);

    if (!(derived()
              ->*encode_fns[pred - Pred::FIRST_FCMP_PREDICATE]
                           [is_double ? 1 : 0])(
            std::move(lhs_op), std::move(rhs_op), res_scratch)) {
        return false;
    }

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_switch(
    IRValueRef, llvm::Instruction *inst) noexcept {
    using EncodeImm   = typename Derived::EncodeImm;
    auto *switch_inst = llvm::cast<llvm::SwitchInst>(inst);

    ScratchReg scratch{this};
    AsmReg     cmp_reg;
    bool       width_is_32 = false;
    {
        auto arg_ref =
            this->val_ref(llvm_val_idx(switch_inst->getCondition()), 0);
        auto *arg_ty = switch_inst->getCondition()->getType();
        assert(arg_ty->isIntegerTy() || arg_ty->isPointerTy());
        u32 width = 64;
        if (arg_ty->isIntegerTy()) {
            width = arg_ty->getIntegerBitWidth();
            assert(width <= 64);
        }

        if (width < 32) {
            width_is_32 = true;
            if (width == 8) {
                derived()->encode_zext_8_to_32(std::move(arg_ref), scratch);
            } else if (width == 16) {
                derived()->encode_zext_16_to_32(std::move(arg_ref), scratch);
            } else {
                u32 mask = (1ull << width) - 1;
                derived()->encode_landi32(
                    std::move(arg_ref), EncodeImm{mask}, scratch);
            }
            cmp_reg = scratch.cur_reg;
        } else if (width == 32) {
            width_is_32 = true;
            cmp_reg     = this->val_as_reg(arg_ref, scratch);
            // make sure we can overwrite the register when we generate a jump
            // table
            if (arg_ref.can_salvage()) {
                scratch.alloc_specific(arg_ref.salvage());
            } else if (!arg_ref.is_const) {
                arg_ref.unlock();
                arg_ref.reload_into_specific_fixed(this, scratch.alloc_gp());
                cmp_reg = scratch.cur_reg;
            }
        } else if (width < 64) {
            u64 mask = (1ull << width) - 1;
            derived()->encode_landi64(
                std::move(arg_ref), EncodeImm{mask}, scratch);
            cmp_reg = scratch.cur_reg;
        } else {
            cmp_reg = this->val_as_reg(arg_ref, scratch);
            // make sure we can overwrite the register when we generate a jump
            // table
            if (arg_ref.can_salvage()) {
                scratch.alloc_specific(arg_ref.salvage());
            } else if (!arg_ref.is_const) {
                arg_ref.reload_into_specific_fixed(this, scratch.alloc_gp());
                cmp_reg = scratch.cur_reg;
            }
        }
    }

    const auto spilled = this->spill_before_branch();

    // get all cases, their target block and sort them in ascending order
    tpde::util::SmallVector<std::pair<u64, IRBlockRef>, 64> cases;
    assert(switch_inst->getNumCases() <= 200000);
    cases.reserve(switch_inst->getNumCases());
    for (auto case_val : switch_inst->cases()) {
        cases.push_back(std::make_pair(
            case_val.getCaseValue()->getZExtValue(),
            this->adaptor->block_lookup[case_val.getCaseSuccessor()]));
    }
    std::sort(cases.begin(), cases.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });

    // because some blocks might have PHI-values we need to first jump to a
    // label which then fixes the registers and then jumps to the block
    // TODO(ts): check which blocks need PHIs and otherwise jump directly to
    // them? probably better for branch predictor

    tpde::util::SmallVector<typename Assembler::Label, 64> case_labels;
    for (auto i = 0u; i < cases.size(); ++i) {
        case_labels.push_back(this->assembler.label_create());
    }

    const auto default_label = this->assembler.label_create();

    const auto build_range = [&, this](
                                 size_t begin, size_t end, const auto &self) {
        assert(begin <= end);
        const auto num_cases = end - begin;
        if (num_cases <= 4) {
            // if there are four or less cases we just compare the values
            // against each of them
            for (auto i = 0u; i < num_cases; ++i) {
                derived()->switch_emit_cmpeq(case_labels[begin + i],
                                             cmp_reg,
                                             cases[begin + i].first,
                                             width_is_32);
            }

            derived()->generate_raw_jump(Derived::Jump::jmp, default_label);
            return;
        }

        // check if the density of the values is high enough to warrant building
        // a jump table
        auto range = cases[end - 1].first - cases[begin].first;
        // we will get wrong results if range is -1 so skip the jump table if
        // that is the case
        if (range != 0xFFFF'FFFF'FFFF'FFFF && (range / num_cases) < 8) {
            // for gcc, it seems that if there are less than 8 values per
            // case it will build a jump table so we do that, too

            // the actual range is one greater than the result we get from
            // subtracting so adjust for that
            range += 1;

            tpde::util::SmallVector<typename Assembler::Label, 32> label_vec;
            std::span<typename Assembler::Label>                   labels;
            if (range == num_cases) {
                labels = std::span{case_labels.begin() + begin, num_cases};
            } else {
                label_vec.resize(range, default_label);
                for (auto i = 0u; i < num_cases; ++i) {
                    label_vec[cases[begin + i].first - cases[begin].first] =
                        case_labels[begin + i];
                }
                labels = std::span{label_vec.begin(), range};
            }

            derived()->switch_emit_jump_table(default_label,
                                              labels,
                                              cmp_reg,
                                              cases[begin].first,
                                              cases[end - 1].first,
                                              width_is_32);
            return;
        }

        // do a binary search step
        const auto half_len   = num_cases / 2;
        const auto half_value = cases[begin + half_len].first;
        const auto gt_label   = this->assembler.label_create();

        // this will cmp against the input value, jump to the case if it is
        // equal or to gt_label if the value is greater. Otherwise it will
        // fall-through
        derived()->switch_emit_binary_step(case_labels[begin + half_len],
                                           gt_label,
                                           cmp_reg,
                                           half_value,
                                           width_is_32);
        // search the lower half
        self(begin, begin + half_len, self);

        // and the upper half
        this->assembler.label_place(gt_label);
        self(begin + half_len + 1, end, self);
    };

    build_range(0, case_labels.size(), build_range);

    // write out the labels
    this->assembler.label_place(default_label);
    // TODO(ts): factor into arch-code?
    derived()->generate_branch_to_block(
        Derived::Jump::jmp,
        this->adaptor->block_lookup[switch_inst->getDefaultDest()],
        false,
        false);

    for (auto i = 0u; i < cases.size(); ++i) {
        this->assembler.label_place(case_labels[i]);
        derived()->generate_branch_to_block(
            Derived::Jump::jmp, cases[i].second, false, false);
    }

    this->release_spilled_regs(spilled);

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_invoke(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    auto *invoke = llvm::cast<llvm::InvokeInst>(inst);

    // we need to spill here since the call might branch off
    // TODO: this will also spill the call arguments even if the call kills them
    // however, spillBeforeCall already does this anyways so probably something
    // for later
    auto spilled = this->spill_before_branch();

    const auto off_before_call = this->assembler.text_cur_off();
    // compile the call
    // TODO: in the case of an exception we need to invalidate the result
    // registers
    this->compile_call(inst_idx, inst);
    const auto off_after_call = this->assembler.text_cur_off();

    // build the eh table
    auto                 *unwind_block         = invoke->getUnwindDest();
    llvm::LandingPadInst *landing_pad          = nullptr;
    auto                  unwind_block_has_phi = false;

    for (auto it = unwind_block->begin(), end = unwind_block->end(); it != end;
         ++it) {
        llvm::Instruction *inst = &*it;
        if (llvm::isa<llvm::PHINode>(inst)) {
            unwind_block_has_phi = true;
            continue;
        }

        landing_pad = llvm::dyn_cast<llvm::LandingPadInst>(inst);
        assert(landing_pad != nullptr);
        break;
    }

    const auto unwind_block_ref = this->adaptor->block_lookup[unwind_block];
    const auto normal_block_ref =
        this->adaptor->block_lookup[invoke->getNormalDest()];
    auto unwind_label =
        this->block_labels[(u32)this->analyzer.block_idx(unwind_block_ref)];

    // we need to check whether the call result needs to be spilled, too.
    // This needs to be done since the invoke is conceptually a branch
    auto check_res_spill = [&]() {
        auto  call_res_idx = llvm_val_idx(invoke);
        auto *a            = this->val_assignment(this->val_idx(call_res_idx));
        if (a == nullptr) {
            // call has void result
            return;
        }

        auto cur_block = this->analyzer.block_ref(this->cur_block_idx);

        if (this->analyzer.block_ref(this->next_block()) == normal_block_ref) {
            // this block will handle the spilling for us, the value is not
            // usable in unwind block
            return;
        }

        uint32_t num_phi_reads = 0;
        for (auto i : this->adaptor->block_phis(normal_block_ref)) {
            assert(this->adaptor->val_is_phi(i));
            auto phi = this->adaptor->val_as_phi(i);

            auto incoming_val = phi.incoming_val_for_block(cur_block);
            if (incoming_val == call_res_idx) {
                ++num_phi_reads;
            }
        }

        auto ap = AssignmentPartRef{a, 0};
        // no need to spill if only the phi-nodes in the (normal) successor read
        // the value
        if (a->references_left <= num_phi_reads
            && (u32)this->analyzer.liveness_info(call_res_idx).last
                   <= (u32)this->cur_block_idx) {
            return;
        }

        // spill
        // no need to spill fixed assignments
        while (true) {
            if (!ap.fixed_assignment() && ap.register_valid()) {
                // this is the call result...
                assert(ap.modified());
                ap.spill_if_needed(this);
                ap.set_modified(false);

                spilled |= 1ull << ap.full_reg_id();
            }
            if (!ap.has_next_part()) {
                break;
            }
            ++ap.part;
        }
    };
    check_res_spill();

    // if the unwind block has phi-nodes, we need more code to propagate values
    // to it so do the propagation logic
    if (unwind_block_has_phi) {
        // generate the jump to the normal successor but don't allow
        // fall-through
        derived()->generate_branch_to_block(Derived::Jump::jmp,
                                            normal_block_ref,
                                            /* split */ false,
                                            /* last_inst */ false);

        unwind_label = this->assembler.label_create();
        this->assembler.label_place(unwind_label);

        // allocate the special registers that are set by the unwinding logic
        // so the phi-propagation does not use them as temporaries
        ScratchReg scratch1{derived()}, scratch2{derived()};
        assert(!this->register_file.is_used(Derived::LANDING_PAD_RES_REGS[0]));
        assert(!this->register_file.is_used(Derived::LANDING_PAD_RES_REGS[1]));
        scratch1.alloc_specific(Derived::LANDING_PAD_RES_REGS[0]);
        scratch2.alloc_specific(Derived::LANDING_PAD_RES_REGS[1]);

        derived()->generate_branch_to_block(Derived::Jump::jmp,
                                            unwind_block_ref,
                                            /* split */ false,
                                            /* last_inst */ false);
    } else {
        // allow fall-through
        derived()->generate_branch_to_block(Derived::Jump::jmp,
                                            normal_block_ref,
                                            /* split */ false,
                                            /* last_inst */ true);
    }

    this->release_spilled_regs(spilled);

    const auto is_cleanup   = landing_pad->isCleanup();
    const auto num_clauses  = landing_pad->getNumClauses();
    const auto only_cleanup = is_cleanup && num_clauses == 0;

    this->assembler.except_add_call_site(off_before_call,
                                         off_after_call - off_before_call,
                                         static_cast<u32>(unwind_label),
                                         only_cleanup);

    if (only_cleanup) {
        // no clause so we are done
        return true;
    }

    for (auto i = 0u; i < num_clauses; ++i) {
        if (landing_pad->isCatch(i)) {
            auto  *C   = landing_pad->getClause(i);
            SymRef sym = Assembler::INVALID_SYM_REF;
            if (!C->isNullValue()) {
                assert(llvm::dyn_cast<llvm::GlobalValue>(C));
                sym = lookup_type_info_sym(llvm_val_idx(C));
            }
            this->assembler.except_add_action(i == 0, sym);
        } else {
            assert(landing_pad->isFilter(i));
            auto *C = landing_pad->getClause(i);
            assert(C->getType()->isArrayTy());
            if (C->getType()->getArrayNumElements() == 0) {
                this->assembler.except_add_empty_spec_action(i == 0);
            } else {
                llvm::errs() << "Exception filters with non-zero length arrays "
                                "not supported\n";
                assert(0); // not supported rn
                return false;
            }
        }
    }

    if (is_cleanup) {
        assert(num_clauses != 0);
        this->assembler.except_add_cleanup_action();
    }

    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_landing_pad(
    IRValueRef inst_idx, llvm::Instruction *) noexcept {
    auto res_ref_first  = this->result_ref_lazy(inst_idx, 0);
    auto res_ref_second = this->result_ref_lazy(inst_idx, 1);

    this->set_value(res_ref_first, Derived::LANDING_PAD_RES_REGS[0]);
    this->set_value(res_ref_second, Derived::LANDING_PAD_RES_REGS[1]);

    res_ref_second.reset_without_refcount();
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_resume(
    IRValueRef, llvm::Instruction *inst) noexcept {
    IRValueRef arg = llvm_val_idx(inst->getOperand(0));

    const auto sym = get_or_create_sym_ref(sym_resume, "_Unwind_Resume");

    derived()->create_helper_call({&arg, 1}, {}, sym);
    return derived()->compile_unreachable(Adaptor::INVALID_VALUE_REF, nullptr);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::lookup_type_info_sym(
        IRValueRef value) noexcept {
    for (const auto &[val, sym] : type_info_syms) {
        if (val == value) {
            return sym;
        }
    }

    const auto sym = global_sym(
        llvm::dyn_cast<llvm::GlobalValue>(this->adaptor->values[value].val));

    u32        off;
    u8         tmp[8]   = {};
    const auto addr_sym = this->assembler.sym_def_data(
        {}, {tmp, sizeof(tmp)}, 8, true, true, true, false, &off);
    this->assembler.reloc_data_abs(sym, true, off, 0);

    type_info_syms.emplace_back(value, addr_sym);
    return addr_sym;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_intrin(
    IRValueRef         inst_idx,
    llvm::Instruction *inst,
    llvm::Function    *intrin) noexcept {
    using AsmOperand     = typename Derived::AsmOperand;
    using EncodeImm      = typename Derived::EncodeImm;
    const auto intrin_id = intrin->getIntrinsicID();

    switch (intrin_id) {
    case llvm::Intrinsic::memcpy: {
        const auto dst = llvm_val_idx(inst->getOperand(0));
        const auto src = llvm_val_idx(inst->getOperand(1));
        const auto len = llvm_val_idx(inst->getOperand(2));

        std::array<IRValueRef, 3> args{dst, src, len};

        const auto sym = get_or_create_sym_ref(sym_memcpy, "memcpy");
        derived()->create_helper_call(args, {}, sym);
        return true;
    }
    case llvm::Intrinsic::memset: {
        const auto dst = llvm_val_idx(inst->getOperand(0));
        const auto val = llvm_val_idx(inst->getOperand(1));
        const auto len = llvm_val_idx(inst->getOperand(2));

        std::array<IRValueRef, 3> args{dst, val, len};

        const auto sym = get_or_create_sym_ref(sym_memset, "memset");
        derived()->create_helper_call(args, {}, sym);
        return true;
    }
    case llvm::Intrinsic::memmove: {
        const auto dst = llvm_val_idx(inst->getOperand(0));
        const auto src = llvm_val_idx(inst->getOperand(1));
        const auto len = llvm_val_idx(inst->getOperand(2));

        std::array<IRValueRef, 3> args{dst, src, len};

        const auto sym = get_or_create_sym_ref(sym_memmove, "memmove");
        derived()->create_helper_call(args, {}, sym);
        return true;
    }
    case llvm::Intrinsic::vaend: {
        // no-op
        this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
        return true;
    }
    case llvm::Intrinsic::is_fpclass: {
        return compile_is_fpclass(inst_idx, inst);
    }
    case llvm::Intrinsic::floor:
    case llvm::Intrinsic::ceil:
    case llvm::Intrinsic::round:
    case llvm::Intrinsic::trunc: {
        auto val = llvm_val_idx(inst->getOperand(0));

        const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();
        auto       res_ref   = this->result_ref_lazy(inst_idx, 0);
        SymRef     sym;
        if (intrin_id == llvm::Intrinsic::floor) {
            if (is_double) {
                sym = get_or_create_sym_ref(sym_floor, "floor");
            } else {
                sym = get_or_create_sym_ref(sym_floorf, "floorf");
            }
        } else if (intrin_id == llvm::Intrinsic::ceil) {
            if (is_double) {
                sym = get_or_create_sym_ref(sym_ceil, "ceil");
            } else {
                sym = get_or_create_sym_ref(sym_ceilf, "ceilf");
            }
        } else if (intrin_id == llvm::Intrinsic::round) {
            if (is_double) {
                sym = get_or_create_sym_ref(sym_round, "round");
            } else {
                sym = get_or_create_sym_ref(sym_roundf, "roundf");
            }
        } else {
            assert(intrin_id == llvm::Intrinsic::trunc);
            if (is_double) {
                sym = get_or_create_sym_ref(sym_trunc, "trunc");
            } else {
                sym = get_or_create_sym_ref(sym_truncf, "truncf");
            }
        }

        derived()->create_helper_call({&val, 1}, {&res_ref, 1}, sym);
        return true;
    }
    case llvm::Intrinsic::fabs: {
        auto *val = inst->getOperand(0);
        auto *ty  = val->getType();

        if (!ty->isFloatTy() && !ty->isDoubleTy()) {
            assert(0);
            TPDE_LOG_ERR("only float/double supported for fabs");
            return false;
        }

        auto       val_ref = this->val_ref(llvm_val_idx(val), 0);
        auto       res_ref = this->result_ref_lazy(inst_idx, 0);
        ScratchReg res_scratch{derived()};

        if (ty->isDoubleTy()) {
            derived()->encode_fabsf64(std::move(val_ref), res_scratch);
        } else {
            derived()->encode_fabsf32(std::move(val_ref), res_scratch);
        }
        this->set_value(res_ref, res_scratch);
        return true;
    }
    case llvm::Intrinsic::fmuladd: {
        auto op1_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
        auto op2_ref = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
        auto op3_ref = this->val_ref(llvm_val_idx(inst->getOperand(2)), 0);

        const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();

        auto       res_ref = this->result_ref_lazy(inst_idx, 0);
        ScratchReg res_scratch{derived()};
        if (is_double) {
            derived()->encode_fmaf64(std::move(op1_ref),
                                     std::move(op2_ref),
                                     std::move(op3_ref),
                                     res_scratch);
        } else {
            derived()->encode_fmaf32(std::move(op1_ref),
                                     std::move(op2_ref),
                                     std::move(op3_ref),
                                     res_scratch);
        }
        this->set_value(res_ref, res_scratch);
        return true;
    }
    case llvm::Intrinsic::powi: {
        std::array<IRValueRef, 2> args = {
            {llvm_val_idx(inst->getOperand(0)),
             llvm_val_idx(inst->getOperand(1))}
        };

        auto *ty = inst->getOperand(0)->getType();
        if (!ty->isDoubleTy() && !ty->isFloatTy()) {
            assert(0);
            return false;
        }
        const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();
        SymRef     sym;
        if (is_double) {
            sym = get_or_create_sym_ref(sym_powidf2, "__powidf2");
        } else {
            sym = get_or_create_sym_ref(sym_powisf2, "__powisf2");
        }
        auto res_ref = this->result_ref_lazy(inst_idx, 0);

        derived()->create_helper_call(args, {&res_ref, 1}, sym);
        return true;
    }
    case llvm::Intrinsic::abs: {
        auto *val    = inst->getOperand(0);
        auto *val_ty = val->getType();
        if (!val_ty->isIntegerTy()) {
            assert(0);
            return false;
        }
        const auto width = val_ty->getIntegerBitWidth();
        if (width > 64) {
            assert(0);
            return false;
        }

        auto op = AsmOperand{this->val_ref(llvm_val_idx(val), 0)};
        if (width < 32) {
            ScratchReg res{derived()};
            if (width == 8) {
                derived()->encode_sext_8_to_32(std::move(op), res);
            } else if (width == 16) {
                derived()->encode_sext_16_to_32(std::move(op), res);
            } else {
                const u32 shift = 32 - width;
                derived()->encode_sext_arbitrary_to_32(
                    std::move(op), EncodeImm{shift}, res);
            }
            op = std::move(res);
        } else if (width != 32 && width < 64) {
            ScratchReg res{derived()};
            const u32  shift = 64 - width;
            derived()->encode_sext_arbitrary_to_64(
                std::move(op), EncodeImm{shift}, res);
            op = std::move(res);
        }

        ScratchReg res{derived()};
        auto       res_ref = this->result_ref_lazy(inst_idx, 0);
        if (width <= 32) {
            derived()->encode_absi32(std::move(op), res);
        } else {
            derived()->encode_absi64(std::move(op), res);
        }
        this->set_value(res_ref, res);
        return true;
    }
    case llvm::Intrinsic::uadd_with_overflow:
        return compile_overflow_intrin(inst_idx, inst, OverflowOp::uadd);
    case llvm::Intrinsic::sadd_with_overflow:
        return compile_overflow_intrin(inst_idx, inst, OverflowOp::sadd);
    case llvm::Intrinsic::usub_with_overflow:
        return compile_overflow_intrin(inst_idx, inst, OverflowOp::usub);
    case llvm::Intrinsic::ssub_with_overflow:
        return compile_overflow_intrin(inst_idx, inst, OverflowOp::ssub);
    case llvm::Intrinsic::umul_with_overflow:
        return compile_overflow_intrin(inst_idx, inst, OverflowOp::umul);
    case llvm::Intrinsic::smul_with_overflow:
        return compile_overflow_intrin(inst_idx, inst, OverflowOp::smul);
    case llvm::Intrinsic::ctlz:
    case llvm::Intrinsic::cttz: {
        auto *val = inst->getOperand(0);
        assert(val->getType()->isIntegerTy());
        const auto width = val->getType()->getIntegerBitWidth();
        if (width != 8 && width != 16 && width != 32 && width != 64) {
            assert(0);
            return false;
        }

        const auto zero_is_poison =
            !llvm::cast<llvm::ConstantInt>(inst->getOperand(1))->isZero();

        auto       val_ref = this->val_ref(llvm_val_idx(val), 0);
        auto       res_ref = this->result_ref_lazy(inst_idx, 0);
        ScratchReg res{derived()};

        if (intrin_id == llvm::Intrinsic::ctlz) {
            switch (width) {
            case 8:
                if (zero_is_poison) {
                    derived()->encode_ctlzi8_zero_poison(std::move(val_ref),
                                                         res);
                } else {
                    derived()->encode_ctlzi8(std::move(val_ref), res);
                }
                break;
            case 16:
                if (zero_is_poison) {
                    derived()->encode_ctlzi16_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_ctlzi16(std::move(val_ref), res);
                }
                break;
            case 32:
                if (zero_is_poison) {
                    derived()->encode_ctlzi32_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_ctlzi32(std::move(val_ref), res);
                }
                break;
            case 64:
                if (zero_is_poison) {
                    derived()->encode_ctlzi64_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_ctlzi64(std::move(val_ref), res);
                }
                break;
            default: __builtin_unreachable();
            }
        } else {
            assert(intrin_id == llvm::Intrinsic::cttz);
            switch (width) {
            case 8:
                if (zero_is_poison) {
                    derived()->encode_cttzi32_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_cttzi8(std::move(val_ref), res);
                }
                break;
            case 16:
                if (zero_is_poison) {
                    derived()->encode_cttzi32_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_cttzi16(std::move(val_ref), res);
                }
                break;
            case 32:
                if (zero_is_poison) {
                    derived()->encode_cttzi32_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_cttzi32(std::move(val_ref), res);
                }
                break;
            case 64:
                if (zero_is_poison) {
                    derived()->encode_cttzi64_zero_poison(std::move(val_ref),
                                                          res);
                } else {
                    derived()->encode_cttzi64(std::move(val_ref), res);
                }
                break;
            default: __builtin_unreachable();
            }
        }

        this->set_value(res_ref, res);
        return true;
    }
    case llvm::Intrinsic::prefetch: {
        auto ptr_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);

        const auto rw =
            llvm::cast<llvm::ConstantInt>(inst->getOperand(1))->getZExtValue();
        const auto locality =
            llvm::cast<llvm::ConstantInt>(inst->getOperand(2))->getZExtValue();
        // for now, ignore instruction/data distinction

        if (rw == 0) {
            // read
            switch (locality) {
            case 0: derived()->encode_prefetch_rl0(std::move(ptr_ref)); break;
            case 1: derived()->encode_prefetch_rl1(std::move(ptr_ref)); break;
            case 2: derived()->encode_prefetch_rl2(std::move(ptr_ref)); break;
            default: assert(0);
            case 3: derived()->encode_prefetch_rl3(std::move(ptr_ref)); break;
            }
        } else {
            assert(rw == 1);
            // write
            switch (locality) {
            case 0: derived()->encode_prefetch_wl0(std::move(ptr_ref)); break;
            case 1: derived()->encode_prefetch_wl1(std::move(ptr_ref)); break;
            case 2: derived()->encode_prefetch_wl2(std::move(ptr_ref)); break;
            default: assert(0);
            case 3: derived()->encode_prefetch_wl3(std::move(ptr_ref)); break;
            }
        }
        return true;
    }
    case llvm::Intrinsic::eh_typeid_for: {
        auto *type = inst->getOperand(0);
        assert(llvm::isa<llvm::GlobalValue>(type));

        // not the most efficient but it's OK
        const auto type_info_sym = lookup_type_info_sym(llvm_val_idx(type));
        const auto idx = this->assembler.except_type_idx_for_sym(type_info_sym);

        auto res_ref   = this->result_ref_eager(inst_idx, 0);
        auto const_ref = ValuePartRef{idx, Config::GP_BANK, 4};
        derived()->materialize_constant(const_ref, res_ref.cur_reg());
        this->set_value(res_ref, res_ref.cur_reg());
        return true;
    }
    case llvm::Intrinsic::is_constant: {
        // > On the other hand, if constant folding is not run, it will never
        // evaluate to true, even in simple cases. example in
        // 641.leela_s:UCTNode.cpp

        // ref-count the argument
        this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
        auto res_ref   = this->result_ref_eager(inst_idx, 0);
        auto const_ref = ValuePartRef{0, Config::GP_BANK, 4};
        derived()->materialize_constant(const_ref, res_ref.cur_reg());
        this->set_value(res_ref, res_ref.cur_reg());
        return true;
    }
    default: {
        if (derived()->handle_intrin(inst_idx, inst, intrin)) {
            return true;
        }

        std::string buf{};
        auto        os = llvm::raw_string_ostream{buf};
        intrin->print(os);
        llvm::errs() << "Unknown intrinsic:\n" << buf << '\n';
        assert(0);
        return false;
    }
    }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_is_fpclass(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
    using EncodeImm = typename Derived::EncodeImm;

    auto *op    = inst->getOperand(0);
    auto *op_ty = op->getType();

    if (!op_ty->isFloatTy() && !op_ty->isDoubleTy()) {
        TPDE_LOG_ERR("is_fpclass only supports float and doubles");
        return false;
    }
    const auto is_double = op_ty->isDoubleTy();
    const auto test =
        llvm::dyn_cast<llvm::ConstantInt>(inst->getOperand(1))->getZExtValue();

    enum {
        SIGNALING_NAN = 1 << 0,
        QUIET_NAN     = 1 << 1,
        NEG_INF       = 1 << 2,
        NEG_NORM      = 1 << 3,
        NEG_SUBNORM   = 1 << 4,
        NEG_ZERO      = 1 << 5,
        POS_ZERO      = 1 << 6,
        POS_SUBNORM   = 1 << 7,
        POS_NORM      = 1 << 8,
        POS_INF       = 1 << 9,

        IS_NAN    = SIGNALING_NAN | QUIET_NAN,
        IS_INF    = NEG_INF | POS_INF,
        IS_NORM   = NEG_NORM | POS_NORM,
        IS_FINITE = NEG_NORM | NEG_SUBNORM | NEG_ZERO | POS_ZERO | POS_SUBNORM
                    | POS_NORM,
    };

    ScratchReg res_scratch{derived()};
    auto       res_ref = this->result_ref_lazy(inst_idx, 0);
    auto       op_ref  = this->val_ref(llvm_val_idx(op), 0);

    // handle common case
#define TEST(cond, name)                                                       \
    if (test == cond) {                                                        \
        if (is_double) {                                                       \
            derived()->encode_is_fpclass_##name##_double(                      \
                EncodeImm{0u}, std::move(op_ref), res_scratch);                \
        } else {                                                               \
            derived()->encode_is_fpclass_##name##_float(                       \
                EncodeImm{0u}, std::move(op_ref), res_scratch);                \
        }                                                                      \
        this->set_value(res_ref, res_scratch);                                 \
        return true;                                                           \
    }

    TEST(IS_NAN, nan)
    TEST(IS_INF, inf)
    TEST(IS_NORM, norm)
#undef TEST

    // we OR' together the results from each test so initialize the result with
    // zero
    auto const_ref = ValuePartRef{0, Config::GP_BANK, 4};
    derived()->materialize_constant(const_ref, res_scratch.alloc_gp());

#define TEST(cond, name)                                                       \
    if (test & cond) {                                                         \
        if (is_double) {                                                       \
            /* note that the std::move(res_scratch) here creates a new         \
             * ScratchReg that manages the register inside the AsmOperand and  \
             * res_scratch becomes invalid by the time the encode function is  \
             * entered */                                                      \
            derived()->encode_is_fpclass_##name##_double(                      \
                std::move(res_scratch), op_ref, res_scratch);                  \
        } else {                                                               \
            derived()->encode_is_fpclass_##name##_float(                       \
                std::move(res_scratch), op_ref, res_scratch);                  \
        }                                                                      \
    }
    TEST(SIGNALING_NAN, snan)
    TEST(QUIET_NAN, qnan)
    TEST(NEG_INF, ninf)
    TEST(NEG_NORM, nnorm)
    TEST(NEG_SUBNORM, nsnorm)
    TEST(NEG_ZERO, nzero)
    TEST(POS_ZERO, pzero)
    TEST(POS_SUBNORM, psnorm)
    TEST(POS_NORM, pnorm)
    TEST(POS_INF, pinf)
#undef TEST

    this->set_value(res_ref, res_scratch);
    return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_overflow_intrin(
    IRValueRef inst_idx, llvm::Instruction *inst, OverflowOp op) noexcept {
    using AsmOperand = typename Derived::AsmOperand;

    auto *llvm_lhs = inst->getOperand(0);
    auto *llvm_rhs = inst->getOperand(1);

    auto *ty = llvm_lhs->getType();
    assert(ty->isIntegerTy());
    const auto width = ty->getIntegerBitWidth();

    if (width == 128) {
        using EncodeFnTy     = bool (Derived::*)(AsmOperand,
                                             AsmOperand,
                                             AsmOperand,
                                             AsmOperand,
                                             ScratchReg &,
                                             ScratchReg &,
                                             ScratchReg &);
        EncodeFnTy encode_fn = nullptr;
        switch (op) {
        case OverflowOp::uadd: encode_fn = &Derived::encode_of_add_u128; break;
        case OverflowOp::sadd: encode_fn = &Derived::encode_of_add_i128; break;
        case OverflowOp::usub: encode_fn = &Derived::encode_of_sub_u128; break;
        case OverflowOp::ssub: encode_fn = &Derived::encode_of_sub_i128; break;
        case OverflowOp::umul: encode_fn = &Derived::encode_of_mul_u128; break;
        case OverflowOp::smul: encode_fn = &Derived::encode_of_mul_i128; break;
        default: __builtin_unreachable();
        }

        auto lhs_ref = this->val_ref(llvm_val_idx(llvm_lhs), 0);
        auto rhs_ref = this->val_ref(llvm_val_idx(llvm_rhs), 0);
        lhs_ref.inc_ref_count();
        rhs_ref.inc_ref_count();
        AsmOperand lhs_op_high = this->val_ref(llvm_val_idx(llvm_lhs), 1);
        AsmOperand rhs_op_high = this->val_ref(llvm_val_idx(llvm_rhs), 1);
        ScratchReg res_val{derived()}, res_val_high{derived()},
            res_of{derived()};

        if (!(derived()->*encode_fn)(std::move(lhs_ref),
                                     std::move(lhs_op_high),
                                     std::move(rhs_ref),
                                     std::move(rhs_op_high),
                                     res_val,
                                     res_val_high,
                                     res_of)) {
            return false;
        }

        auto res_ref_val  = this->result_ref_lazy(inst_idx, 0);
        auto res_ref_high = this->result_ref_lazy(inst_idx, 1);
        auto res_ref_of   = this->result_ref_lazy(inst_idx, 2);
        this->set_value(res_ref_val, res_val);
        this->set_value(res_ref_high, res_val_high);
        this->set_value(res_ref_of, res_of);

        res_ref_high.reset_without_refcount();
        res_ref_of.reset_without_refcount();
        return true;
    }

    u32 width_idx = 0;
    switch (width) {
    case 8: width_idx = 0; break;
    case 16: width_idx = 1; break;
    case 32: width_idx = 2; break;
    case 64: width_idx = 3; break;
    default:
        assert(0);
        TPDE_LOG_ERR("overflow op with width {} not supported", width);
        return false;
    }

    using EncodeFnTy =
        bool (Derived::*)(AsmOperand, AsmOperand, ScratchReg &, ScratchReg &);
    std::array<std::array<EncodeFnTy, 4>, 6> encode_fns = {
        {
         {&Derived::encode_of_add_u8,
             &Derived::encode_of_add_u16,
             &Derived::encode_of_add_u32,
             &Derived::encode_of_add_u64},
         {&Derived::encode_of_add_i8,
             &Derived::encode_of_add_i16,
             &Derived::encode_of_add_i32,
             &Derived::encode_of_add_i64},
         {&Derived::encode_of_sub_u8,
             &Derived::encode_of_sub_u16,
             &Derived::encode_of_sub_u32,
             &Derived::encode_of_sub_u64},
         {&Derived::encode_of_sub_i8,
             &Derived::encode_of_sub_i16,
             &Derived::encode_of_sub_i32,
             &Derived::encode_of_sub_i64},
         {&Derived::encode_of_mul_u8,
             &Derived::encode_of_mul_u16,
             &Derived::encode_of_mul_u32,
             &Derived::encode_of_mul_u64},
         {&Derived::encode_of_mul_i8,
             &Derived::encode_of_mul_i16,
             &Derived::encode_of_mul_i32,
             &Derived::encode_of_mul_i64},
         }
    };

    EncodeFnTy encode_fn = encode_fns[static_cast<u32>(op)][width_idx];

    AsmOperand lhs_op = this->val_ref(llvm_val_idx(llvm_lhs), 0);
    AsmOperand rhs_op = this->val_ref(llvm_val_idx(llvm_rhs), 0);
    ScratchReg res_val{derived()}, res_of{derived()};

    if (!(derived()->*encode_fn)(
            std::move(lhs_op), std::move(rhs_op), res_val, res_of)) {
        return false;
    }

    auto res_ref_val = this->result_ref_lazy(inst_idx, 0);
    auto res_ref_of  = this->result_ref_lazy(inst_idx, 1);

    this->set_value(res_ref_val, res_val);
    this->set_value(res_ref_of, res_of);
    res_ref_of.reset_without_refcount();
    return true;
}
} // namespace tpde_llvm
