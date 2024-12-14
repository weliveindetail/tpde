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
        u32 inst_len = fe64_##op(this->assembler.text_write_ptr,               \
                                 0 __VA_OPT__(, ) __VA_ARGS__);                \
        assert(inst_len != 0);                                                 \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate instruction with flags
#define ASMF(op, flag, ...)                                                    \
    do {                                                                       \
        this->assembler.text_ensure_space(16);                                 \
        u32 inst_len = fe64_##op(this->assembler.text_write_ptr,               \
                                 flag __VA_OPT__(, ) __VA_ARGS__);             \
        assert(inst_len != 0);                                                 \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate instruction and reserve a custom amount of bytes
#define ASME(bytes, op, ...)                                                   \
    do {                                                                       \
        assert(bytes >= 1);                                                    \
        this->assembler.text_ensure_space(cnt);                                \
        u32 inst_len = fe64_##op(this->assembler.text_write_ptr,               \
                                 0 __VA_OPT__(, ) __VA_ARGS__);                \
        assert(inst_len != 0);                                                 \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate an instruction without checking that enough space is available
#define ASMNC(op, ...)                                                         \
    do {                                                                       \
        u32 inst_len = fe64_##op(this->assembler.text_write_ptr,               \
                                 0 __VA_OPT__(, ) __VA_ARGS__);                \
        assert(inst_len != 0);                                                 \
        assert(this->assembler.text_reserve_end                                \
                   - this->assembler.text_write_ptr                            \
               >= inst_len);                                                   \
        this->assembler.text_write_ptr += inst_len;                            \
    } while (false)

// generate an instruction with a custom compiler ptr
#define ASMC(compiler, op, ...)                                                \
    do {                                                                       \
        compiler->assembler.text_ensure_space(16);                             \
        u32 inst_len = fe64_##op(compiler->assembler.text_write_ptr,           \
                                 0 __VA_OPT__(, ) __VA_ARGS__);                \
        assert(inst_len != 0);                                                 \
        compiler->assembler.text_write_ptr += inst_len;                        \
    } while (false)


// generate an instruction without checking that enough space is available and a
// flag
#define ASMNCF(op, flag, ...)                                                  \
    do {                                                                       \
        u32 inst_len = fe64_##op(this->assembler.text_write_ptr,               \
                                 flag __VA_OPT__(, ) __VA_ARGS__);             \
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

    constexpr explicit AsmReg() noexcept : AsmRegBase((u8)0xFF) {}

    constexpr AsmReg(const REG id) noexcept : AsmRegBase((u8)id) {}

    constexpr AsmReg(const AsmRegBase base) noexcept : AsmRegBase(base) {}

    constexpr explicit AsmReg(const u8 id) noexcept : AsmRegBase(id) {
        assert(id <= R15 || (id >= XMM0 && id <= XMM15));
    }

    constexpr explicit AsmReg(const u64 id) noexcept : AsmRegBase(id) {
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

struct PlatformConfig : CompilerConfigDefault {
    using Assembler = AssemblerElfX64;
    using AsmReg    = tpde::x64::AsmReg;

    static constexpr u8   GP_BANK                 = 0;
    static constexpr u8   FP_BANK                 = 1;
    static constexpr bool FRAME_INDEXING_NEGATIVE = true;
    static constexpr u32  PLATFORM_POINTER_SIZE   = 8;
    static constexpr u32  NUM_BANKS               = 2;
};

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy =
              CompilerBase,
          typename Config = PlatformConfig>
struct CompilerX64;

struct CallingConv {
    enum TYPE {
        SYSV_CC
    };

    TYPE ty;

    constexpr CallingConv(const TYPE ty) : ty(ty) {}

    [[nodiscard]] constexpr std::span<const AsmReg>
        arg_regs_gp() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::arg_regs_gp;
        }
    }

    [[nodiscard]] constexpr std::span<const AsmReg>
        arg_regs_vec() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::arg_regs_vec;
        }
    }

    [[nodiscard]] constexpr std::span<const AsmReg>
        ret_regs_gp() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::ret_regs_gp;
        }
    }

    [[nodiscard]] constexpr std::span<const AsmReg>
        ret_regs_vec() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::ret_regs_vec;
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

    [[nodiscard]] constexpr u64 arg_regs_mask() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::arg_regs_mask;
        }
    }

    [[nodiscard]] constexpr u64 result_regs_mask() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::result_regs_mask;
        }
    }

    [[nodiscard]] constexpr u64 initial_free_regs() const noexcept {
        switch (ty) {
        case SYSV_CC: return SysV::initial_free_regs;
        }
    }

    template <typename Adaptor,
              typename Derived,
              template <typename, typename, typename>
              typename BaseTy,
              typename Config>
    void handle_func_args(
        CompilerX64<Adaptor, Derived, BaseTy, Config> *) const noexcept;

    template <typename Adaptor,
              typename Derived,
              template <typename, typename, typename>
              typename BaseTy,
              typename Config>
    u32 calculate_call_stack_space(
        CompilerX64<Adaptor, Derived, BaseTy, Config> *,
        std::span<
            typename CompilerX64<Adaptor, Derived, BaseTy, Config>::CallArg>
            arguments) noexcept;

    /// returns the number of vector arguments
    template <typename Adaptor,
              typename Derived,
              template <typename, typename, typename>
              typename BaseTy,
              typename Config>
    u32 handle_call_args(
        CompilerX64<Adaptor, Derived, BaseTy, Config> *,
        std::span<
            typename CompilerX64<Adaptor, Derived, BaseTy, Config>::CallArg>
            arguments) noexcept;

    template <typename Adaptor,
              typename Derived,
              template <typename, typename, typename>
              typename BaseTy,
              typename Config>
    void fill_call_results(
        CompilerX64<Adaptor, Derived, BaseTy, Config> *,
        std::span<std::variant<
            typename CompilerX64<Adaptor, Derived, BaseTy, Config>::
                ValuePartRef,
            std::pair<typename CompilerX64<Adaptor, Derived, BaseTy, Config>::
                          ScratchReg,
                      u8>>> results) noexcept;

    struct SysV {
        constexpr static std::array<AsmReg, 6> arg_regs_gp{AsmReg::DI,
                                                           AsmReg::SI,
                                                           AsmReg::DX,
                                                           AsmReg::CX,
                                                           AsmReg::R8,
                                                           AsmReg::R9};

        constexpr static std::array<AsmReg, 8> arg_regs_vec{AsmReg::XMM0,
                                                            AsmReg::XMM1,
                                                            AsmReg::XMM2,
                                                            AsmReg::XMM3,
                                                            AsmReg::XMM4,
                                                            AsmReg::XMM5,
                                                            AsmReg::XMM6,
                                                            AsmReg::XMM7};

        constexpr static std::array<AsmReg, 2> ret_regs_gp{AsmReg::AX,
                                                           AsmReg::DX};

        constexpr static std::array<AsmReg, 2> ret_regs_vec{AsmReg::XMM0,
                                                            AsmReg::XMM1};

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

        constexpr static u64 arg_regs_mask =
            create_bitmask(arg_regs_gp) | create_bitmask(arg_regs_vec);

        constexpr static u64 result_regs_mask =
            create_bitmask(ret_regs_gp) | create_bitmask(ret_regs_vec);
    };
};

namespace concepts {
template <typename T, typename Config>
concept Compiler = tpde::Compiler<T, Config> && requires(T a) {
    {
        a.arg_is_int128(std::declval<typename T::IRValueRef>())
    } -> std::convertible_to<bool>;

    { a.cur_calling_convention() } -> SameBaseAs<CallingConv>;
};
} // namespace concepts

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
struct CompilerX64 : BaseTy<Adaptor, Derived, Config> {
    using Base = BaseTy<Adaptor, Derived, Config>;

    using IRValueRef = typename Base::IRValueRef;
    using IRBlockRef = typename Base::IRBlockRef;
    using IRFuncRef  = typename Base::IRFuncRef;

    using AssignmentPartRef = typename Base::AssignmentPartRef;
    using ScratchReg        = typename Base::ScratchReg;
    using ValuePartRef      = typename Base::ValuePartRef;
    using GenericValuePart = typename Base::GenericValuePart;

    using Assembler    = typename PlatformConfig::Assembler;
    using RegisterFile = typename Base::RegisterFile;

    using Base::derived;


    // TODO(ts): make this dependent on the number of callee-saved regs of the
    // current function or if there is a call in the function?
    static constexpr u32 NUM_FIXED_ASSIGNMENTS[PlatformConfig::NUM_BANKS] = {5,
                                                                             6};

    enum CPU_FEATURES : u32 {
        CPU_BASELINE   = 0, // x86-64-v1
        CPU_CMPXCHG16B = (1 << 0),
        CPU_POPCNT     = (1 << 1),
        CPU_SSE3       = (1 << 2),
        CPU_SSSE3      = (1 << 3),
        CPU_SSE4_1     = (1 << 4),
        CPU_SSE4_2     = (1 << 5),
        CPU_AVX        = (1 << 6),
        CPU_AVX2       = (1 << 7),
        CPU_BMI1       = (1 << 8),
        CPU_BMI2       = (1 << 9),
        CPU_F16C       = (1 << 10),
        CPU_FMA        = (1 << 11),
        CPU_LZCNT      = (1 << 12),
        CPU_MOVBE      = (1 << 13),
        CPU_AVX512F    = (1 << 14),
        CPU_AVX512BW   = (1 << 15),
        CPU_AVX512CD   = (1 << 16),
        CPU_AVX512DQ   = (1 << 17),
        CPU_AVX512VL   = (1 << 18),

        CPU_V2 = CPU_BASELINE | CPU_CMPXCHG16B | CPU_POPCNT | CPU_SSE3
                 | CPU_SSSE3 | CPU_SSE4_1 | CPU_SSE4_2,
        CPU_V3 = CPU_V2 | CPU_AVX | CPU_AVX2 | CPU_BMI1 | CPU_BMI2 | CPU_F16C
                 | CPU_FMA | CPU_LZCNT | CPU_MOVBE,
        CPU_V4 = CPU_V3 | CPU_AVX512F | CPU_AVX512BW | CPU_AVX512CD
                 | CPU_AVX512DQ | CPU_AVX512VL,
    };

    CPU_FEATURES cpu_feats = CPU_BASELINE;

    // When handling function arguments, we need to prevent argument registers
    // from being handed out as fixed registers
    //
    // Additionally, for now we prevent AX,DX,CX to be fixed to not run into
    // issues with instructions that need them as implicit arguments
    // also AX and DX can never be fixed if exception handling is used
    // since they are clobbered there
    u64 fixed_assignment_nonallocatable_mask =
        create_bitmask({AsmReg::AX, AsmReg::DX, AsmReg::CX});
    u32 func_start_off = 0u, func_reg_save_off = 0u, func_reg_save_alloc = 0u,
        func_reg_restore_alloc  = 0u;
    /// Offset to the `sub rsp, XXX` instruction that sets up the frame
    u32 frame_size_setup_offset = 0u;
    u32 scalar_arg_count = 0xFFFF'FFFF, vec_arg_count = 0xFFFF'FFFF;
    u32 reg_save_frame_off                  = 0;
    u32 var_arg_stack_off                   = 0;
    util::SmallVector<u32, 8> func_ret_offs = {};

    util::SmallVector<std::pair<IRFuncRef, typename Assembler::SymRef>, 4>
        personality_syms = {};

    // for now, always generate an object
    explicit CompilerX64(Adaptor           *adaptor,
                         const CPU_FEATURES cpu_features = CPU_BASELINE)
        : Base{adaptor, true}, cpu_feats(cpu_features) {
        static_assert(std::is_base_of_v<CompilerX64, Derived>);
        static_assert(concepts::Compiler<Derived, PlatformConfig>);
    }

    void start_func(u32 func_idx) noexcept;

    void gen_func_prolog_and_args() noexcept;

    // note: this has to call assembler->end_func
    void finish_func() noexcept;

    u32 func_reserved_frame_size() noexcept;

    void reset_register_file() noexcept;

    void reset() noexcept;

    // helpers

    void gen_func_epilog() noexcept;

    void spill_reg(const AsmReg reg,
                   const u32    frame_off,
                   const u32    size) noexcept;

    void load_from_stack(AsmReg dst,
                         i32    frame_off,
                         u32    size,
                         bool   sign_extend = false) noexcept;

    void load_address_of_var_reference(AsmReg            dst,
                                       AssignmentPartRef ap) noexcept;

    void mov(AsmReg dst, AsmReg src, u32 size) noexcept;

    AsmReg gval_expr_as_reg(GenericValuePart &gv) noexcept;

    void materialize_constant(ValuePartRef &val_ref, AsmReg dst) noexcept;
    void materialize_constant(ValuePartRef &val_ref, ScratchReg &dst) noexcept;
    void materialize_constant(const std::array<u8, 64> &data,
                              u32                       bank,
                              u32                       size,
                              AsmReg                    dst) noexcept;

    AsmReg select_fixed_assignment_reg(u32 bank, IRValueRef) noexcept;

    enum class Jump {
        ja,
        jae,
        jb,
        jbe,
        je,
        jg,
        jge,
        jl,
        jle,
        jmp,
        jne,
        jno,
        jo,
        js,
        jns,
        jp,
        jnp,
    };

    Jump invert_jump(Jump jmp) noexcept;
    Jump swap_jump(Jump jmp) noexcept;

    void generate_branch_to_block(Jump       jmp,
                                  IRBlockRef target,
                                  bool       needs_split,
                                  bool       last_inst) noexcept;

    void generate_raw_jump(Jump jmp, Assembler::Label target) noexcept;

    void generate_raw_set(Jump jmp, AsmReg dst) noexcept;

    void spill_before_call(CallingConv calling_conv, u64 except_mask = 0);

    struct CallArg {
        enum class Flag : u8 {
            none,
            zext,
            sext,
            byval
        };

        explicit CallArg(IRValueRef value,
                         Flag       flags       = Flag::none,
                         u32        byval_align = 0,
                         u32        byval_size  = 0)
            : value(value),
              flag(flags),
              byval_align(byval_align),
              byval_size(byval_size) {}

        IRValueRef value;
        Flag       flag;
        u32        byval_align;
        u32        byval_size;
    };

    /// Generate a function call
    ///
    /// This will get the arguments into the correct registers according to the
    /// calling convention, clear non-callee-saved registers from the register
    /// file (make sure you do not have any fixed assignments left over) and
    /// fill the result registers (the u8 in the ScratchReg pair indicates the
    /// register bank)
    ///
    /// Targets can be a symbol (call to PLT with relocation), or an indirect
    /// call to a ScratchReg/Value
    void generate_call(
        std::variant<Assembler::SymRef, ScratchReg, ValuePartRef> &&target,
        std::span<CallArg>                                          arguments,
        std::span<std::variant<ValuePartRef, std::pair<ScratchReg, u8>>>
                    results,
        CallingConv calling_conv,
        bool        variable_args);

    bool has_cpu_feats(CPU_FEATURES feats) const noexcept {
        return ((cpu_feats & feats) == feats);
    }
};

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CallingConv::handle_func_args(
    CompilerX64<Adaptor, Derived, BaseTy, Config> *compiler) const noexcept {
    using IRValueRef = typename Adaptor::IRValueRef;
    using ScratchReg =
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::ScratchReg;

    u32 scalar_reg_count = 0, xmm_reg_count = 0;
    i32 frame_off = 16;

    const auto gp_regs  = arg_regs_gp();
    const auto xmm_regs = arg_regs_vec();

    // not the most prettiest but it's okay for now
    // TODO(ts): we definitely want to see if writing some custom machinery to
    // give arguments their own register as a fixed assignment (if there are no
    // calls) gives perf benefits on C/C++ codebases with a lot of smaller
    // getters
    compiler->fixed_assignment_nonallocatable_mask |= arg_regs_mask();

    u32 arg_idx = 0;
    for (const IRValueRef arg : compiler->adaptor->cur_args()) {
        if (compiler->adaptor->cur_arg_is_byval(arg_idx)) {
            const u32 size  = compiler->adaptor->cur_arg_byval_size(arg_idx);
            const u32 align = compiler->adaptor->cur_arg_byval_align(arg_idx);
            assert(align <= 16);
            assert((align & (align - 1)) == 0);
            if (align == 16) {
                frame_off = util::align_up(frame_off, 16);
            }

            // need to use a ScratchReg here since otherwise the ValuePartRef
            // could allocate one of the argument registers
            ScratchReg ptr_scratch{compiler};
            auto       arg_ref = compiler->result_ref_lazy(arg, 0);
            const auto res_reg =
                ptr_scratch.alloc(Config::GP_BANK, arg_regs_mask());
            ASMC(compiler,
                 LEA64rm,
                 res_reg,
                 FE_MEM(FE_BP, 0, FE_NOREG, frame_off));
            compiler->set_value(arg_ref, ptr_scratch);

            frame_off += util::align_up(size, 8);
            ++arg_idx;
            continue;
        }

        const u32 part_count = compiler->derived()->val_part_count(arg);
        if (compiler->derived()->arg_is_int128(arg)) {
            if (scalar_reg_count + 1 >= gp_regs.size()) {
                scalar_reg_count = gp_regs.size();
            }
        }

        for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
            auto part_ref = compiler->result_ref_lazy(arg, part_idx);

            if (compiler->derived()->val_part_bank(arg, part_idx) == 0) {
                if (scalar_reg_count < gp_regs.size()) {
                    compiler->set_value(part_ref, gp_regs[scalar_reg_count++]);
                } else {
                    const auto size =
                        compiler->derived()->val_part_size(arg, part_idx);
                    //  TODO(ts): maybe allow negative frame offsets for value
                    //  assignments so we can simply reference this?
                    //  but this probably doesn't work with multi-part values
                    //  since the offsets are different
                    auto ap = part_ref.assignment();
                    if (ap.fixed_assignment()) {
                        compiler->load_from_stack(
                            AsmReg{ap.full_reg_id()}, -frame_off, size);
                        ap.set_modified(true);
                    } else {
                        ScratchReg scratch{compiler};
                        auto       tmp_reg = scratch.alloc_gp();
                        compiler->load_from_stack(tmp_reg, -frame_off, size);
                        compiler->spill_reg(tmp_reg, ap.frame_off(), size);
                    }
                    frame_off += 8;
                }
            } else {
                if (xmm_reg_count < xmm_regs.size()) {
                    compiler->set_value(part_ref, xmm_regs[xmm_reg_count++]);
                } else {
                    auto ap   = part_ref.assignment();
                    auto size = ap.part_size();
                    assert(size <= 16);
                    if (size == 16) {
                        // TODO(ts): I'm correct that 16 byte vector regs get
                        // aligned?
                        frame_off = util::align_up(frame_off, 16);
                    }

                    if (ap.fixed_assignment()) {
                        compiler->load_from_stack(
                            AsmReg{ap.full_reg_id()}, -frame_off, size);
                        ap.set_modified(true);
                    } else {
                        ScratchReg scratch{compiler};
                        auto tmp_reg = scratch.alloc(Config::FP_BANK);
                        compiler->load_from_stack(tmp_reg, -frame_off, size);
                        compiler->spill_reg(tmp_reg, ap.frame_off(), size);
                    }

                    frame_off += 8;
                    if (size > 8) {
                        frame_off += 8;
                    }
                }
            }

            if (part_idx != part_count - 1) {
                part_ref.inc_ref_count();
            }
        }

        ++arg_idx;
    }

    compiler->fixed_assignment_nonallocatable_mask &= ~arg_regs_mask();
    compiler->scalar_arg_count                      = scalar_reg_count;
    compiler->vec_arg_count                         = xmm_reg_count;
    compiler->var_arg_stack_off                     = frame_off;
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
u32 CallingConv::calculate_call_stack_space(
    CompilerX64<Adaptor, Derived, BaseTy, Config> *compiler,
    std::span<typename CompilerX64<Adaptor, Derived, BaseTy, Config>::CallArg>
        arguments) noexcept {
    using CallArg =
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::CallArg;

    u32 gp_reg_count = 0, xmm_reg_count = 0, stack_space = 0;

    const auto gp_regs  = arg_regs_gp();
    const auto xmm_regs = arg_regs_vec();

    for (auto &arg : arguments) {
        if (arg.flag == CallArg::Flag::byval) {
            // the value is passed fully on the stack
            assert(arg.byval_align <= 16);
            assert((arg.byval_align & (arg.byval_align - 1)) == 0);
            if (arg.byval_align == 16) {
                stack_space = util::align_up(stack_space, 16);
            } else {
                assert(stack_space == util::align_up(stack_space, 8));
            }

            stack_space += util::align_up(arg.byval_size, 8);
            continue;
        }

        const u32 part_count = compiler->derived()->val_part_count(arg.value);

        if (compiler->derived()->arg_is_int128(arg.value)) {
            if (gp_reg_count + 1 >= gp_regs.size()) {
                gp_reg_count = gp_regs.size();
            }
        }

        for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
            auto ref = compiler->val_ref(arg.value, part_idx);

            if (ref.bank() == 0) {
                if (gp_reg_count < gp_regs.size()) {
                    ++gp_reg_count;
                } else {
                    stack_space += 8;
                }
            } else {
                assert(ref.bank() == 1);
                if (xmm_reg_count < xmm_regs.size()) {
                    ++xmm_reg_count;
                } else {
                    stack_space += util::align_up(ref.part_size(), 8);
                }
            }

            ref.reset_without_refcount();
        }
    }

    return stack_space;
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
u32 CallingConv::handle_call_args(
    CompilerX64<Adaptor, Derived, BaseTy, Config> *compiler,
    std::span<typename CompilerX64<Adaptor, Derived, BaseTy, Config>::CallArg>
        arguments) noexcept {
    using CallArg =
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::CallArg;
    using ScratchReg =
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::ScratchReg;

    u32 gp_reg_count = 0, xmm_reg_count = 0;
    i32 stack_off = 0;

    const auto gp_regs  = arg_regs_gp();
    const auto xmm_regs = arg_regs_vec();

    util::SmallVector<ScratchReg, 8> arg_scratchs;


    for (auto &arg : arguments) {
        if (arg.flag == CallArg::Flag::byval) {
            ScratchReg scratch1(compiler), scratch2(compiler);
            auto       ptr_ref = compiler->val_ref(arg.value, 0);
            assert(!ptr_ref.is_const);
            ScratchReg ptr_scratch{compiler};
            AsmReg     ptr_reg = compiler->val_as_reg(ptr_ref, ptr_scratch);

            auto tmp_reg = scratch2.alloc_gp();

            auto size = arg.byval_size;
            assert(arg.byval_align <= 16);
            stack_off = util::align_up(stack_off, arg.byval_align);

            i32 off = 0;
            while (size > 0) {
                if (size >= 8) {
                    ASMC(compiler,
                         MOV64rm,
                         tmp_reg,
                         FE_MEM(ptr_reg, 0, FE_NOREG, off));
                    ASMC(compiler,
                         MOV64mr,
                         FE_MEM(FE_SP, 0, FE_NOREG, (i32)(stack_off + off)),
                         tmp_reg);
                    off  += 8;
                    size -= 8;
                    continue;
                }
                if (size >= 4) {
                    ASMC(compiler,
                         MOV32rm,
                         tmp_reg,
                         FE_MEM(ptr_reg, 0, FE_NOREG, off));
                    ASMC(compiler,
                         MOV32mr,
                         FE_MEM(FE_SP, 0, FE_NOREG, (i32)(stack_off + off)),
                         tmp_reg);
                    off  += 4;
                    size -= 4;
                    continue;
                }
                if (size >= 2) {
                    ASMC(compiler,
                         MOVZXr32m16,
                         tmp_reg,
                         FE_MEM(ptr_reg, 0, FE_NOREG, off));
                    ASMC(compiler,
                         MOV16mr,
                         FE_MEM(FE_SP, 0, FE_NOREG, (i32)(stack_off + off)),
                         tmp_reg);
                    off  += 2;
                    size -= 2;
                    continue;
                }
                ASMC(compiler,
                     MOVZXr32m8,
                     tmp_reg,
                     FE_MEM(ptr_reg, 0, FE_NOREG, off));
                ASMC(compiler,
                     MOV8mr,
                     FE_MEM(FE_SP, 0, FE_NOREG, (i32)(stack_off + off)),
                     tmp_reg);
                off  += 1;
                size -= 1;
            }

            stack_off += util::align_up(arg.byval_size, 8);
            continue;
        }

        const u32 part_count = compiler->derived()->val_part_count(arg.value);

        if (compiler->derived()->arg_is_int128(arg.value)) {
            if (gp_reg_count + 1 >= gp_regs.size()) {
                gp_reg_count = gp_regs.size();
            }
        }

        for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
            auto       ref = compiler->val_ref(arg.value, part_idx);
            ScratchReg scratch(compiler);

            if (ref.bank() == 0) {
                const auto ext_reg = [&](AsmReg reg) {
                    if (arg.flag == CallArg::Flag::zext) {
                        switch (ref.part_size()) {
                        case 1: ASMC(compiler, MOVZXr32r8, reg, reg); break;
                        case 2: ASMC(compiler, MOVZXr32r16, reg, reg); break;
                        case 4: ASMC(compiler, MOV32rr, reg, reg); break;
                        default: break;
                        }
                    } else if (arg.flag == CallArg::Flag::sext) {
                        switch (ref.part_size()) {
                        case 1: ASMC(compiler, MOVSXr64r8, reg, reg); break;
                        case 2: ASMC(compiler, MOVSXr64r16, reg, reg); break;
                        case 4: ASMC(compiler, MOVSXr64r32, reg, reg); break;
                        default: break;
                        }
                    }
                };

                if (gp_reg_count < gp_regs.size()) {
                    const AsmReg target_reg = gp_regs[gp_reg_count++];
                    if (ref.is_in_reg(target_reg) && ref.can_salvage()) {
                        scratch.alloc_specific(ref.salvage());
                    } else {
                        scratch.alloc_specific(
                            ref.reload_into_specific(compiler, target_reg));
                    }
                    ext_reg(scratch.cur_reg);
                    arg_scratchs.push_back(std::move(scratch));
                } else {
                    auto reg = ref.reload_into_specific_fixed(
                        compiler, scratch.alloc_gp());
                    ext_reg(reg);
                    ASMC(compiler,
                         MOV64mr,
                         FE_MEM(FE_SP, 0, FE_NOREG, stack_off),
                         reg);
                    stack_off += 8;
                }
            } else {
                assert(ref.bank() == 1);
                if (xmm_reg_count < xmm_regs.size()) {
                    const AsmReg target_reg = xmm_regs[xmm_reg_count++];
                    if (ref.is_in_reg(target_reg) && ref.can_salvage()) {
                        scratch.alloc_specific(ref.salvage());
                    } else {
                        scratch.alloc_specific(
                            ref.reload_into_specific(compiler, target_reg));
                    }
                    arg_scratchs.push_back(std::move(scratch));
                } else {
                    auto reg = compiler->val_as_reg(ref, scratch);
                    switch (ref.part_size()) {
                    case 4:
                        ASMC(compiler,
                             SSE_MOVD_X2Gmr,
                             FE_MEM(FE_SP, 0, FE_NOREG, stack_off),
                             reg);
                        stack_off += 8;
                        break;
                    case 8:
                        ASMC(compiler,
                             SSE_MOVQ_X2Gmr,
                             FE_MEM(FE_SP, 0, FE_NOREG, stack_off),
                             reg);
                        stack_off += 8;
                        break;
                    case 16: {
                        stack_off = util::align_up(stack_off, 16);
                        ASMC(compiler,
                             SSE_MOVAPDmr,
                             FE_MEM(FE_SP, 0, FE_NOREG, stack_off),
                             reg);
                        stack_off += 16;
                        break;
                    }
                        // can't guarantee the alignment on the stack
                    default: assert(0); exit(1);
                    }
                }
            }

            if (part_idx != part_count - 1) {
                ref.reset_without_refcount();
            }
        }
    }

    return xmm_reg_count;
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CallingConv::fill_call_results(
    CompilerX64<Adaptor, Derived, BaseTy, Config> *compiler,
    std::span<std::variant<
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::ValuePartRef,
        std::pair<
            typename CompilerX64<Adaptor, Derived, BaseTy, Config>::ScratchReg,
            u8>>>                                  results) noexcept {
    using ValuePartRef =
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::ValuePartRef;
    using ScratchReg =
        typename CompilerX64<Adaptor, Derived, BaseTy, Config>::ScratchReg;

    u32 gp_reg_count = 0, xmm_reg_count = 0;

    const auto gp_regs  = ret_regs_gp();
    const auto xmm_regs = ret_regs_vec();

    for (auto &res : results) {
        if (std::holds_alternative<ValuePartRef>(res)) {
            auto &ref = std::get<ValuePartRef>(res);
            assert(!ref.is_const);
            if (ref.bank() == 0) {
                assert(gp_reg_count < gp_regs.size());
                compiler->set_value(ref, gp_regs[gp_reg_count++]);
            } else {
                assert(ref.bank() == 1);
                assert(xmm_reg_count < xmm_regs.size());
                compiler->set_value(ref, xmm_regs[xmm_reg_count++]);
            }
        } else {
            auto &[reg, bank] = std::get<std::pair<ScratchReg, u8>>(res);
            reg.reset();

            if (bank == 0) {
                assert(gp_reg_count < gp_regs.size());
                reg.cur_reg = gp_regs[gp_reg_count++];
            } else {
                assert(bank == 1);
                assert(xmm_reg_count < xmm_regs.size());
                reg.cur_reg = xmm_regs[xmm_reg_count++];
            }

            compiler->register_file.mark_used(
                reg.cur_reg,
                CompilerX64<Adaptor, Derived, BaseTy, Config>::
                    INVALID_VAL_LOCAL_IDX,
                0);
            compiler->register_file.mark_fixed(reg.cur_reg);
        }
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::start_func(
    const u32 func_idx) noexcept {
    using SymRef           = typename Assembler::SymRef;
    SymRef personality_sym = Assembler::INVALID_SYM_REF;
    if (this->adaptor->cur_needs_unwind_info()) {
        const IRFuncRef personality_func =
            this->adaptor->cur_personality_func();
        if (personality_func != Adaptor::INVALID_FUNC_REF) {
            for (const auto &[func_ref, sym] : personality_syms) {
                if (func_ref == personality_func) {
                    personality_sym = sym;
                    break;
                }
            }

            if (personality_sym == Assembler::INVALID_SYM_REF) {
                // create symbol that contains the address of the personality
                // function
                auto fn_sym = this->assembler.sym_add_undef(
                    this->adaptor->func_link_name(personality_func));

                u32 off;
                u8  tmp[8]      = {};
                personality_sym = this->assembler.sym_def_data(
                    {}, {tmp, sizeof(tmp)}, 8, true, true, true, false, &off);
                this->assembler.reloc_data_abs(fn_sym, true, off, 0);

                personality_syms.emplace_back(personality_func,
                                              personality_sym);
            }
        }
    }
    this->assembler.start_func(this->func_syms[func_idx], personality_sym);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::
    gen_func_prolog_and_args() noexcept {
    // prologue:
    // push rbp
    // mov rbp, rsp
    // optionally create vararg save-area
    // reserve space for callee-saved regs
    //   = 1 byte for each of the lower 8 regs and 2
    //   bytes for the higher 8 regs
    // sub rsp, #<frame_size>+<largest_call_frame_usage>

    // TODO(ts): technically we only need rbp if there
    // is a dynamic alloca but then we need to make the
    // frame indexing dynamic in CompilerBase and the
    // unwind info needs to take the dynamic sub rsp for
    // calls into account

    func_ret_offs.clear();
    func_start_off   = this->assembler.text_cur_off();
    scalar_arg_count = vec_arg_count = 0xFFFF'FFFF;
    ASM(PUSHr, FE_BP);
    ASM(MOV64rr, FE_BP, FE_SP);

    const CallingConv call_conv = derived()->cur_calling_convention();
    {
        func_reg_save_off = this->assembler.text_cur_off();

        u32 reg_save_size = 0u;
        for (const auto reg : call_conv.callee_saved_regs()) {
            // need special handling for xmm saves if
            // they are needed
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

    if (this->adaptor->cur_is_vararg()) {
        reg_save_frame_off = 8u * call_conv.callee_saved_regs().size() + 176;
        auto mem = FE_MEM(FE_BP, 0, FE_NOREG, -(i32)reg_save_frame_off);
        ASM(MOV64mr, mem, FE_DI);
        mem.off += 8;
        ASM(MOV64mr, mem, FE_SI);
        mem.off += 8;
        ASM(MOV64mr, mem, FE_DX);
        mem.off += 8;
        ASM(MOV64mr, mem, FE_CX);
        mem.off += 8;
        ASM(MOV64mr, mem, FE_R8);
        mem.off += 8;
        ASM(MOV64mr, mem, FE_R9);
        auto skip_fp = this->assembler.label_create();
        ASM(TEST8rr, FE_AX, FE_AX);
        generate_raw_jump(Jump::je, skip_fp);
        mem.off += 8;
        ASM(SSE_MOVDQUmr, mem, FE_XMM0);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM1);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM2);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM3);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM4);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM5);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM6);
        mem.off += 16;
        ASM(SSE_MOVDQUmr, mem, FE_XMM7);
        this->assembler.label_place(skip_fp);
    }

    call_conv.handle_func_args(this);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::finish_func() noexcept {
    const CallingConv conv = Base::derived()->cur_calling_convention();

    auto *write_ptr = this->assembler.sec_text.data.data() + func_reg_save_off;
    const u64 saved_regs =
        this->register_file.clobbered & conv.callee_saved_mask();
    u32 num_saved_regs = 0u;
    for (auto reg : util::BitSetIterator{saved_regs}) {
        assert(reg <= AsmReg::R15);
        write_ptr +=
            fe64_PUSHr(write_ptr, 0, AsmReg{static_cast<AsmReg::REG>(reg)});
        ++num_saved_regs;
    }

    // The frame_size contains the reserved frame size so we need to subtract
    // the stack space we used for the saved registers
    const auto final_frame_size =
        util::align_up(this->stack.frame_size, 16) - num_saved_regs * 8;
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

    // nop out the rest
    const auto reg_save_end = this->assembler.sec_text.data.data()
                              + func_reg_save_off + func_reg_save_alloc;
    assert(reg_save_end >= write_ptr);
    const u32 nop_len = reg_save_end - write_ptr;
    if (nop_len) {
        fe64_NOP(write_ptr, nop_len);
    }

    if (this->adaptor->cur_needs_unwind_info()) {
        // we need to patch the landing pad labels into their actual offsets
        for (auto &info : this->assembler.except_call_site_table) {
            if (info.pad_label_or_off == ~0u) {
                info.pad_label_or_off =
                    0; // special marker for resume/normal calls where we dont
                       // have a landing pad
                continue;
            }
            info.pad_label_or_off =
                this->assembler.label_offset(
                    static_cast<Assembler::Label>(info.pad_label_or_off))
                - func_start_off;
        }
    } else {
        assert(this->assembler.except_call_site_table.empty());
    }

    if (func_ret_offs.empty()) {
        // TODO(ts): honor cur_needs_unwind_info
        this->assembler.end_func(saved_regs);
        return;
    }

    auto *text_data     = this->assembler.sec_text.data.data();
    u32   first_ret_off = func_ret_offs[0];
    u32   ret_size      = 0;
    u32   epilogue_size = 7 + 1 + 1 + func_reg_restore_alloc; // add + pop + ret
    u32   func_end_ret_off = this->assembler.text_cur_off() - epilogue_size;
    {
        write_ptr            = text_data + first_ret_off;
        const auto ret_start = write_ptr;
        if (this->adaptor->cur_has_dynamic_alloca()) {
            if (num_saved_regs == 0) {
                write_ptr += fe64_MOV64rr(write_ptr, 0, FE_SP, FE_BP);
            } else {
                write_ptr += fe64_LEA64rm(
                    write_ptr,
                    0,
                    FE_SP,
                    FE_MEM(FE_BP, 0, FE_NOREG, -(i32)num_saved_regs * 8));
            }
        } else {
            write_ptr += fe64_ADD64ri(write_ptr, 0, FE_SP, final_frame_size);
        }
        for (auto reg : util::BitSetIterator<true>{
                 this->register_file.clobbered & conv.callee_saved_mask()}) {
            assert(reg <= AsmReg::R15);
            write_ptr +=
                fe64_POPr(write_ptr, 0, AsmReg{static_cast<AsmReg::REG>(reg)});
        }
        write_ptr += fe64_POPr(write_ptr, 0, FE_BP);
        write_ptr += fe64_RET(write_ptr, 0);
        ret_size   = write_ptr - ret_start;
        assert(ret_size <= epilogue_size && "function epilogue too long");

        // write NOP for better disassembly
        if (epilogue_size > ret_size) {
            fe64_NOP(write_ptr, epilogue_size - ret_size);
            if (first_ret_off == func_end_ret_off) {
                this->assembler.text_write_ptr -= epilogue_size - ret_size;
            }
        }
    }

    for (u32 i = 1; i < func_ret_offs.size(); ++i) {
        std::memcpy(text_data + func_ret_offs[i],
                    text_data + first_ret_off,
                    epilogue_size);
        if (func_ret_offs[i] == func_end_ret_off) {
            this->assembler.text_write_ptr -= epilogue_size - ret_size;
        }
    }

    // Do end_func at the very end; we shorten the function here again, so only
    // at this point we know the actual size of the function.
    // TODO(ts): honor cur_needs_unwind_info
    this->assembler.end_func(saved_regs);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy,
          typename Config>
u32 CompilerX64<Adaptor, Derived, BaseTy, Config>::
    func_reserved_frame_size() noexcept {
    const CallingConv call_conv     = derived()->cur_calling_convention();
    // when indexing into the stack frame, the code needs to skip over the saved
    // registers and the reg-save area if it exists
    u32               reserved_size = call_conv.callee_saved_regs().size() * 8;
    if (this->adaptor->cur_is_vararg()) {
        reserved_size += 176;
    }
    return reserved_size;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::
    reset_register_file() noexcept {
    const CallingConv conv            = derived()->cur_calling_convention();
    this->register_file.fixed         = this->register_file.used =
        this->register_file.clobbered = 0;
    this->register_file.free          = conv.initial_free_regs();
    this->register_file.clocks[0]     = 0;
    this->register_file.clocks[1]     = 0;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::reset() noexcept {
    func_ret_offs.clear();
    personality_syms.clear();
    Base::reset();
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::gen_func_epilog() noexcept {
    // epilogue:
    // if !func_has_dynamic_alloca:
    //   add rsp, #<frame_size>+<largest_call_frame_usage>
    // else:
    //   lea rsp, [rbp - <size_of_reg_save_area>]
    // for each saved reg:
    //   pop <reg>
    // pop rbp
    // ret
    //
    // however, since we will later patch this, we only
    // reserve the space for now

    func_ret_offs.push_back(this->assembler.text_cur_off());

    // add reg, imm32
    // and
    // lea rsp, [rbp - imm32]
    // both take 7 bytes
    u32 epilogue_size =
        7 + 1 + 1
        + func_reg_restore_alloc; // add/lea + pop + ret + size of reg restore

    this->assembler.text_ensure_space(epilogue_size);
    this->assembler.text_write_ptr += epilogue_size;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::spill_reg(
    const AsmReg reg, const u32 frame_off, const u32 size) noexcept {
    this->assembler.text_ensure_space(16);
    assert(-static_cast<i32>(frame_off) < 0);
    const auto mem = FE_MEM(FE_BP, 0, FE_NOREG, -static_cast<i32>(frame_off));
    if (reg.id() <= AsmReg::R15) {
        switch (size) {
        case 1: ASMNC(MOV8mr, mem, reg); break;
        case 2: ASMNC(MOV16mr, mem, reg); break;
        case 4: ASMNC(MOV32mr, mem, reg); break;
        case 8: ASMNC(MOV64mr, mem, reg); break;
        default: assert(0); __builtin_unreachable();
        }
        return;
    }

    switch (size) {
    case 4: ASMNC(SSE_MOVD_X2Gmr, mem, reg); break;
    case 8: ASMNC(SSE_MOVQ_X2Gmr, mem, reg); break;
    case 16:
        ASMNC(SSE_MOVAPDmr, mem, reg);
        break;
        // TODO(ts): 32/64 with feature flag?
    case 1: assert(0);
    case 2: assert(0);
    default: assert(0); __builtin_unreachable();
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::load_from_stack(
    const AsmReg dst,
    const i32    frame_off,
    const u32    size,
    const bool   sign_extend) noexcept {
    this->assembler.text_ensure_space(16);
    // assert(-static_cast<i32>(frame_off) < 0);
    const auto mem = FE_MEM(FE_BP, 0, FE_NOREG, -static_cast<i32>(frame_off));

    if (dst.id() <= AsmReg::R15) {
        if (!sign_extend) {
            switch (size) {
            case 1: ASMNC(MOVZXr32m8, dst, mem); break;
            case 2: ASMNC(MOVZXr32m16, dst, mem); break;
            case 4: ASMNC(MOV32rm, dst, mem); break;
            case 8: ASMNC(MOV64rm, dst, mem); break;
            default: assert(0); __builtin_unreachable();
            }
        } else {
            switch (size) {
            case 1: ASMNC(MOVSXr64m8, dst, mem); break;
            case 2: ASMNC(MOVSXr64m16, dst, mem); break;
            case 4: ASMNC(MOVSXr64m32, dst, mem); break;
            case 8: ASMNC(MOV64rm, dst, mem); break;
            default: assert(0); __builtin_unreachable();
            }
        }
        return;
    }

    assert(!sign_extend);

    switch (size) {
    case 4: ASMNC(SSE_MOVD_G2Xrm, dst, mem); break;
    case 8: ASMNC(SSE_MOVQ_G2Xrm, dst, mem); break;
    case 16:
        ASMNC(SSE_MOVAPDrm, dst, mem);
        break;
        // TODO(ts): 32/64 with feature flag?
    case 1: assert(0);
    case 2: assert(0);
    default: assert(0); __builtin_unreachable();
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::
    load_address_of_var_reference(const AsmReg            dst,
                                  const AssignmentPartRef ap) noexcept {
    static_assert(Config::DEFAULT_VAR_REF_HANDLING);
    assert(-static_cast<i32>(ap.assignment->frame_off) < 0);
    // per-default, variable references are only used by
    // allocas
    ASM(LEA64rm,
        dst,
        FE_MEM(
            FE_BP, 0, FE_NOREG, -static_cast<i32>(ap.assignment->frame_off)));
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::mov(
    const AsmReg dst, const AsmReg src, const u32 size) noexcept {
    assert(dst.valid());
    assert(src.valid());
    if (dst.id() <= AsmReg::R15 && src.id() <= AsmReg::R15) {
        if (size > 4) {
            ASM(MOV64rr, dst, src);
        } else {
            ASM(MOV32rr, dst, src);
        }
    } else if (dst.id() >= AsmReg::XMM0 && src.id() >= AsmReg::XMM0) {
        if (size <= 16) {
            if (dst.id() > AsmReg::XMM15 || src.id() > AsmReg::XMM15) {
                assert(has_cpu_feats(CPU_AVX512F));
                ASM(VMOVAPD128rr, dst, src);
            } else {
                ASM(SSE_MOVAPDrr, dst, src);
            }
        } else if (size <= 32) {
            assert(has_cpu_feats(CPU_AVX));
            assert((dst.id() <= AsmReg::XMM15 && src.id() <= AsmReg::XMM15)
                   || has_cpu_feats(CPU_AVX512F));
            ASM(VMOVAPD256rr, dst, src);
        } else {
            assert(size <= 64);
            assert(has_cpu_feats(CPU_AVX512F));
            ASM(VMOVAPD512rr, dst, src);
        }
    } else if (dst.id() <= AsmReg::R15) {
        // gp<-xmm
        assert(src.id() >= AsmReg::XMM0);
        assert(size <= 8);
        if (src.id() > AsmReg::XMM15) {
            assert(has_cpu_feats(CPU_AVX512F));
            if (size <= 4) {
                ASM(VMOVD_X2Grr, dst, src);
            } else {
                ASM(VMOVQ_X2Grr, dst, src);
            }
        } else {
            if (size <= 4) {
                ASM(SSE_MOVD_X2Grr, dst, src);
            } else {
                ASM(SSE_MOVQ_X2Grr, dst, src);
            }
        }
    } else {
        // xmm<-gp
        assert(src.id() <= AsmReg::R15);
        assert(dst.id() >= AsmReg::XMM0);
        assert(size <= 8);
        if (dst.id() > AsmReg::XMM15) {
            assert(has_cpu_feats(CPU_AVX512F));
            if (size <= 4) {
                ASM(VMOVD_G2Xrr, dst, src);
            } else {
                ASM(VMOVQ_G2Xrr, dst, src);
            }
        } else {
            if (size <= 4) {
                ASM(SSE_MOVD_G2Xrr, dst, src);
            } else {
                ASM(SSE_MOVQ_G2Xrr, dst, src);
            }
        }
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
AsmReg CompilerX64<Adaptor, Derived, BaseTy, Config>::gval_expr_as_reg(
    GenericValuePart &gv) noexcept {
    auto &expr = std::get<typename GenericValuePart::Expr>(gv.state);

    ScratchReg scratch{derived()};
    bool disp32 = i32(expr.disp) == expr.disp;
    AsmReg base = expr.has_base() ? expr.base_reg() : AsmReg::make_invalid();
    AsmReg idx = expr.has_index() ? expr.index_reg() : AsmReg::make_invalid();
    if (std::holds_alternative<ScratchReg>(expr.base)) {
        scratch = std::move(std::get<ScratchReg>(expr.base));
    } else if (std::holds_alternative<ScratchReg>(expr.index)) {
        scratch = std::move(std::get<ScratchReg>(expr.index));
    } else {
        (void)scratch.alloc_gp();
    }
    auto dst = scratch.cur_reg;
    if (idx.valid()) {
        if ((expr.scale & (expr.scale - 1)) == 0 && expr.scale < 16) {
            u8 sc = expr.scale;
            if (base.valid() && disp32) {
                ASM(LEA64rm, dst, FE_MEM(base, sc, idx, i32(expr.disp)));
                expr.disp = 0;
            } else if (base.valid()) {
                ASM(LEA64rm, dst, FE_MEM(base, sc, idx, 0));
            } else if (disp32) {
                ASM(LEA64rm, dst, FE_MEM(FE_NOREG, sc, idx, i32(expr.disp)));
            } else {
                ASM(LEA64rm, dst, FE_MEM(FE_NOREG, sc, idx, 0));
            }
        } else {
            u64 scale = expr.scale;
            if (base == idx) {
                base = AsmReg::make_invalid();
                scale += 1;
            }

            ScratchReg idx_scratch{derived()};
            // We need a register to compute the scaled index.
            AsmReg idx_tmp = dst;
            if (dst == base && std::holds_alternative<ScratchReg>(expr.index)) {
                // We can't use dst, it'd clobber base, so use the other
                // register we currently own.
                idx_tmp = std::get<ScratchReg>(expr.index).cur_reg;
            } else if (dst == base) {
                idx_tmp = idx_scratch.alloc_gp();
            }

            if ((scale & (scale - 1)) == 0) {
                if (idx_tmp != idx) {
                    ASM(MOV64rr, idx_tmp, idx);
                }
                ASM(SHL64ri, idx_tmp, util::cnt_tz(scale));
            } else {
                if (i32(scale) == i64(scale)) {
                    ASM(IMUL64rri, idx_tmp, idx, scale);
                } else {
                    ScratchReg scratch2{derived()};
                    auto tmp2 = scratch2.alloc_gp();
                    ASM(MOV64ri, tmp2, scale);
                    if (idx_tmp != idx) {
                        ASM(MOV64rr, idx_tmp, idx);
                    }
                    ASM(IMUL64rr, idx_tmp, tmp2);
                }
            }
            if (base.valid()) {
                if (disp32 || (idx_tmp != dst && base != dst)) {
                    ASM(LEA64rm, dst, FE_MEM(base, 1, idx_tmp, i32(expr.disp)));
                    expr.disp = 0;
                } else if (dst == base) {
                    ASM(ADD64rr, dst, idx_tmp);
                } else {
                    ASM(ADD64rr, dst, base);
                }
            }
        }
    } else if (base.valid()) {
        if (expr.disp && disp32) {
            ASM(LEA64rm, dst, FE_MEM(base, 0, FE_NOREG, i32(expr.disp)));
            expr.disp = 0;
        } else if (dst != base) {
            ASM(MOV64rr, dst, base);
        }
    }
    if (expr.disp) {
        ScratchReg scratch2{derived()};
        auto tmp2 = scratch2.alloc_gp();
        ASM(MOV64ri, tmp2, expr.disp);
        ASM(ADD64rr, dst, tmp2);
    }
    gv.state = std::move(scratch);
    return dst;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    ValuePartRef &val_ref, const AsmReg dst) noexcept {
    assert(val_ref.is_const);
    const auto &data = val_ref.state.c;
    materialize_constant(data.const_data, data.bank, data.size, dst);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    ValuePartRef &val_ref, ScratchReg &dst) noexcept {
    assert(val_ref.is_const);
    materialize_constant(val_ref, dst.alloc(val_ref.state.c.bank));
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    const std::array<u8, 64> &data,
    const u32                 bank,
    const u32                 size,
    AsmReg                    dst) noexcept {
    const auto const_u64 = *reinterpret_cast<const u64 *>(data.data());
    if (bank == 0) {
        assert(size <= 8);
        if (const_u64 == 0) {
            // note: cannot use XOR here since this might be called in-between
            // instructions that rely on the flags being preserved
            // ASM(XOR32rr, dst, dst);
            ASM(MOV32ri, dst, 0);
            return;
        }

        if (size <= 4) {
            ASM(MOV32ri, dst, const_u64);
        } else {
            ASM(MOV64ri, dst, const_u64);
        }
        return;
    }

    assert(bank == 1);
    if (size <= 8 && const_u64 == 0) {
        if (has_cpu_feats(CPU_AVX)) {
            ASM(VPXOR128rrr, dst, dst, dst);
        } else {
            ASM(SSE_PXORrr, dst, dst);
        }
        return;
    }

    if (size == 4) {
        ScratchReg tmp{derived()};
        auto tmp_reg = tmp.alloc_gp();
        ASM(MOV32ri, tmp_reg, const_u64);
        if (has_cpu_feats(CPU_AVX)) {
            ASM(VMOVD_G2Xrr, dst, tmp_reg);
        } else {
            ASM(SSE_MOVD_G2Xrr, dst, tmp_reg);
        }
        return;
    }

    if (size == 8) {
        ScratchReg tmp{derived()};
        auto tmp_reg = tmp.alloc_gp();
        ASM(MOV64ri, tmp_reg, const_u64);
        if (has_cpu_feats(CPU_AVX)) {
            ASM(VMOVQ_G2Xrr, dst, tmp_reg);
        } else {
            ASM(SSE_MOVQ_G2Xrr, dst, tmp_reg);
        }
        return;
    }

    // TODO(ts): have some facility to use a constant pool
    assert(0);
    exit(1);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
AsmReg
    CompilerX64<Adaptor, Derived, BaseTy, Config>::select_fixed_assignment_reg(
        const u32 bank, IRValueRef) noexcept {
    assert(bank <= 1);
    u64 reg_mask = (1ull << 32) - 1;
    if (bank != 0) {
        reg_mask = ~reg_mask;
    }
    reg_mask &= ~fixed_assignment_nonallocatable_mask;

    const auto find_possible_regs =
        [this, reg_mask](const u64 preferred_regs) -> u64 {
        // try to first get an unused reg, otherwise an unfixed reg
        u64 possible_regs =
            this->register_file.free & preferred_regs & reg_mask;
        if (possible_regs == 0) {
            possible_regs =
                (this->register_file.used & ~this->register_file.fixed)
                & preferred_regs & reg_mask;
        }
        return possible_regs;
    };

    u64        possible_regs;
    const auto call_conv = derived()->cur_calling_convention();
    if (derived()->cur_func_may_emit_calls()) {
        // we can only allocated fixed assignments from the callee-saved regs
        const u64 preferred_regs = call_conv.callee_saved_mask();
        possible_regs            = find_possible_regs(preferred_regs);
    } else {
        // try allocating any non-callee saved register first, except the result
        // registers
        u64 preferred_regs =
            ~call_conv.result_regs_mask() & ~call_conv.callee_saved_mask();
        possible_regs = find_possible_regs(preferred_regs);
        if (possible_regs == 0) {
            // otherwise fallback to callee-saved regs
            preferred_regs =
                derived()->cur_calling_convention().callee_saved_mask();
            possible_regs = find_possible_regs(preferred_regs);
        }
    }

    if (possible_regs == 0) {
        return AsmReg::make_invalid();
    }

    // try to first get an unused reg, otherwise an unfixed reg
    if ((possible_regs & this->register_file.free) != 0) {
        return AsmReg{util::cnt_tz(possible_regs)};
    }

    for (const auto reg_id : util::BitSetIterator<>{possible_regs}) {
        const auto reg = AsmReg{reg_id};

        if (this->register_file.is_fixed(reg)) {
            continue;
        }

        const auto local_idx = this->register_file.reg_local_idx(reg);
        const auto part      = this->register_file.reg_part(reg);

        if (local_idx == Base::INVALID_VAL_LOCAL_IDX) {
            continue;
        }
        auto *assignment = this->val_assignment(local_idx);
        auto  ap         = AssignmentPartRef{assignment, part};
        if (ap.modified()) {
            continue;
        }

        return reg;
    }

    return AsmReg::make_invalid();
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
typename CompilerX64<Adaptor, Derived, BaseTy, Config>::Jump
    CompilerX64<Adaptor, Derived, BaseTy, Config>::invert_jump(
        Jump jmp) noexcept {
    switch (jmp) {
    case Jump::ja: return Jump::jbe;
    case Jump::jae: return Jump::jb;
    case Jump::jb: return Jump::jae;
    case Jump::jbe: return Jump::ja;
    case Jump::je: return Jump::jne;
    case Jump::jg: return Jump::jle;
    case Jump::jge: return Jump::jl;
    case Jump::jl: return Jump::jge;
    case Jump::jle: return Jump::jg;
    case Jump::jmp: return Jump::jmp;
    case Jump::jne: return Jump::je;
    case Jump::jno: return Jump::jo;
    case Jump::jo: return Jump::jno;
    case Jump::js: return Jump::jns;
    case Jump::jns: return Jump::js;
    case Jump::jp: return Jump::jnp;
    case Jump::jnp: return Jump::jp;
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy,
          typename Config>
typename CompilerX64<Adaptor, Derived, BaseTy, Config>::Jump
    CompilerX64<Adaptor, Derived, BaseTy, Config>::swap_jump(
        Jump jmp) noexcept {
    switch (jmp) {
    case Jump::ja: return Jump::jb;
    case Jump::jae: return Jump::jbe;
    case Jump::jb: return Jump::ja;
    case Jump::jbe: return Jump::jae;
    case Jump::je: return Jump::je;
    case Jump::jg: return Jump::jl;
    case Jump::jge: return Jump::jle;
    case Jump::jl: return Jump::jg;
    case Jump::jle: return Jump::jge;
    case Jump::jmp: return Jump::jmp;
    case Jump::jne: return Jump::jne;
    case Jump::jno: return Jump::jno;
    case Jump::jo: return Jump::jo;
    case Jump::js: return Jump::js;
    case Jump::jns: return Jump::jns;
    case Jump::jp: return Jump::jp;
    case Jump::jnp: return Jump::jnp;
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::generate_branch_to_block(
    const Jump jmp,
    IRBlockRef target,
    const bool needs_split,
    const bool last_inst) noexcept {
    const auto target_idx = this->analyzer.block_idx(target);
    if (!needs_split || jmp == Jump::jmp) {
        this->derived()->move_to_phi_nodes(target_idx);

        if (!last_inst
            || this->analyzer.block_idx(target) != this->next_block()) {
            generate_raw_jump(jmp, this->block_labels[(u32)target_idx]);
        }
    } else {
        auto tmp_label = this->assembler.label_create();
        generate_raw_jump(invert_jump(jmp), tmp_label);

        this->derived()->move_to_phi_nodes(target_idx);

        generate_raw_jump(Jump::jmp, this->block_labels[(u32)target_idx]);

        this->assembler.label_place(tmp_label);
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::generate_raw_jump(
    Jump jmp, Assembler::Label target_label) noexcept {
    if (this->assembler.label_is_pending(target_label)) {
        this->assembler.text_ensure_space(6);
        auto *target = this->assembler.text_write_ptr;
        switch (jmp) {
        case Jump::ja: ASMNCF(JA, FE_JMPL, target); break;
        case Jump::jae: ASMNCF(JNC, FE_JMPL, target); break;
        case Jump::jb: ASMNCF(JC, FE_JMPL, target); break;
        case Jump::jbe: ASMNCF(JBE, FE_JMPL, target); break;
        case Jump::je: ASMNCF(JZ, FE_JMPL, target); break;
        case Jump::jg: ASMNCF(JG, FE_JMPL, target); break;
        case Jump::jge: ASMNCF(JGE, FE_JMPL, target); break;
        case Jump::jl: ASMNCF(JL, FE_JMPL, target); break;
        case Jump::jle: ASMNCF(JLE, FE_JMPL, target); break;
        case Jump::jmp: ASMNCF(JMP, FE_JMPL, target); break;
        case Jump::jne: ASMNCF(JNZ, FE_JMPL, target); break;
        case Jump::jno: ASMNCF(JNO, FE_JMPL, target); break;
        case Jump::jo: ASMNCF(JO, FE_JMPL, target); break;
        case Jump::js: ASMNCF(JS, FE_JMPL, target); break;
        case Jump::jns: ASMNCF(JNS, FE_JMPL, target); break;
        case Jump::jp: ASMNCF(JP, FE_JMPL, target); break;
        case Jump::jnp: ASMNCF(JNP, FE_JMPL, target); break;
        }

        this->assembler.label_add_unresolved_jump_offset(
            target_label, this->assembler.text_cur_off() - 4);
    } else {
        this->assembler.text_ensure_space(6);
        auto *target = this->assembler.sec_text.data.data()
                       + this->assembler.label_offset(target_label);
        switch (jmp) {
        case Jump::ja: ASMNC(JA, target); break;
        case Jump::jae: ASMNC(JNC, target); break;
        case Jump::jb: ASMNC(JC, target); break;
        case Jump::jbe: ASMNC(JBE, target); break;
        case Jump::je: ASMNC(JZ, target); break;
        case Jump::jg: ASMNC(JG, target); break;
        case Jump::jge: ASMNC(JGE, target); break;
        case Jump::jl: ASMNC(JL, target); break;
        case Jump::jle: ASMNC(JLE, target); break;
        case Jump::jmp: ASMNC(JMP, target); break;
        case Jump::jne: ASMNC(JNZ, target); break;
        case Jump::jno: ASMNC(JNO, target); break;
        case Jump::jo: ASMNC(JO, target); break;
        case Jump::js: ASMNC(JS, target); break;
        case Jump::jns: ASMNC(JNS, target); break;
        case Jump::jp: ASMNC(JP, target); break;
        case Jump::jnp: ASMNC(JNP, target); break;
        }
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::generate_raw_set(
    Jump jmp, AsmReg dst) noexcept {
    ASM(MOV32ri, dst, 0);
    switch (jmp) {
    case Jump::ja: ASM(SETA8r, dst); break;
    case Jump::jae: ASM(SETNC8r, dst); break;
    case Jump::jb: ASM(SETC8r, dst); break;
    case Jump::jbe: ASM(SETBE8r, dst); break;
    case Jump::je: ASM(SETZ8r, dst); break;
    case Jump::jg: ASM(SETG8r, dst); break;
    case Jump::jge: ASM(SETGE8r, dst); break;
    case Jump::jl: ASM(SETL8r, dst); break;
    case Jump::jle: ASM(SETLE8r, dst); break;
    case Jump::jmp: ASM(MOV32ri, dst, 1); break;
    case Jump::jne: ASM(SETNZ8r, dst); break;
    case Jump::jno: ASM(SETNO8r, dst); break;
    case Jump::jo: ASM(SETO8r, dst); break;
    case Jump::js: ASM(SETS8r, dst); break;
    case Jump::jns: ASM(SETNS8r, dst); break;
    case Jump::jp: ASM(SETP8r, dst); break;
    case Jump::jnp: ASM(SETNP8r, dst); break;
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::spill_before_call(
    const CallingConv calling_conv, const u64 except_mask) {
    for (auto reg_id : util::BitSetIterator<>{
             this->register_file.used & ~calling_conv.callee_saved_mask()
             & ~except_mask}) {
        auto reg = AsmReg{reg_id};
        assert(!this->register_file.is_fixed(reg));
        assert(this->register_file.reg_local_idx(reg)
               != Base::INVALID_VAL_LOCAL_IDX);

        auto ap = AssignmentPartRef{
            this->val_assignment(this->register_file.reg_local_idx(reg)),
            this->register_file.reg_part(reg)};
        assert(ap.register_valid());
        assert(ap.full_reg_id() == reg_id);

        ap.spill_if_needed(this);
        this->register_file.unmark_used(reg);
        ap.set_register_valid(false);
    }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy,
          typename Config>
void CompilerX64<Adaptor, Derived, BaseTy, Config>::generate_call(
    std::variant<Assembler::SymRef, ScratchReg, ValuePartRef>      &&target,
    std::span<CallArg>                                               arguments,
    std::span<std::variant<ValuePartRef, std::pair<ScratchReg, u8>>> results,
    CallingConv calling_conv,
    const bool  variable_args) {
    if (std::holds_alternative<ScratchReg>(target)) {
        assert(((1ull << std::get<ScratchReg>(target).cur_reg.id())
                & calling_conv.arg_regs_mask())
               == 0);
    }

    // in case results are locked, we unlock them here
    // TODO(ts): can that actually happen?
    for (auto &res : results) {
        if (std::holds_alternative<ValuePartRef>(res)) {
            std::get<ValuePartRef>(res).unlock();
        } else {
            std::get<std::pair<ScratchReg, u8>>(res).first.reset();
        }
    }

    const auto stack_space_used = util::align_up(
        calling_conv.calculate_call_stack_space(this, arguments), 16);

    if (stack_space_used) {
        ASM(SUB64ri, FE_SP, stack_space_used);
    }

    const auto vec_arg_count = calling_conv.handle_call_args(this, arguments);

    u64 except_mask = 0;
    if (std::holds_alternative<ScratchReg>(target)) {
        except_mask = (1ull << std::get<ScratchReg>(target).cur_reg.id());
    } else if (std::holds_alternative<ValuePartRef>(target)) {
        auto &ref = std::get<ValuePartRef>(target);
        if (ref.assignment().register_valid()) {
            except_mask = (1ull << ref.assignment().full_reg_id());
        }
    }
    spill_before_call(calling_conv, except_mask);

    ScratchReg vararg_scratch{derived()};
    if (variable_args) {
        // make sure the call argument is not in AX
        vararg_scratch.alloc_specific(AsmReg::AX);
        ASM(MOV32ri, FE_AX, vec_arg_count);
    }

    if (std::holds_alternative<Assembler::SymRef>(target)) {
        this->assembler.text_ensure_space(8);
        auto *target_ptr = this->assembler.text_write_ptr;
        ASMNC(CALL, target_ptr);
        this->assembler.reloc_text_plt32(std::get<Assembler::SymRef>(target),
                                         this->assembler.text_cur_off() - 4);
    } else if (std::holds_alternative<ScratchReg>(target)) {
        auto &reg = std::get<ScratchReg>(target);
        assert(!reg.cur_reg.invalid());
        ASM(CALLr, reg.cur_reg);

        if (((1ull << reg.cur_reg.id()) & calling_conv.callee_saved_mask())
            == 0) {
            reg.reset();
        }
    } else {
        assert(std::holds_alternative<ValuePartRef>(target));
        auto &ref = std::get<ValuePartRef>(target);
        assert(((1ull << AsmReg::R10)
                & (calling_conv.callee_saved_mask()
                   | calling_conv.arg_regs_mask()))
               == 0);
        if (ref.is_const) {
            this->register_file.clobbered |= (1ull << AsmReg::R10);
            ASM(CALLr, ref.reload_into_specific(derived(), AsmReg::R10));
        } else {
            auto ap = ref.assignment();
            if (ap.register_valid()) {
                auto reg = AsmReg{ap.full_reg_id()};
                ASM(CALLr, reg);
            } else if (!ap.variable_ref()) {
                assert(static_cast<i32>(-ap.frame_off()) < 0);
                ASM(CALLm, FE_MEM(FE_BP, 0, FE_NOREG, (i32)-ap.frame_off()));
            } else {
                this->register_file.clobbered |= (1ull << AsmReg::R10);
                ref.reload_into_specific(derived(), AsmReg::R10);
                ASM(CALLr, FE_R10);
            }

            if (ap.register_valid()
                && ((1ull << ap.full_reg_id())
                    & calling_conv.callee_saved_mask())
                       == 0) {
                ref.spill();
                ref.unlock();
                auto reg = AsmReg{ap.full_reg_id()};
                this->register_file.unmark_used(reg);
                ap.set_register_valid(false);
            }
        }
    }

    vararg_scratch.reset();

    if (stack_space_used) {
        ASM(ADD64ri, FE_SP, stack_space_used);
    }

    calling_conv.fill_call_results(this, results);
}

} // namespace tpde::x64
