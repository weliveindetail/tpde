// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <fstream>

#include "LLVMAdaptor.hpp"
#include "LLVMCompilerBase.hpp"
#include "encode_compiler.hpp"
#include "tpde/x64/CompilerX64.hpp"

namespace tpde_llvm::x64 {

struct LLVMCompilerX64
    : tpde::x64::CompilerX64<LLVMAdaptor, LLVMCompilerX64, LLVMCompilerBase>,
      tpde_encodegen::
          EncodeCompiler<LLVMAdaptor, LLVMCompilerX64, LLVMCompilerBase> {
    using Base =
        tpde::x64::CompilerX64<LLVMAdaptor, LLVMCompilerX64, LLVMCompilerBase>;
    using EncCompiler =
        EncodeCompiler<LLVMAdaptor, LLVMCompilerX64, LLVMCompilerBase>;

    struct EncodeImm : EncCompiler::AsmOperand::Immediate {
        explicit EncodeImm(const u32 value)
            : Immediate{.const_u64 = value, .bank = 0, .size = 4} {}

        explicit EncodeImm(const u64 value)
            : Immediate{.const_u64 = value, .bank = 0, .size = 8} {}

        explicit EncodeImm(const float value)
            : Immediate{.const_u64 = std::bit_cast<u32>(value),
                        .bank      = 1,
                        .size      = 4} {}

        explicit EncodeImm(const double value)
            : Immediate{.const_u64 = std::bit_cast<u64>(value),
                        .bank      = 1,
                        .size      = 8} {}
    };

    std::unique_ptr<LLVMAdaptor> adaptor;

    explicit LLVMCompilerX64(std::unique_ptr<LLVMAdaptor> &&adaptor)
        : Base{adaptor.get()}, adaptor(std::move(adaptor)) {
        static_assert(
            tpde::Compiler<LLVMCompilerX64, tpde::x64::PlatformConfig>);
    }

    [[nodiscard]] static tpde::x64::CallingConv
        cur_calling_convention() noexcept {
        return tpde::x64::CallingConv::SYSV_CC;
    }

    bool arg_is_int128(const IRValueRef val_idx) const noexcept {
        return this->adaptor->values[val_idx].type == LLVMBasicValType::i128;
    }

    static u32 basic_ty_part_count(LLVMBasicValType ty) noexcept;
    static u32 basic_ty_part_size(LLVMBasicValType ty) noexcept;

    u32 calculate_complex_real_part_count(IRValueRef) const noexcept;

    u32 val_part_count(IRValueRef) const noexcept;

    u32 val_part_size(IRValueRef, u32) const noexcept;

    u8 val_part_bank(IRValueRef, u32) const noexcept;

    void move_val_to_ret_regs(llvm::Value *) noexcept;

    void create_frem_calls(IRValueRef     lhs,
                           IRValueRef     rhs,
                           ValuePartRef &&res,
                           bool           is_double) noexcept;
};

u32 LLVMCompilerX64::basic_ty_part_count(const LLVMBasicValType ty) noexcept {
    switch (ty) {
        using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64:
    case ptr:
    case f32:
    case f64:
    case v32:
    case v64:
    case v128:
    case v256:
    case v512: return 1;
    case i128: return 2;
    case complex:
    case none:
    case invalid: {
        TPDE_LOG_ERR("basic_ty_part_count for value with none/invalid type");
        assert(0);
        __builtin_unreachable();
    }
    }
}

u32 LLVMCompilerX64::basic_ty_part_size(const LLVMBasicValType ty) noexcept {
    switch (ty) {
        using enum LLVMBasicValType;
    case i1:
    case i8: return 1;
    case i16: return 2;
    case i32: return 4;
    case i64:
    case ptr:
    case i128: return 8;
    case f32: return 4;
    case f64: return 8;
    case v32: return 4;
    case v64: return 8;
    case v128: return 16;
    case v256: return 32;
    case v512: return 64;
    case complex:
    case invalid:
    case none: {
        assert(0);
        __builtin_unreachable();
    }
    }
}

u32 LLVMCompilerX64::calculate_complex_real_part_count(
    const IRValueRef val_idx) const noexcept {
    auto *val    = this->adaptor->values[val_idx].val;
    auto *val_ty = val->getType();
    assert(val_ty->isAggregateType());

    const auto llvm_part_count = val_ty->getNumContainedTypes();
    const auto ty_idx = this->adaptor->values[val_idx].complex_part_tys_idx;
    u32        real_part_count = 0;
    for (u32 i = 0; i < llvm_part_count; ++i) {
        // skip over duplicate entries when LLVM part has multiple TPDE parts
        real_part_count += basic_ty_part_count(
            this->adaptor->complex_part_types[ty_idx + real_part_count]);
    }

    this->adaptor->complex_real_part_count(val_idx) = real_part_count;
    return real_part_count;
}

u32 LLVMCompilerX64::val_part_count(const IRValueRef val_idx) const noexcept {
    switch (this->adaptor->values[val_idx].type) {
        using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64:
    case ptr:
    case f32:
    case f64:
    case v32:
    case v64:
    case v128:
    case v256:
    case v512: return 1;
    case i128: return 2;
    case complex: {
        const auto real_part_count = adaptor->complex_real_part_count(val_idx);
        if (real_part_count != 0xFFFF'FFFF) {
            return real_part_count;
        }
        return calculate_complex_real_part_count(val_idx);
    }
    case none:
    case invalid: {
        TPDE_LOG_ERR("val_part_count for value with none/invalid type");
        assert(0);
        exit(1);
    }
    }
}

u32 LLVMCompilerX64::val_part_size(const IRValueRef val_idx,
                                   const u32        part_idx) const noexcept {
    if (const auto ty = this->adaptor->values[val_idx].type;
        ty != LLVMBasicValType::complex) {
        return basic_ty_part_size(ty);
    }

    return basic_ty_part_size(
        this->adaptor->complex_part_types
            [this->adaptor->values[val_idx].complex_part_tys_idx + part_idx]);
}

u8 LLVMCompilerX64::val_part_bank(const IRValueRef val_idx,
                                  const u32        part_idx) const noexcept {
    const auto bank_simple = [](const LLVMBasicValType ty) {
        switch (ty) {
            using enum LLVMBasicValType;
        case i1:
        case i8:
        case i16:
        case i32:
        case i64:
        case i128:
        case ptr: return 0;
        case f32:
        case f64:
        case v32:
        case v64:
        case v128:
        case v256:
        case v512: return 1;
        case none:
        case invalid:
        case complex: {
            assert(0);
            exit(1);
        }
        }
    };

    if (const auto ty = this->adaptor->values[val_idx].type;
        ty != LLVMBasicValType::complex) {
        return bank_simple(ty);
    }

    return bank_simple(
        this->adaptor->complex_part_types
            [this->adaptor->values[val_idx].complex_part_tys_idx + part_idx]);
}

void LLVMCompilerX64::move_val_to_ret_regs(llvm::Value *val) noexcept {
    const auto val_idx = llvm_val_idx(val);

    const auto &val_info = this->adaptor->values[val_idx];
    switch (val_info.type) {
        using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64:
    case ptr: {
        assert(val->getType()->isIntegerTy() || val->getType()->isPointerTy());
        auto       val_ref    = this->val_ref(val_idx, 0);
        const auto call_conv  = this->cur_calling_convention();
        const auto target_reg = call_conv.ret_regs_gp()[0];

        if (val_ref.is_const) {
            this->materialize_constant(val_ref, target_reg);
        } else {
            if (val_ref.assignment().fixed_assignment()) {
                val_ref.reload_into_specific(this, target_reg);
            } else {
                val_ref.move_into_specific(target_reg);
            }
        }

        if (val_info.type == LLVMBasicValType::ptr) {
            break;
        }

        auto *fn = this->adaptor->cur_func;
        if (!val_ref.is_const && fn->hasRetAttribute(llvm::Attribute::ZExt)) {
            const auto bit_width = val->getType()->getIntegerBitWidth();
            // TODO(ts): add zext/sext to ValuePartRef
            // reload/move_into_specific?
            switch (bit_width) {
            case 8: ASM(MOVZXr32r8, target_reg, target_reg); break;
            case 16: ASM(MOVZXr32r16, target_reg, target_reg); break;
            case 32: ASM(MOV32rr, target_reg, target_reg); break;
            case 64: break;
            default: {
                if (bit_width <= 32) {
                    ASM(AND32ri, target_reg, (1ull << bit_width) - 1);
                } else {
                    // TODO(ts): instead generate
                    // shl target_reg, (64 - bit_width)
                    // shr target_reg, (64 - bit_width)?
                    ScratchReg scratch{this};
                    const auto tmp_reg = scratch.alloc_from_bank_excluding(
                        0, (1ull << target_reg.id()));
                    ASM(MOV64ri, tmp_reg, (1ull << bit_width) - 1);
                    ASM(AND64rr, target_reg, tmp_reg);
                }
                break;
            }
            }
        } else if (fn->hasRetAttribute(llvm::Attribute::SExt)) {
            const auto bit_width = val->getType()->getIntegerBitWidth();
            switch (bit_width) {
            case 8: ASM(MOVSXr64r8, target_reg, target_reg); break;
            case 16: ASM(MOVSXr64r16, target_reg, target_reg); break;
            case 32: ASM(MOVSXr64r32, target_reg, target_reg); break;
            case 64: break;
            default: {
                if (bit_width <= 32) {
                    ASM(SHL32ri, target_reg, 32 - bit_width);
                    ASM(SAR32ri, target_reg, 32 - bit_width);
                } else {
                    ASM(SHL64ri, target_reg, 64 - bit_width);
                    ASM(SAR64ri, target_reg, 64 - bit_width);
                }
                break;
            }
            }
        }
        break;
    }
    case i128: {
        assert(val->getType()->isIntegerTy());
        auto val_ref = this->val_ref(val_idx, 0);
        val_ref.inc_ref_count();
        auto val_ref_high = this->val_ref(val_idx, 1);

        const auto call_conv = this->cur_calling_convention();
        if (val_ref.is_const) {
            assert(val_ref_high.is_const);
            this->materialize_constant(val_ref, call_conv.ret_regs_gp()[0]);
            this->materialize_constant(val_ref_high,
                                       call_conv.ret_regs_gp()[1]);
            break;
        }

        if (val_ref.assignment().fixed_assignment()) {
            val_ref.reload_into_specific(this, call_conv.ret_regs_gp()[0]);
        } else {
            val_ref.move_into_specific(call_conv.ret_regs_gp()[0]);
        }

        if (val_ref_high.assignment().fixed_assignment()) {
            val_ref_high.reload_into_specific(this, call_conv.ret_regs_gp()[1]);
        } else {
            val_ref_high.move_into_specific(call_conv.ret_regs_gp()[1]);
        }
        break;
    }
    case f32:
    case f64:
    case v32:
    case v64:
    case v128: {
        assert(val_info.type != LLVMBasicValType::f32
               || val->getType()->isFloatTy());
        assert(val_info.type != LLVMBasicValType::f64
               || val->getType()->isDoubleTy());
        auto val_ref = this->val_ref(val_idx, 0);

        const auto call_conv = this->cur_calling_convention();
        if (val_ref.is_const) {
            this->materialize_constant(val_ref, call_conv.ret_regs_vec()[0]);
            break;
        }

        if (val_ref.assignment().fixed_assignment()) {
            val_ref.reload_into_specific(this, call_conv.ret_regs_vec()[0]);
        } else {
            val_ref.move_into_specific(call_conv.ret_regs_vec()[0]);
        }
        break;
    }
    case v256:
    case v512: {
        TPDE_LOG_ERR("Vector types not yet supported");
        assert(0);
        exit(1);
    }
    case invalid:
    case none:
    case complex: {
        TPDE_LOG_ERR("Invalid value type for return: {}",
                     static_cast<u32>(val_info.type));
        assert(0);
        exit(1);
    }
    }
}

void LLVMCompilerX64::create_frem_calls(const IRValueRef lhs,
                                        const IRValueRef rhs,
                                        ValuePartRef   &&res_val,
                                        const bool       is_double) noexcept {
    SymRef sym;
    if (is_double) {
        sym = get_or_create_sym_ref(sym_fmod, "fmod");
    } else {
        sym = get_or_create_sym_ref(sym_fmodf, "fmodf");
    }

    std::array<CallArg, 2> args = {
        {CallArg{lhs}, CallArg{rhs}}
    };

    std::variant<ValuePartRef, std::pair<ScratchReg, u8>> res =
        std::move(res_val);

    generate_call(
        sym, args, std::span{&res, 1}, tpde::x64::CallingConv::SYSV_CC, false);
}

extern bool compile_llvm(llvm::LLVMContext    &ctx,
                         llvm::Module         &mod,
                         const char           *out_path,
                         [[maybe_unused]] bool print_liveness) {
    auto adaptor  = std::make_unique<LLVMAdaptor>(ctx, mod);
    auto compiler = std::make_unique<LLVMCompilerX64>(std::move(adaptor));
#ifdef TPDE_TESTING
    compiler->analyzer.test_print_liveness = print_liveness;
#endif


    if (!compiler->compile()) {
        return false;
    }

    std::ofstream         out{out_path, std::ios::binary};
    const std::vector<u8> data = compiler->assembler.build_object_file();
    out.write(reinterpret_cast<const char *>(data.data()), data.size());

    return true;
}

extern bool compile_llvm(llvm::LLVMContext    &ctx,
                         llvm::Module         &mod,
                         std::vector<u8>      &out_buf,
                         [[maybe_unused]] bool print_liveness) {
    auto adaptor  = std::make_unique<LLVMAdaptor>(ctx, mod);
    auto compiler = std::make_unique<LLVMCompilerX64>(std::move(adaptor));

#ifdef TPDE_TESTING
    compiler->analyzer.test_print_liveness = print_liveness;
#endif

    if (!compiler->compile()) {
        return false;
    }

    std::vector<u8> data = compiler->assembler.build_object_file();
    out_buf              = std::move(data);

    return true;
}

} // namespace tpde_llvm::x64
