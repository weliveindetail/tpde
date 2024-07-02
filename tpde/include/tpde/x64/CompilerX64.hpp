// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "AssemblerElfX64.hpp"
#include "tpde/CompilerBase.hpp"

#ifdef TPDE_ASSERTS
    #include <fadec.h>
#endif

// Helper macros for assembling in the compiler
#if defined(ASM) || defined(ASMF) || defined(ASMNC) || defined(ASME)
    #error Got definition for ASM macros from somewhere else. Maybe you included compilers for multiple architectures?
#endif

#define ASM(op, ...)                                                           \
    do {                                                                       \
        this->assembler.text_ensure_space(16);                                 \
        u32 inst_len =                                                         \
            fe64_##op(this->assembler.text_write_ptr, 0, __VA_ARGS__);         \
        assert(inst_len != 0);                                                 \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate instruction with flags
#define ASMF(op, flag, ...)                                                    \
    do {                                                                       \
        this->assembler.text_ensure_space(16);                                 \
        u32 inst_len =                                                         \
            fe64_##op(this->assembler.text_write_ptr, flag, __VA_ARGS__);      \
        assert(inst_len != 0);                                                 \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate instruction and reserve a custom amount of bytes
#define ASME(bytes, op, ...)                                                   \
    do {                                                                       \
        assert(bytes >= 1);                                                    \
        this->assembler.text_ensure_space(cnt);                                \
        u32 inst_len =                                                         \
            fe64_##op(this->assembler.text_write_ptr, 0, __VA_ARGS__);         \
        assert(inst_len != 0);                                                 \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate an instruction without checking that enough space is available
#define ASMNC(op, ...)                                                         \
    do {                                                                       \
        u32 inst_len =                                                         \
            fe64_##op(this->assembler.text_write_ptr, 0, __VA_ARGS__);         \
        assert(inst_len != 0);                                                 \
        assert(this->assembler.text_reserve_end                                \
                   - this->assembler.text_write_ptr                            \
               >= inst_len);                                                   \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

namespace tpde::x64 {

struct AsmReg : AsmRegBase {
    enum REG : u8 {
        AX = 0,
        CX,
        DX,
        BX,
        SP,
        BP,
        SI,
        DI,
        R8,
        R9,
        R10,
        R11,
        R12,
        R13,
        R14,
        R15,

        XMM0 = 32,
        XMM1,
        XMM2,
        XMM3,
        XMM4,
        XMM5,
        XMM6,
        XMM7,
        XMM8,
        XMM9,
        XMM10,
        XMM11,
        XMM12,
        XMM13,
        XMM14,
        XMM15,
        // TODO(ts): optional support for AVX registers with compiler flag
    };

    constexpr AsmReg(const REG id) noexcept : AsmRegBase(id) {}

    constexpr AsmReg(const AsmRegBase base) noexcept : AsmRegBase(base) {}

    constexpr explicit AsmReg(const u8 id) noexcept : AsmRegBase(id) {
        assert(id <= R15 || (id >= XMM0 && id <= XMM15));
    }

    constexpr operator FeRegGP() const noexcept {
        assert(reg_id <= R15);
        return FeRegGP{reg_id};
    }

    constexpr operator FeRegXMM() const noexcept {
        assert(reg_id >= XMM0 && reg_id <= XMM15);
        return FeRegXMM{static_cast<u8>(reg_id & 0x1F)};
    }
};

constexpr static u64
    create_bitmask(const std::initializer_list<AsmReg::REG> regs) {
    u64 set = 0;
    for (const auto reg : regs) {
        set |= 1ull << reg;
    }
    return set;
}

template <size_t N>
constexpr static u64 create_bitmask(const std::array<AsmReg, N> regs) {
    u64 set = 0;
    for (const auto reg : regs) {
        set |= 1ull << reg.id();
    }
    return set;
}

struct CallingConv {
    enum TYPE {
        SYSV_CC
    };

    TYPE ty;

    constexpr CallingConv(const TYPE ty) : ty(ty) {}

    [[nodiscard]] constexpr std::span<const FeRegGP>
        arg_regs_gp() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::arg_regs_gp;
        }
    }

    [[nodiscard]] constexpr std::span<const FeRegXMM>
        arg_regs_vec() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::arg_regs_vec;
        }
    }

    [[nodiscard]] constexpr std::span<const AsmReg>
        callee_saved_regs() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::callee_saved_regs;
        }
    }

    [[nodiscard]] constexpr u64 callee_saved_mask() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::callee_saved_mask;
        }
    }

    [[nodiscard]] constexpr u64 initial_free_regs() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::initial_free_regs;
        }
    }

    struct SysV {
        constexpr static std::array<FeRegGP, 6> arg_regs_gp{
            FE_DI, FE_SI, FE_DX, FE_CX, FE_R8, FE_R9};

        constexpr static std::array<FeRegXMM, 8> arg_regs_vec{FE_XMM0,
                                                              FE_XMM1,
                                                              FE_XMM2,
                                                              FE_XMM3,
                                                              FE_XMM4,
                                                              FE_XMM5,
                                                              FE_XMM6,
                                                              FE_XMM7};

        constexpr static std::array<AsmReg, 5> callee_saved_regs{
            AsmReg::BX,
            AsmReg::R12,
            AsmReg::R13,
            AsmReg::R14,
            AsmReg::R15,
        };

        // all XMM and normal regs (except bp/sp) are available
        constexpr static u64 initial_free_regs =
            0xFFFF'0000'FFFF & ~create_bitmask({AsmReg::BP, AsmReg::SP});

        constexpr static u64 callee_saved_mask =
            create_bitmask(callee_saved_regs);
    };
};

struct PlatformConfig : CompilerConfigDefault {
    using Assembler = AssemblerElfX64;
    using AsmReg    = tpde::x64::AsmReg;

    static constexpr u8   GP_BANK                 = 0;
    static constexpr bool FRAME_INDEXING_NEGATIVE = true;
};

template <IRAdaptor Adaptor, typename Derived>
struct CompilerX64 : CompilerBase<Adaptor, Derived, PlatformConfig> {
    using Base = CompilerBase<Adaptor, Derived, PlatformConfig>;

    using IRValueRef = typename Base::IRValueRef;
    using IRBlockRef = typename Base::IRBlockRef;
    using IRFuncRef  = typename Base::IRFuncRef;

    using Base::derived;

    static constexpr u32 PLATFORM_POINTER_SIZE = 8;

    u32 func_start_off = 0u, func_reg_save_off = 0u, func_reg_save_alloc = 0u,
        func_reg_restore_alloc                        = 0u;
    /// Offset to the `sub rsp, XXX` instruction that sets up the frame
    u32                       frame_size_setup_offset = 0u;
    util::SmallVector<u32, 8> func_ret_offs           = {};

    // for now, always generate an object
    explicit CompilerX64(Adaptor *adaptor) : Base{adaptor, true} {}

    void gen_func_prolog_and_args() noexcept;
    void finish_func() noexcept;

    void reset_register_file() noexcept;

    void reset() noexcept;

    // helpers

    void gen_func_epilog() noexcept;

    void spill_reg(const AsmReg::REG reg,
                   const u32         frame_off,
                   const u32         size) noexcept;

    void load_from_stack(AsmReg::REG dst,
                         u32         frame_off,
                         u32         size,
                         bool        sign_extend = false) noexcept;

    void mov(AsmReg::REG dst, AsmReg::REG src, bool only_32 = false) noexcept;
};

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::gen_func_prolog_and_args() noexcept {
    // prologue:
    // push rbp
    // mov rbp, rsp
    // optionally create vararg save-area
    // reserve space for callee-saved regs
    //   = 1 byte for each of the lower 8 regs and 2 bytes for the higher 8 regs
    // sub rsp, #<frame_size>+<largest_call_frame_usage>

    // TODO(ts): technically we only need rbp if there is a dynamic alloca
    // but then we need to make the frame indexing dynamic in CompilerBase
    // and the unwind info needs to take the dynamic sub rsp for calls into
    // account

    func_ret_offs.clear();
    func_start_off = this->assembler.text_cur_off();
    ASM(PUSHr, FE_BP);
    ASM(MOV64rr, FE_BP, FE_SP);

    if (this->adaptor->cur_is_vararg()) {
        assert(0);
    }

    const CallingConv call_conv = derived()->cur_calling_convention();
    {
        func_reg_save_off = this->assembler.text_cur_off();

        u32 reg_save_size = 0u;
        for (const auto reg : call_conv.callee_saved_regs()) {
            // need special handling for xmm saves if they are needed
            assert(reg.id() <= AsmReg::R15);
            reg_save_size += (reg.id() < AsmReg::R8) ? 1 : 2;
        }
        this->assembler.text_ensure_space(reg_save_size);
        this->assembler.text_write_ptr += reg_save_size;
        func_reg_save_alloc             = reg_save_size;
        // pop uses the same amount of bytes as push
        func_reg_restore_alloc          = reg_save_size;
    }

    // TODO(ts): support larger stack alignments?

    // placeholder for later
    frame_size_setup_offset = this->assembler.text_cur_off();
    ASM(SUB64ri, FE_SP, 0x7FFF'FFFF);
#ifdef TPDE_ASSERTS
    assert((this->assembler.text_cur_off() - frame_size_setup_offset) == 7);
#endif

    // TODO(ts): setup args

    u32 scalar_reg_count = 0, xmm_reg_count = 0;
    u32 frame_off = 16;
    for (const IRValueRef arg : this->adaptor->cur_args()) {
        const u32 part_count = this->adaptor->val_part_count(arg);

        for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
            auto part_ref = this->result_ref_lazy(arg, part_idx);


            if (part_idx != part_count - 1) {
                part_ref.inc_ref_count();
            }
        }

        assert(0);
    }
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::finish_func() noexcept {
    const auto final_frame_size = util::align_up(this->stack.frame_size, 16);
    *reinterpret_cast<u32 *>(this->assembler.sec_text.data.data()
                             + frame_size_setup_offset + 3) = final_frame_size;
#ifdef TPDE_ASSERTS
    FdInstr instr = {};
    assert(fd_decode(this->assembler.sec_text.data.data()
                         + frame_size_setup_offset,
                     7,
                     64,
                     0,
                     &instr)
           == 7);
    assert(FD_TYPE(&instr) == FDI_SUB);
    assert(FD_OP_TYPE(&instr, 0) == FD_OT_REG);
    assert(FD_OP_TYPE(&instr, 1) == FD_OT_IMM);
    assert(FD_OP_SIZE(&instr, 0) == 8);
    assert(FD_OP_SIZE(&instr, 1) == 8);
    assert(FD_OP_IMM(&instr, 1) == final_frame_size);
#endif

    const CallingConv conv = Base::derived()->cur_calling_convention();

    auto *write_ptr = this->assembler.sec_text.data.data() + func_reg_save_off;
    for (auto reg : util::BitSetIterator{this->register_file.clobbered
                                         & conv.callee_saved_mask()}) {
        assert(reg <= AsmReg::R15);
        write_ptr +=
            fe64_PUSHr(write_ptr, 0, AsmReg{static_cast<AsmReg::REG>(reg)});
    }

    // nop out the rest
    const auto reg_save_end = this->assembler.sec_text.data.data()
                              + func_reg_save_off + func_reg_save_alloc;
    assert(reg_save_end >= write_ptr);
    const u32 nop_len = reg_save_end - write_ptr;
    if (nop_len) {
        fe64_NOP(write_ptr, nop_len);
    }


    if (func_ret_offs.empty()) {
        return;
    }

    auto *text_data     = this->assembler.sec_text.data.data();
    u32   first_ret_off = func_ret_offs[0];
    u32   ret_size      = 0;
    {
        write_ptr            = text_data + first_ret_off;
        const auto ret_start = write_ptr;
        write_ptr += fe64_ADD64ri(write_ptr, 0, FE_SP, final_frame_size);
        for (auto reg : util::BitSetIterator<true>{
                 this->register_file.clobbered & conv.callee_saved_mask()}) {
            assert(reg <= AsmReg::R15);
            write_ptr +=
                fe64_POPr(write_ptr, 0, AsmReg{static_cast<AsmReg::REG>(reg)});
        }
        write_ptr += fe64_POPr(write_ptr, 0, FE_BP);
        write_ptr += fe64_RET(write_ptr, 0);
        ret_size   = write_ptr - ret_start;
    }

    for (u32 i = 1; i < func_ret_offs.size(); ++i) {
        std::memcpy(
            text_data + func_ret_offs[i], text_data + first_ret_off, ret_size);
    }
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::reset_register_file() noexcept {
    const CallingConv conv            = derived()->cur_calling_convention();
    this->register_file.fixed         = this->register_file.used =
        this->register_file.clobbered = 0;
    this->register_file.free          = conv.initial_free_regs();
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::reset() noexcept {
    func_ret_offs.clear();
    Base::reset();
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::gen_func_epilog() noexcept {
    // epilogue:
    // add rsp, #<frame_size>+<largest_call_frame_usage>
    // optionally create vararg save-area
    // reserve space for callee-saved regs
    //   = 1 byte for each of the lower 8 regs and 2 bytes for the higher 8 regs
    // pop rbp
    // ret
    //
    // however, since we will later patch this, we only reserve the space for
    // now

    func_ret_offs.push_back(this->assembler.text_cur_off());

    u32 epilogue_size = 7 + 1 + 1 + func_reg_restore_alloc; // add + pop + ret

    this->assembler.text_ensure_space(epilogue_size);
    this->assembler.text_write_ptr += epilogue_size;
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::spill_reg(const AsmReg::REG reg,
                                              const u32         frame_off,
                                              const u32         size) noexcept {
    this->assembler.text_ensure_space(16);
    const auto mem = FE_MEM(FE_BP, 0, FE_NOREG, -static_cast<i32>(frame_off));
    if (reg <= AsmReg::R15) {
        switch (size) {
        case 1: ASMNC(MOV8mr, mem, AsmReg{reg}); break;
        case 2: ASMNC(MOV16mr, mem, AsmReg{reg}); break;
        case 4: ASMNC(MOV32mr, mem, AsmReg{reg}); break;
        case 8: ASMNC(MOV64mr, mem, AsmReg{reg}); break;
        default: assert(0); __builtin_unreachable();
        }
        return;
    }

    switch (size) {
    case 4: ASMNC(SSE_MOVD_X2Gmr, mem, AsmReg{reg}); break;
    case 8: ASMNC(SSE_MOVQ_X2Gmr, mem, AsmReg{reg}); break;
    case 16:
        ASMNC(SSE_MOVUPDmr, mem, AsmReg{reg});
        break;
        // TODO(ts): 32/64 with feature flag?
    case 1: assert(0);
    case 2: assert(0);
    default: assert(0); __builtin_unreachable();
    }
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::load_from_stack(
    const AsmReg::REG dst,
    const u32         frame_off,
    const u32         size,
    const bool        sign_extend) noexcept {
    this->assembler.text_ensure_space(16);
    const auto mem = FE_MEM(FE_BP, 0, FE_NOREG, -static_cast<i32>(frame_off));

    if (dst <= AsmReg::R15) {
        if (!sign_extend) {
            switch (size) {
            case 1: ASMNC(MOVZXr32m8, AsmReg{dst}, mem); break;
            case 2: ASMNC(MOVZXr32m16, AsmReg{dst}, mem); break;
            case 4: ASMNC(MOV32rm, AsmReg{dst}, mem); break;
            case 8: ASMNC(MOV64rm, AsmReg{dst}, mem); break;
            default: assert(0); __builtin_unreachable();
            }
        } else {
            switch (size) {
            case 1: ASMNC(MOVSXr64m8, AsmReg{dst}, mem); break;
            case 2: ASMNC(MOVSXr64m16, AsmReg{dst}, mem); break;
            case 4: ASMNC(MOVSXr64m32, AsmReg{dst}, mem); break;
            case 8: ASMNC(MOV64rm, AsmReg{dst}, mem); break;
            default: assert(0); __builtin_unreachable();
            }
        }
        return;
    }

    assert(!sign_extend);

    switch (size) {
    case 4: ASMNC(SSE_MOVD_G2Xrm, AsmReg{dst}, mem); break;
    case 8: ASMNC(SSE_MOVQ_G2Xrm, AsmReg{dst}, mem); break;
    case 16:
        ASMNC(SSE_MOVUPDrm, AsmReg{dst}, mem);
        break;
        // TODO(ts): 32/64 with feature flag?
    case 1: assert(0);
    case 2: assert(0);
    default: assert(0); __builtin_unreachable();
    }
}

template <IRAdaptor Adaptor, typename Derived>
void CompilerX64<Adaptor, Derived>::mov(const AsmReg::REG dst,
                                        const AsmReg::REG src,
                                        const bool        only_32) noexcept {
    if (!only_32) {
        ASM(MOV64rr, AsmReg{dst}, AsmReg{src});
    } else {
        ASM(MOV32rr, AsmReg{dst}, AsmReg{src});
    }
}
} // namespace tpde::x64
