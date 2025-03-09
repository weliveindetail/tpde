// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "AssemblerElfA64.hpp"
#include "tpde/CompilerBase.hpp"
#include "tpde/base.hpp"

#include <disarm64.h>
#include <elf.h>

// Helper macros for assembling in the compiler
#if defined(ASM) || defined(ASME) || defined(ASMNC) || defined(ASMC)
  #error Got definition for ASM macros from somewhere else. Maybe you included compilers for multiple architectures?
#endif

#define ASM(op, ...)                                                           \
  do {                                                                         \
    this->assembler.text_ensure_space(4);                                      \
    u32 inst = de64_##op(__VA_ARGS__);                                         \
    assert(inst != 0);                                                         \
    *reinterpret_cast<u32 *>(this->assembler.text_write_ptr) = inst;           \
    this->assembler.text_write_ptr += 4;                                       \
  } while (false)

// generate instruction and reserve a custom amount of bytes
#define ASME(bytes, op, ...)                                                   \
  do {                                                                         \
    assert(bytes >= 4);                                                        \
    this->assembler.text_ensure_space(bytes);                                  \
    u32 inst = de64_##op(__VA_ARGS__);                                         \
    assert(inst != 0);                                                         \
    *reinterpret_cast<u32 *>(this->assembler.text_write_ptr) = inst;           \
    this->assembler.text_write_ptr += 4;                                       \
  } while (false)

// generate an instruction without checking that enough space is available
#define ASMNC(op, ...)                                                         \
  do {                                                                         \
    u32 inst = de64_##op(__VA_ARGS__);                                         \
    assert(inst != 0);                                                         \
    assert(this->assembler.text_reserve_end -                                  \
               this->assembler.text_write_ptr >=                               \
           4);                                                                 \
    *reinterpret_cast<u32 *>(this->assembler.text_write_ptr) = inst;           \
    this->assembler.text_write_ptr += 4;                                       \
  } while (false)

// generate an instruction with a custom compiler ptr
#define ASMC(compiler, op, ...)                                                \
  do {                                                                         \
    compiler->assembler.text_ensure_space(4);                                  \
    u32 inst = de64_##op(__VA_ARGS__);                                         \
    assert(inst != 0);                                                         \
    *reinterpret_cast<u32 *>(compiler->assembler.text_write_ptr) = inst;       \
    compiler->assembler.text_write_ptr += 4;                                   \
  } while (false)

// check if the instruction could be successfully encoded with custom compiler
#define ASMIFC(compiler, op, ...)                                              \
  (([&]() -> bool {                                                            \
    compiler->assembler.text_ensure_space(4);                                  \
    u32 inst = de64_##op(__VA_ARGS__);                                         \
    if (inst == 0)                                                             \
      return false;                                                            \
    *reinterpret_cast<u32 *>(compiler->assembler.text_write_ptr) = inst;       \
    compiler->assembler.text_write_ptr += 4;                                   \
    return true;                                                               \
  })())
// check if the instruction could be successfully encoded
#define ASMIF(...) ASMIFC(this, __VA_ARGS__)

namespace tpde::a64 {

struct AsmReg : AsmRegBase {
  enum REG : u8 {
    R0 = 0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    R16,
    R17,
    R18,
    R19,
    R20,
    R21,
    R22,
    R23,
    R24,
    R25,
    R26,
    R27,
    R28,
    R29,
    FP = 29,
    R30,
    LR = 30,
    SP = 31,

    V0 = 32,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
    V8,
    V9,
    V10,
    V11,
    V12,
    V13,
    V14,
    V15,
    V16,
    V17,
    V18,
    V19,
    V20,
    V21,
    V22,
    V23,
    V24,
    V25,
    V26,
    V27,
    V28,
    V29,
    V30,
    V31
  };

  constexpr explicit AsmReg() noexcept : AsmRegBase((u8)0xFF) {}

  constexpr AsmReg(const REG id) noexcept : AsmRegBase((u8)id) {}

  constexpr AsmReg(const AsmRegBase base) noexcept : AsmRegBase(base) {}

  constexpr explicit AsmReg(const u8 id) noexcept : AsmRegBase(id) {
    assert(id <= SP || (id >= V0 && id <= V31));
  }

  constexpr explicit AsmReg(const u64 id) noexcept : AsmRegBase(id) {
    assert(id <= SP || (id >= V0 && id <= V31));
  }

  operator DA_GReg() const noexcept {
    assert(reg_id < V0);
    return DA_GReg{reg_id};
  }

  operator DA_GRegZR() const noexcept {
    assert(reg_id < V0);
    assert(reg_id != SP); // 31 means SP in our enums
    return DA_GRegZR{reg_id};
  }

  operator DA_GRegSP() const noexcept {
    assert(reg_id <= SP);
    return DA_GRegSP{reg_id};
  }

  operator DA_VReg() const noexcept {
    assert(reg_id >= V0 && reg_id <= V31);
    return DA_VReg{static_cast<u8>(reg_id - V0)};
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
  using Assembler = AssemblerElfA64;
  using AsmReg = tpde::a64::AsmReg;

  static constexpr u8 GP_BANK = 0;
  static constexpr u8 FP_BANK = 1;
  static constexpr bool FRAME_INDEXING_NEGATIVE = false;
  static constexpr u32 PLATFORM_POINTER_SIZE = 8;
  static constexpr u32 NUM_BANKS = 2;
};

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy =
              CompilerBase,
          typename Config = PlatformConfig>
struct CompilerA64;

struct CallingConv {
  enum TYPE {
    SYSV_CC // TODO(ts): AAPCS?
  };

  TYPE ty;

  constexpr CallingConv(const TYPE ty) : ty(ty) {}

  [[nodiscard]] constexpr std::span<const AsmReg> arg_regs_gp() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::arg_regs_gp;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr std::span<const AsmReg>
      arg_regs_vec() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::arg_regs_vec;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr std::span<const AsmReg> ret_regs_gp() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::ret_regs_gp;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr std::span<const AsmReg>
      ret_regs_vec() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::ret_regs_vec;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr std::span<const AsmReg>
      callee_saved_regs() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::callee_saved_regs;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr u64 callee_saved_mask() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::callee_saved_mask;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr u64 arg_regs_mask() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::arg_regs_mask;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr u64 result_regs_mask() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::result_regs_mask;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr u64 initial_free_regs() const noexcept {
    switch (ty) {
    case SYSV_CC: return SysV::initial_free_regs;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  [[nodiscard]] constexpr std::optional<AsmReg> sret_reg() const noexcept {
    switch (ty) {
    case SYSV_CC: return AsmReg::R8;
    default: TPDE_UNREACHABLE("invalid calling convention");
    }
  }

  template <typename Adaptor,
            typename Derived,
            template <typename, typename, typename> typename BaseTy,
            typename Config>
  void handle_func_args(
      CompilerA64<Adaptor, Derived, BaseTy, Config> *) const noexcept;

  template <typename Adaptor,
            typename Derived,
            template <typename, typename, typename> typename BaseTy,
            typename Config>
  u32 calculate_call_stack_space(
      CompilerA64<Adaptor, Derived, BaseTy, Config> *,
      std::span<typename CompilerA64<Adaptor, Derived, BaseTy, Config>::CallArg>
          arguments) noexcept;

  /// returns the number of vector arguments
  template <typename Adaptor,
            typename Derived,
            template <typename, typename, typename> typename BaseTy,
            typename Config>
  u32 handle_call_args(
      CompilerA64<Adaptor, Derived, BaseTy, Config> *,
      std::span<typename CompilerA64<Adaptor, Derived, BaseTy, Config>::CallArg>
          arguments,
      util::SmallVector<
          typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ScratchReg,
          8> &arg_scratchs) noexcept;

  template <typename Adaptor,
            typename Derived,
            template <typename, typename, typename> typename BaseTy,
            typename Config>
  void fill_call_results(
      CompilerA64<Adaptor, Derived, BaseTy, Config> *,
      std::span<
          typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ValuePart>
          results) noexcept;

  struct SysV {
    constexpr static std::array<AsmReg, 8> arg_regs_gp{AsmReg::R0,
                                                       AsmReg::R1,
                                                       AsmReg::R2,
                                                       AsmReg::R3,
                                                       AsmReg::R4,
                                                       AsmReg::R5,
                                                       AsmReg::R6,
                                                       AsmReg::R7};

    constexpr static std::array<AsmReg, 8> arg_regs_vec{AsmReg::V0,
                                                        AsmReg::V1,
                                                        AsmReg::V2,
                                                        AsmReg::V3,
                                                        AsmReg::V4,
                                                        AsmReg::V5,
                                                        AsmReg::V6,
                                                        AsmReg::V7};

    constexpr static std::array<AsmReg, 8> ret_regs_gp = arg_regs_gp;

    constexpr static std::array<AsmReg, 8> ret_regs_vec = arg_regs_vec;

    // TODO(ts): only the low 64 bits of V8-V15 are callee-saved, either
    // treat them as non-callee saved here and have some special casing in
    // the prologue/epilogue stuff or think about smth else
    constexpr static std::array<AsmReg, 18> callee_saved_regs{
        AsmReg::R19,
        AsmReg::R20,
        AsmReg::R21,
        AsmReg::R22,
        AsmReg::R23,
        AsmReg::R24,
        AsmReg::R25,
        AsmReg::R26,
        AsmReg::R27,
        AsmReg::R28,
        AsmReg::V8,
        AsmReg::V9,
        AsmReg::V10,
        AsmReg::V11,
        AsmReg::V12,
        AsmReg::V13,
        AsmReg::V14,
        AsmReg::V15,
    };

    // we reserve SP,FP,R16 and R17 for our special use cases
    constexpr static u64 initial_free_regs =
        0xFFFF'FFFF'FFFF'FFFF &
        ~create_bitmask({AsmReg::SP, AsmReg::FP, AsmReg::R16, AsmReg::R17});

    constexpr static u64 callee_saved_mask = create_bitmask(callee_saved_regs);

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

  {
    a.arg_allow_split_reg_stack_passing(std::declval<typename T::IRValueRef>())
  } -> std::convertible_to<bool>;

  { a.cur_calling_convention() } -> SameBaseAs<CallingConv>;
};
} // namespace concepts

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
struct CompilerA64 : BaseTy<Adaptor, Derived, Config> {
  using Base = BaseTy<Adaptor, Derived, Config>;

  using IRValueRef = typename Base::IRValueRef;
  using IRBlockRef = typename Base::IRBlockRef;
  using IRFuncRef = typename Base::IRFuncRef;

  using AssignmentPartRef = typename Base::AssignmentPartRef;
  using ScratchReg = typename Base::ScratchReg;
  using ValuePartRef = typename Base::ValuePartRef;
  using ValuePart = typename Base::ValuePart;
  using GenericValuePart = typename Base::GenericValuePart;

  using Assembler = typename PlatformConfig::Assembler;
  using RegisterFile = typename Base::RegisterFile;

  using Base::derived;


  // TODO(ts): make this dependent on the number of callee-saved regs of the
  // current function or if there is a call in the function?
  static constexpr u32 NUM_FIXED_ASSIGNMENTS[PlatformConfig::NUM_BANKS] = {5,
                                                                           6};

  enum CPU_FEATURES : u32 {
    CPU_BASELINE = 0, // ARMV8.0
  };

  CPU_FEATURES cpu_feats = CPU_BASELINE;

  // When handling function arguments, we need to prevent argument registers
  // from being handed out as fixed registers
  //
  // Additionally, we prevent R0 and R1 from being fixed assignments to
  // prevent issues with exception handling
  u64 fixed_assignment_nonallocatable_mask =
      create_bitmask({AsmReg::R0, AsmReg::R1});
  u32 func_start_off = 0u, func_prologue_alloc = 0u,
      func_reg_restore_alloc = 0u;
  /// Offset to the `add sp, sp, XXX` instruction that the argument handling
  /// uses to access stack arguments if needed
  u32 func_arg_stack_add_off = ~0u;
  AsmReg func_arg_stack_add_reg = AsmReg::make_invalid();

  u32 scalar_arg_count = 0xFFFF'FFFF, vec_arg_count = 0xFFFF'FFFF;
  u32 reg_save_frame_off = 0;
  u32 var_arg_stack_off = 0;
  util::SmallVector<u32, 8> func_ret_offs = {};

  util::SmallVector<std::pair<IRFuncRef, typename Assembler::SymRef>, 4>
      personality_syms = {};

  // for now, always generate an object
  explicit CompilerA64(Adaptor *adaptor,
                       const CPU_FEATURES cpu_features = CPU_BASELINE)
      : Base{adaptor, true}, cpu_feats(cpu_features) {
    static_assert(std::is_base_of_v<CompilerA64, Derived>);
    static_assert(concepts::Compiler<Derived, PlatformConfig>);
  }

  void start_func(u32 func_idx) noexcept;

  void gen_func_prolog_and_args() noexcept;

  // note: this has to call assembler->end_func
  void finish_func(u32 func_idx) noexcept;

  u32 func_reserved_frame_size() noexcept;

  void reset() noexcept;

  // helpers

  void gen_func_epilog() noexcept;

  void
      spill_reg(const AsmReg reg, const u32 frame_off, const u32 size) noexcept;

  void load_from_stack(AsmReg dst,
                       i32 frame_off,
                       u32 size,
                       bool sign_extend = false) noexcept;

  void load_address_of_var_reference(AsmReg dst, AssignmentPartRef ap) noexcept;

  void mov(AsmReg dst, AsmReg src, u32 size) noexcept;

  GenericValuePart val_spill_slot(ValuePart &val_ref) noexcept {
    const auto ap = val_ref.assignment();
    assert(ap.stack_valid() && !ap.variable_ref());
    return typename GenericValuePart::Expr(AsmReg::R29, ap.frame_off());
  }

  AsmReg gval_expr_as_reg(GenericValuePart &gv) noexcept;

  void materialize_constant(ValuePartRef &val_ref, AsmReg dst) noexcept;
  void materialize_constant(ValuePartRef &val_ref, ScratchReg &dst) noexcept;
  void materialize_constant(const u64 *data,
                            u32 bank,
                            u32 size,
                            AsmReg dst) noexcept;
  void materialize_constant(u64 const_u64,
                            u32 bank,
                            u32 size,
                            AsmReg dst) noexcept;

  AsmReg select_fixed_assignment_reg(u32 bank, IRValueRef) noexcept;

  struct Jump {
    enum Kind : uint8_t {
      Jeq,
      Jne,
      Jcs,
      Jhs = Jcs,
      Jcc,
      Jlo = Jcc,
      Jmi,
      Jpl,
      Jvs,
      Jvc,
      Jhi,
      Jls,
      Jge,
      Jlt,
      Jgt,
      Jle,
      // TDOO: consistency
      jmp,
      Cbz,
      Cbnz,
      Tbz,
      Tbnz
    };

    Kind kind;
    AsmReg cmp_reg;
    bool cmp_is_32;
    u8 test_bit;

    constexpr Jump() : kind(Kind::jmp) {}

    constexpr Jump(Kind kind) : kind(kind), cmp_is_32(false), test_bit(0) {
      assert(kind != Cbz && kind != Cbnz && kind != Tbz && kind != Tbnz);
    }

    constexpr Jump(Kind kind, AsmReg cmp_reg, bool cmp_is_32)
        : kind(kind), cmp_reg(cmp_reg), cmp_is_32(cmp_is_32), test_bit(0) {
      assert(kind == Cbz || kind == Cbnz);
    }

    constexpr Jump(Kind kind, AsmReg cmp_reg, u8 test_bit)
        : kind(kind), cmp_reg(cmp_reg), cmp_is_32(false), test_bit(test_bit) {
      assert(kind == Tbz || kind == Tbnz);
    }

    constexpr Jump change_kind(Kind new_kind) const {
      auto cpy = *this;
      cpy.kind = new_kind;
      return cpy;
    }
  };

  Jump invert_jump(Jump jmp) noexcept;
  Jump swap_jump(Jump jmp) noexcept;

  void generate_branch_to_block(Jump jmp,
                                IRBlockRef target,
                                bool needs_split,
                                bool last_inst) noexcept;

  void generate_raw_jump(Jump jmp, Assembler::Label target) noexcept;

  void generate_raw_set(Jump jmp, AsmReg dst) noexcept;
  void generate_raw_mask(Jump jmp, AsmReg dst) noexcept;

  void spill_before_call(CallingConv calling_conv, u64 except_mask = 0);

  struct CallArg {
    enum class Flag : u8 {
      none,
      zext,
      sext,
      byval,
      sret,
    };

    explicit CallArg(IRValueRef value,
                     Flag flags = Flag::none,
                     u32 byval_align = 0,
                     u32 byval_size = 0)
        : value(value),
          flag(flags),
          byval_align(byval_align),
          byval_size(byval_size) {}

    IRValueRef value;
    Flag flag;
    u32 byval_align;
    u32 byval_size;
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
      std::span<CallArg> arguments,
      std::span<ValuePart> results,
      CallingConv calling_conv,
      bool variable_args);

  /// Generate code sequence to load address of sym into a register. This will
  /// generate a function call for dynamic TLS access models.
  ScratchReg tls_get_addr(Assembler::SymRef sym, TLSModel model) noexcept;

  bool has_cpu_feats(CPU_FEATURES feats) const noexcept {
    return ((cpu_feats & feats) == feats);
  }
};

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CallingConv::handle_func_args(
    CompilerA64<Adaptor, Derived, BaseTy, Config> *compiler) const noexcept {
  using IRValueRef = typename Adaptor::IRValueRef;
  using ScratchReg =
      typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ScratchReg;
  using ValuePartRef =
      typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ValuePartRef;

  u32 scalar_reg_count = 0, vec_reg_count = 0;
  i32 frame_off = 0;

  const auto gp_regs = arg_regs_gp();
  const auto vec_regs = arg_regs_vec();

  // not the most prettiest but it's okay for now
  // TODO(ts): we definitely want to see if writing some custom machinery to
  // give arguments their own register as a fixed assignment (if there are no
  // calls) gives perf benefits on C/C++ codebases with a lot of smaller
  // getters
  compiler->fixed_assignment_nonallocatable_mask |= arg_regs_mask();

  ValuePartRef stack_off_scratch{compiler, Config::GP_BANK};
  const auto stack_off_reg = [&]() {
    if (stack_off_scratch.has_reg()) {
      return stack_off_scratch.cur_reg();
    }

    const auto reg = stack_off_scratch.alloc_reg(arg_regs_mask());
    compiler->func_arg_stack_add_off = compiler->assembler.text_cur_off();
    compiler->func_arg_stack_add_reg = reg;
    ASMC(compiler, ADDxi, reg, DA_SP, 0);
    return reg;
  };

  u32 arg_idx = 0;
  for (const IRValueRef arg : compiler->adaptor->cur_args()) {
    if (compiler->adaptor->cur_arg_is_byval(arg_idx)) {
      const u32 size = compiler->adaptor->cur_arg_byval_size(arg_idx);
      const u32 align = compiler->adaptor->cur_arg_byval_align(arg_idx);
      assert(align <= 16);
      assert((align & (align - 1)) == 0);
      if (align == 16) {
        frame_off = util::align_up(frame_off, 16);
      }

      const auto stack_reg = stack_off_reg();

      // need to use a ScratchReg here since otherwise the ValuePartRef
      // could allocate one of the argument registers
      auto [_, arg_ref] = compiler->result_ref_single(arg);
      // TODO: multiple arguments?
      const auto res_reg = arg_ref.alloc_reg(arg_regs_mask());
      ASMC(compiler, ADDxi, res_reg, stack_reg, frame_off);
      compiler->set_value(arg_ref, arg_ref.cur_reg());

      frame_off += util::align_up(size, 8);
      ++arg_idx;
      continue;
    }

    const auto parts = compiler->derived()->val_parts(arg);
    const u32 part_count = parts.count();
    if (compiler->adaptor->cur_arg_is_sret(arg_idx)) {
      if (auto target_reg = sret_reg(); target_reg) {
        assert(part_count == 1 && "sret must be single-part");
        auto [_, arg_ref] = compiler->result_ref_single(arg);
        compiler->set_value(arg_ref, *target_reg);
        ++arg_idx;
        continue;
      }
    }

    // TODO(ts): replace with arg_reg_alignment, arg_stack_alignment and
    // allow_split_reg_stack_passing?
    if (compiler->derived()->arg_is_int128(arg)) {
      if (scalar_reg_count & 1) {
        // 128 bit integers are always passed in even positions
        ++scalar_reg_count;
      }
      if (scalar_reg_count + 1 >= gp_regs.size()) {
        frame_off = util::align_up(frame_off, 16);
      }
    } else if (part_count > 1 &&
               !compiler->derived()->arg_allow_split_reg_stack_passing(arg)) {
      if (scalar_reg_count + part_count - 1 >= gp_regs.size()) {
        // multipart values are either completely passed in registers
        // or not at all
        scalar_reg_count = gp_regs.size();
      }
    }

    auto arg_ref = compiler->result_ref(arg);
    for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
      auto part_ref = arg_ref.part(part_idx);
      auto ap = part_ref.assignment();
      unsigned size = ap.part_size();
      unsigned bank = parts.reg_bank(part_idx);
      ScratchReg scratch{compiler};

      if (bank == 0) {
        if (scalar_reg_count < gp_regs.size()) {
          compiler->set_value(part_ref, gp_regs[scalar_reg_count++]);
        } else {
          const auto stack_reg = stack_off_reg();

          AsmReg dst = ap.fixed_assignment() ? AsmReg{ap.full_reg_id()}
                                             : scratch.alloc(bank);
          if (size <= 1) {
            ASMC(compiler, LDRBu, dst, stack_reg, frame_off);
          } else if (size <= 2) {
            ASMC(compiler, LDRHu, dst, stack_reg, frame_off);
          } else if (size <= 4) {
            ASMC(compiler, LDRwu, dst, stack_reg, frame_off);
          } else {
            assert(size <= 8 && "gp arg size > 8");
            ASMC(compiler, LDRxu, dst, stack_reg, frame_off);
          }
          if (ap.fixed_assignment()) {
            ap.set_modified(true);
          } else {
            // TODO(ts): do we need to spill here?
            compiler->spill_reg(dst, ap.frame_off(), size);
            ap.set_stack_valid();
          }
          frame_off += 8;
        }
      } else {
        if (vec_reg_count < vec_regs.size()) {
          compiler->set_value(part_ref, vec_regs[vec_reg_count++]);
        } else {
          const auto stack_reg = stack_off_reg();
          uint64_t word_size = size <= 8 ? 8 : 16;
          frame_off = util::align_up(frame_off, word_size);

          AsmReg dst = ap.fixed_assignment() ? AsmReg{ap.full_reg_id()}
                                             : scratch.alloc(bank);
          if (size <= 4) {
            ASMC(compiler, LDRsu, dst, stack_reg, frame_off);
          } else if (size <= 8) {
            ASMC(compiler, LDRdu, dst, stack_reg, frame_off);
          } else {
            assert(size <= 16);
            ASMC(compiler, LDRqu, dst, stack_reg, frame_off);
          }
          if (ap.fixed_assignment()) {
            ap.set_modified(true);
          } else {
            compiler->spill_reg(dst, ap.frame_off(), size);
            ap.set_stack_valid();
          }

          frame_off += word_size;
        }
      }
    }

    ++arg_idx;
  }

  compiler->fixed_assignment_nonallocatable_mask &= ~arg_regs_mask();
  compiler->scalar_arg_count = scalar_reg_count;
  compiler->vec_arg_count = vec_reg_count;
  // TODO(ae): this is unused right now
  compiler->var_arg_stack_off = frame_off;
  // Hack: we don't know the frame size, so for a va_start(), we cannot easily
  // compute the offset from the frame pointer. But we have a stack_reg here,
  // so use it for var args.
  if (compiler->adaptor->cur_is_vararg()) {
    const auto stack_reg = stack_off_reg();
    ASMC(compiler, ADDxi, stack_reg, stack_reg, frame_off);
    ASMC(compiler,
         STRxu,
         stack_reg,
         DA_GP(29),
         compiler->reg_save_frame_off + 192);
  }
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
u32 CallingConv::calculate_call_stack_space(
    CompilerA64<Adaptor, Derived, BaseTy, Config> *compiler,
    std::span<typename CompilerA64<Adaptor, Derived, BaseTy, Config>::CallArg>
        arguments) noexcept {
  using CallArg =
      typename CompilerA64<Adaptor, Derived, BaseTy, Config>::CallArg;

  u32 gp_reg_count = 0, vec_reg_count = 0, stack_space = 0;

  const auto gp_regs = arg_regs_gp();
  const auto vec_regs = arg_regs_vec();

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

    const u32 part_count = compiler->derived()->val_parts(arg.value).count();

    if (compiler->derived()->arg_is_int128(arg.value)) {
      if (gp_reg_count & 1) {
        // 128 bit ints are only passed starting at even registers
        ++gp_reg_count;
      }
      if (gp_reg_count + 1 >= gp_regs.size()) {
        stack_space = util::align_up(stack_space, 16);
      }
    } else if (part_count > 1 &&
               !compiler->derived()->arg_allow_split_reg_stack_passing(
                   arg.value)) {
      if (gp_reg_count + part_count - 1 >= gp_regs.size()) {
        gp_reg_count = gp_regs.size();
      }
    }

    auto vr = compiler->val_ref(arg.value);
    vr.disown();
    for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
      auto vpr = vr.part(part_idx);

      if (vpr.bank() == 0) {
        if (gp_reg_count < gp_regs.size()) {
          ++gp_reg_count;
        } else {
          stack_space += 8;
        }
      } else {
        assert(vpr.bank() == 1);
        if (vec_reg_count < vec_regs.size()) {
          ++vec_reg_count;
        } else {
          stack_space = util::align_up(stack_space, vpr.part_size());
          stack_space += util::align_up(vpr.part_size(), 8);
        }
      }
    }
  }

  return stack_space;
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
u32 CallingConv::handle_call_args(
    CompilerA64<Adaptor, Derived, BaseTy, Config> *compiler,
    std::span<typename CompilerA64<Adaptor, Derived, BaseTy, Config>::CallArg>
        arguments,
    util::SmallVector<
        typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ScratchReg,
        8> &arg_scratchs) noexcept {
  using CallArg =
      typename CompilerA64<Adaptor, Derived, BaseTy, Config>::CallArg;
  using ScratchReg =
      typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ScratchReg;

  u32 gp_reg_count = 0, vec_reg_count = 0;
  i32 stack_off = 0;

  const auto gp_regs = arg_regs_gp();
  const auto vec_regs = arg_regs_vec();
  for (auto &arg : arguments) {
    if (arg.flag == CallArg::Flag::byval) {
      ScratchReg scratch(compiler);
      auto [_, ptr_ref] = compiler->val_ref_single(arg.value);
      AsmReg ptr_reg = ptr_ref.load_to_reg();

      auto tmp_reg = scratch.alloc_gp();

      auto size = arg.byval_size;
      assert(arg.byval_align <= 16);
      stack_off = util::align_up(stack_off, arg.byval_align);

      i32 off = 0;
      while (size > 0) {
        if (size >= 8) {
          ASMC(compiler, LDRxu, tmp_reg, ptr_reg, off);
          ASMC(compiler, STRxu, tmp_reg, DA_SP, stack_off + off);
          off += 8;
          size -= 8;
          continue;
        }
        if (size >= 4) {
          ASMC(compiler, LDRwu, tmp_reg, ptr_reg, off);
          ASMC(compiler, STRwu, tmp_reg, DA_SP, stack_off + off);
          off += 4;
          size -= 4;
          continue;
        }
        if (size >= 2) {
          ASMC(compiler, LDRHu, tmp_reg, ptr_reg, off);
          ASMC(compiler, STRHu, tmp_reg, DA_SP, stack_off + off);
          off += 2;
          size -= 2;
          continue;
        }
        ASMC(compiler, LDRBu, tmp_reg, ptr_reg, off);
        ASMC(compiler, STRBu, tmp_reg, DA_SP, stack_off + off);
        off += 1;
        size -= 1;
      }

      stack_off += util::align_up(arg.byval_size, 8);
      continue;
    }

    if (arg.flag == CallArg::Flag::sret) {
      if (auto target_reg = sret_reg(); target_reg) {
        auto [_, ptr_ref] = compiler->val_ref_single(arg.value);
        arg_scratchs.emplace_back(compiler).alloc_specific(
            ptr_ref.reload_into_specific(compiler, *target_reg));
        continue;
      }
    }

    const u32 part_count = compiler->derived()->val_parts(arg.value).count();

    if (compiler->derived()->arg_is_int128(arg.value)) {
      if (gp_reg_count & 1) {
        // 128 bit ints are only passed starting in even registers
        ++gp_reg_count;
      }
      if (gp_reg_count + 1 >= gp_regs.size()) {
        stack_off = util::align_up(stack_off, 16);
      }
    } else if (part_count > 1 &&
               !compiler->derived()->arg_allow_split_reg_stack_passing(
                   arg.value)) {
      if (gp_reg_count + part_count - 1 >= gp_regs.size()) {
        gp_reg_count = gp_regs.size();
      }
    }

    auto vr = compiler->val_ref(arg.value);
    for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
      auto ref = vr.part(part_idx);
      ScratchReg scratch(compiler);

      if (ref.bank() == 0) {
        const auto ext_reg = [&](AsmReg reg) {
          if (arg.flag == CallArg::Flag::zext) {
            switch (ref.part_size()) {
            case 1: ASMC(compiler, UXTBw, reg, reg); break;
            case 2: ASMC(compiler, UXTHw, reg, reg); break;
            case 4: ASMC(compiler, MOVw, reg, reg); break;
            default: break;
            }
          } else if (arg.flag == CallArg::Flag::sext) {
            switch (ref.part_size()) {
            case 1: ASMC(compiler, SXTBx, reg, reg); break;
            case 2: ASMC(compiler, SXTHx, reg, reg); break;
            case 4: ASMC(compiler, SXTWx, reg, reg); break;
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
          ext_reg(scratch.cur_reg());
          arg_scratchs.push_back(std::move(scratch));
        } else {
          auto reg =
              ref.reload_into_specific_fixed(compiler, scratch.alloc_gp());
          ext_reg(reg);
          ASMC(compiler, STRxu, reg, DA_SP, stack_off);
          stack_off += 8;
        }
      } else {
        assert(ref.bank() == 1);
        if (vec_reg_count < vec_regs.size()) {
          const AsmReg target_reg = vec_regs[vec_reg_count++];
          if (ref.is_in_reg(target_reg) && ref.can_salvage()) {
            scratch.alloc_specific(ref.salvage());
          } else {
            scratch.alloc_specific(
                ref.reload_into_specific(compiler, target_reg));
          }
          arg_scratchs.push_back(std::move(scratch));
        } else {
          auto reg = ref.load_to_reg();
          switch (ref.part_size()) {
          case 4:
            ASMC(compiler, STRsu, reg, DA_SP, stack_off);
            stack_off += 8;
            break;
          case 8:
            ASMC(compiler, STRdu, reg, DA_SP, stack_off);
            stack_off += 8;
            break;
          case 16: {
            stack_off = util::align_up(stack_off, 16);
            ASMC(compiler, STRqu, reg, DA_SP, stack_off);
            stack_off += 16;
            break;
          }
            // can't guarantee the alignment on the stack
          default: TPDE_FATAL("invalid size for passing vector register");
          }
        }
      }
    }
  }

  return vec_reg_count;
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CallingConv::fill_call_results(
    CompilerA64<Adaptor, Derived, BaseTy, Config> *compiler,
    std::span<typename CompilerA64<Adaptor, Derived, BaseTy, Config>::ValuePart>
        results) noexcept {
  u32 gp_reg_count = 0, vec_reg_count = 0;

  const auto gp_regs = ret_regs_gp();
  const auto vec_regs = ret_regs_vec();

  for (auto &ref : results) {
    AsmReg reg = AsmReg::make_invalid();
    if (ref.bank() == 0) {
      assert(gp_reg_count < gp_regs.size());
      reg = gp_regs[gp_reg_count++];
    } else {
      assert(ref.bank() == 1);
      assert(vec_reg_count < vec_regs.size());
      reg = vec_regs[vec_reg_count++];
    }

    ref.set_value_reg(compiler, reg);
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> class BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::start_func(
    const u32 func_idx) noexcept {
  using SymRef = typename Assembler::SymRef;
  SymRef personality_sym;
  if (this->adaptor->cur_needs_unwind_info()) {
    const IRFuncRef personality_func = this->adaptor->cur_personality_func();
    if (personality_func != Adaptor::INVALID_FUNC_REF) {
      for (const auto &[func_ref, sym] : personality_syms) {
        if (func_ref == personality_func) {
          personality_sym = sym;
          break;
        }
      }

      if (!personality_sym.valid()) {
        // create symbol that contains the address of the personality
        // function
        auto fn_sym = this->assembler.sym_add_undef(
            this->adaptor->func_link_name(personality_func),
            Assembler::SymBinding::GLOBAL);

        u32 off;
        u8 tmp[8] = {};

        auto rodata = this->assembler.get_data_section(true, true);
        personality_sym =
            this->assembler.sym_def_data(rodata,
                                         {},
                                         {tmp, sizeof(tmp)},
                                         8,
                                         Assembler::SymBinding::LOCAL,
                                         &off);
        this->assembler.reloc_abs(rodata, fn_sym, off, 0);

        personality_syms.emplace_back(personality_func, personality_sym);
      }
    }
  }
  this->assembler.start_func(this->func_syms[func_idx], personality_sym);

  const CallingConv conv = derived()->cur_calling_convention();
  this->register_file.allocatable = conv.initial_free_regs();
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::
    gen_func_prolog_and_args() noexcept {
  // prologue:
  // sub sp, sp, #<frame_size>
  // stp x29, x30, [sp]
  // mov x29, sp
  // optionally create vararg save-area
  // reserve space for callee-saved regs
  //   4 byte per callee-saved reg pair since for each we do
  //   stp r1, r2, [sp + XX]

  // TODO(ts): for smaller functions we could enable an optimization
  // to store the saved regs after the local variables
  // which we could then use to not allocate space for unsaved regs
  // which could help in the common case.
  // However, we need to commit to this at the beginning of the function
  // as otherwise stack accesses need to skip the reg-save area

  func_ret_offs.clear();
  func_start_off = this->assembler.text_cur_off();
  scalar_arg_count = vec_arg_count = 0xFFFF'FFFF;
  func_arg_stack_add_off = ~0u;

  // We don't actually generate the prologue here and merely allocate space
  // for it. Right now, we don't know which callee-saved registers will be
  // used. While we could pad with nops, we later move the beginning of the
  // function so that small functions don't have to execute 9 nops.
  // See finish_func.
  const CallingConv call_conv = derived()->cur_calling_convention();
  {
    u32 reg_save_size = 0u;
    bool pending = false;
    u32 last_bank = 0;
    for (const auto reg : call_conv.callee_saved_regs()) {
      const auto bank = this->register_file.reg_bank(reg);
      if (pending) {
        if (last_bank == bank) {
          reg_save_size += 4;
        } else {
          // cannot store gp and vector reg in pair
          reg_save_size += 8;
        }
        pending = false;
        continue;
      }

      last_bank = bank;
      pending = true;
    }
    if (pending) {
      reg_save_size += 4;
    }

    // Reserve space for sub sp, stp x29/x30, and mov x29, sp.
    func_prologue_alloc = reg_save_size + 12;
    this->assembler.text_ensure_space(func_prologue_alloc);
    this->assembler.text_write_ptr += func_prologue_alloc;
    // ldp needs the same number of instructions as stp
    func_reg_restore_alloc = reg_save_size;
  }

  // TODO(ts): support larger stack alignments?

  if (this->adaptor->cur_is_vararg()) {
    reg_save_frame_off =
        util::align_up(8u * call_conv.callee_saved_regs().size(), 16) + 16;
    this->assembler.text_ensure_space(4 * 8);
    ASMNC(STPx, DA_GP(0), DA_GP(1), DA_SP, reg_save_frame_off);
    ASMNC(STPx, DA_GP(2), DA_GP(3), DA_SP, reg_save_frame_off + 16);
    ASMNC(STPx, DA_GP(4), DA_GP(5), DA_SP, reg_save_frame_off + 32);
    ASMNC(STPx, DA_GP(6), DA_GP(7), DA_SP, reg_save_frame_off + 48);
    ASMNC(STPq, DA_V(0), DA_V(1), DA_SP, reg_save_frame_off + 64);
    ASMNC(STPq, DA_V(2), DA_V(3), DA_SP, reg_save_frame_off + 96);
    ASMNC(STPq, DA_V(4), DA_V(5), DA_SP, reg_save_frame_off + 128);
    ASMNC(STPq, DA_V(6), DA_V(7), DA_SP, reg_save_frame_off + 160);
  }

  call_conv.handle_func_args(this);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::finish_func(
    u32 func_idx) noexcept {
  const CallingConv conv = Base::derived()->cur_calling_convention();

  const u64 saved_regs =
      this->register_file.clobbered & conv.callee_saved_mask();

  const auto dyn_alloca = this->adaptor->cur_has_dynamic_alloca();
  auto stack_reg = DA_SP;
  if (dyn_alloca) {
    stack_reg = DA_GP(29);
  }

  auto final_frame_size = util::align_up(this->stack.frame_size, 16);
  if (final_frame_size > 4095) {
    // round up to 4k since SUB cannot encode immediates greater than 4095
    final_frame_size = util::align_up(final_frame_size, 4096);
    assert(final_frame_size < 16 * 1024 * 1024);
  }

  const auto fde_off = this->assembler.eh_begin_fde();

  {
    // NB: code alignment factor 4, data alignment factor -8.
    util::SmallVector<u32, 16> prologue;
    prologue.push_back(de64_SUBxi(DA_SP, DA_SP, final_frame_size));
    this->assembler.eh_write_inst(dwarf::DW_CFA_advance_loc, 1);
    this->assembler.eh_write_inst(dwarf::DW_CFA_def_cfa_offset,
                                  final_frame_size);
    prologue.push_back(de64_STPx(DA_GP(29), DA_GP(30), DA_SP, 0));
    prologue.push_back(de64_MOV_SPx(DA_GP(29), DA_SP));
    this->assembler.eh_write_inst(dwarf::DW_CFA_advance_loc, 2);
    this->assembler.eh_write_inst(dwarf::DW_CFA_def_cfa_register,
                                  dwarf::a64::DW_reg_fp);
    this->assembler.eh_write_inst(
        dwarf::DW_CFA_offset, dwarf::a64::DW_reg_fp, final_frame_size / 8);
    this->assembler.eh_write_inst(
        dwarf::DW_CFA_offset, dwarf::a64::DW_reg_lr, final_frame_size / 8 - 1);

    auto secref_eh_frame = this->assembler.get_eh_frame_section();
    auto &sec_eh_frame = this->assembler.get_section(secref_eh_frame);
    // Patched below
    auto fde_prologue_adv_off = sec_eh_frame.data.size();
    this->assembler.eh_write_inst(dwarf::DW_CFA_advance_loc, 0);

    AsmReg last_reg = AsmReg::make_invalid();
    u32 frame_off = 16;
    for (auto reg : util::BitSetIterator{saved_regs}) {
      if (last_reg.valid()) {
        const auto reg_bank = this->register_file.reg_bank(AsmReg{reg});
        const auto last_bank = this->register_file.reg_bank(last_reg);
        if (reg_bank == last_bank) {
          if (reg_bank == 0) {
            prologue.push_back(
                de64_STPx(last_reg, AsmReg{reg}, stack_reg, frame_off));
          } else {
            prologue.push_back(
                de64_STPd(last_reg, AsmReg{reg}, stack_reg, frame_off));
          }
          frame_off += 16;
          last_reg = AsmReg::make_invalid();
        } else {
          assert(last_bank == 0 && reg_bank == 1);
          prologue.push_back(de64_STRxu(last_reg, stack_reg, frame_off));
          frame_off += 8;
          last_reg = AsmReg{reg};
        }
        continue;
      }

      u8 dwarf_base = reg < 32 ? dwarf::a64::DW_reg_v0 : dwarf::a64::DW_reg_x0;
      u8 dwarf_reg = dwarf_base + reg % 32;
      u32 cfa_off = (final_frame_size - frame_off) / 8;
      if ((dwarf_reg & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) == 0) {
        this->assembler.eh_write_inst(dwarf::DW_CFA_offset, dwarf_reg, cfa_off);
      } else {
        this->assembler.eh_write_inst(
            dwarf::DW_CFA_offset_extended, dwarf_reg, cfa_off);
      }

      last_reg = AsmReg{reg};
    }

    if (last_reg.valid()) {
      if (this->register_file.reg_bank(last_reg) == 0) {
        prologue.push_back(de64_STRxu(last_reg, stack_reg, frame_off));
      } else {
        prologue.push_back(de64_STRdu(last_reg, stack_reg, frame_off));
      }
    }

    assert(prologue.size() * sizeof(u32) <= func_prologue_alloc);

    assert(prologue.size() < 0x4c);
    sec_eh_frame.data[fde_prologue_adv_off] =
        dwarf::DW_CFA_advance_loc | (prologue.size() - 3);

    // Pad with NOPs so that func_prologue_alloc - prologue.size() is a
    // multiple if 16 (the function alignment).
    const auto nop_count = (func_prologue_alloc / 4 - prologue.size()) % 4;
    const auto nop = de64_NOP();
    for (auto i = 0u; i < nop_count; ++i) {
      prologue.push_back(nop);
    }

    // Shrink function at the beginning
    func_start_off +=
        util::align_down(func_prologue_alloc - prologue.size() * 4, 16);
    this->assembler.sym_set_value(this->func_syms[func_idx], func_start_off);
    std::memcpy(this->assembler.text_ptr(func_start_off),
                prologue.data(),
                prologue.size() * sizeof(u32));
  }

  if (func_arg_stack_add_off != ~0u) {
    *reinterpret_cast<u32 *>(this->assembler.text_ptr(func_arg_stack_add_off)) =
        de64_ADDxi(func_arg_stack_add_reg, DA_SP, final_frame_size);
  }

  if (this->adaptor->cur_needs_unwind_info()) {
    // we need to patch the landing pad labels into their actual offsets
    for (auto &info : this->assembler.except_call_site_table) {
      if (info.pad_label_or_off == ~0u) {
        info.pad_label_or_off = 0; // special marker for resume/normal calls
                                   // where we dont have a landing pad
        continue;
      }
      info.pad_label_or_off = this->assembler.label_offset(
          static_cast<Assembler::Label>(info.pad_label_or_off));
    }
  } else {
    assert(this->assembler.except_call_site_table.empty());
  }

  // TODO(ts): honor cur_needs_unwind_info
  this->assembler.end_func();
  this->assembler.eh_end_fde(fde_off, this->func_syms[func_idx]);
  this->assembler.except_encode_func();

  if (func_ret_offs.empty()) {
    return;
  }

  auto *text_data = this->assembler.text_ptr(0);
  u32 first_ret_off = func_ret_offs[0];
  u32 ret_size = 0;
  {
    u32 *write_ptr = reinterpret_cast<u32 *>(text_data + first_ret_off);
    const auto ret_start = write_ptr;
    if (dyn_alloca) {
      *write_ptr++ = de64_MOV_SPx(DA_SP, DA_GP(29));
    } else {
      *write_ptr++ = de64_LDPx(DA_GP(29), DA_GP(30), DA_SP, 0);
    }

    AsmReg last_reg = AsmReg::make_invalid();
    u32 frame_off = 16;
    for (auto reg : util::BitSetIterator{saved_regs}) {
      if (last_reg.valid()) {
        const auto reg_bank = this->register_file.reg_bank(AsmReg{reg});
        const auto last_bank = this->register_file.reg_bank(last_reg);
        if (reg_bank == last_bank) {
          if (reg_bank == 0) {
            *write_ptr++ =
                de64_LDPx(last_reg, AsmReg{reg}, stack_reg, frame_off);
          } else {
            *write_ptr++ =
                de64_LDPd(last_reg, AsmReg{reg}, stack_reg, frame_off);
          }
          frame_off += 16;
          last_reg = AsmReg::make_invalid();
        } else {
          assert(last_bank == 0 && reg_bank == 1);
          *write_ptr++ = de64_LDRxu(last_reg, stack_reg, frame_off);
          frame_off += 8;
          last_reg = AsmReg{reg};
        }
        continue;
      }

      last_reg = AsmReg{reg};
    }

    if (last_reg.valid()) {
      if (this->register_file.reg_bank(last_reg) == 0) {
        *write_ptr++ = de64_LDRxu(last_reg, stack_reg, frame_off);
      } else {
        *write_ptr++ = de64_LDRdu(last_reg, stack_reg, frame_off);
      }
    }

    if (dyn_alloca) {
      *write_ptr++ = de64_LDPx(DA_GP(29), DA_GP(30), DA_SP, 0);
    }

    *write_ptr++ = de64_ADDxi(DA_SP, DA_SP, final_frame_size);
    *write_ptr++ = de64_RET(DA_GP(30));

    ret_size = (write_ptr - ret_start) * 4;
  }

  for (u32 i = 1; i < func_ret_offs.size(); ++i) {
    std::memcpy(
        text_data + func_ret_offs[i], text_data + first_ret_off, ret_size);
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> class BaseTy,
          typename Config>
u32 CompilerA64<Adaptor, Derived, BaseTy, Config>::
    func_reserved_frame_size() noexcept {
  const CallingConv call_conv = derived()->cur_calling_convention();
  // when indexing into the stack frame, the code needs to skip over the saved
  // registers and the reg-save area if it exists
  // (+16 for FP/LR)
  u32 reserved_size =
      util::align_up(call_conv.callee_saved_regs().size() * 8, 16) + 16;
  if (this->adaptor->cur_is_vararg()) {
    // varargs size: 8 GP regs (64), 8 vector regs (128) = 192
    // We add 8 bytes top ptr, because we don't know the frame size while
    // compiling, and 8 bytes padding for alignment.
    reserved_size += 192 + 16;
  }
  return reserved_size;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::reset() noexcept {
  func_ret_offs.clear();
  personality_syms.clear();
  Base::reset();
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::gen_func_epilog() noexcept {
  // epilogue:
  // if !func_has_dynamic_alloca:
  //   ldp x29, x30, [sp]
  // else:
  //   mov sp, fp
  // for each saved reg pair:
  //   if func_has_dynamic_alloca:
  //     ldp r1, r2, [fp, #<off>]
  //   else:
  //     ldp r1, r2, [sp, #<off>]
  // if func_has_dynamic_alloca:
  //   ldp x29, x30, [sp]
  // add sp, sp, #<frame_size>
  // ret
  //
  // however, since we will later patch this, we only
  // reserve the space for now

  func_ret_offs.push_back(this->assembler.text_cur_off());

  u32 epilogue_size = 4 + func_reg_restore_alloc + 4 +
                      4; // ldp + size of reg restore + add + ret
  if (this->adaptor->cur_has_dynamic_alloca()) {
    epilogue_size += 4; // extra mov sp, fp
  }

  this->assembler.text_ensure_space(epilogue_size);
  this->assembler.text_write_ptr += epilogue_size;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::spill_reg(
    const AsmReg reg, const u32 frame_off, const u32 size) noexcept {
  assert((size & (size - 1)) == 0);
  assert(util::align_up(frame_off, size) == frame_off);
  // We don't support stack frames that aren't encodeable with add/sub.
  assert(frame_off >= 0 && frame_off < 0x1'000'000);

  u32 off = frame_off;
  u32 fp_mod = 0;
  auto addr_base = AsmReg{AsmReg::FP};
  ValuePartRef scratch{derived(), Config::GP_BANK};
  if (off >= 0x1000 * size) [[unlikely]] {
    // We cannot encode the offset in the store instruction.
    auto tmp =
        this->register_file.find_first_free_excluding(Config::GP_BANK, 0);
    if (tmp.valid()) {
      scratch.alloc_specific(tmp);
      ASM(ADDxi, tmp, DA_GP(29), off & ~0xfff);
      off &= 0xfff;
      addr_base = tmp;
    } else {
      fp_mod = off & ~u32{0xfff};
      while (off >= 0x1000) {
        u32 step = off > 0xff'f000 ? 0xff'f000 : off & 0xff'f000;
        ASM(ADDxi, DA_GP(29), DA_GP(29), step);
        off -= step;
      }
    }
  }

  this->assembler.text_ensure_space(4);
  assert(-static_cast<i32>(frame_off) < 0);
  if (reg.id() <= AsmReg::R30) {
    switch (size) {
    case 1: ASMNC(STRBu, reg, addr_base, off); break;
    case 2: ASMNC(STRHu, reg, addr_base, off); break;
    case 4: ASMNC(STRwu, reg, addr_base, off); break;
    case 8: ASMNC(STRxu, reg, addr_base, off); break;
    default: TPDE_UNREACHABLE("invalid register spill size");
    }
  } else {
    switch (size) {
    case 1: ASMNC(STRbu, reg, addr_base, off); break;
    case 2: ASMNC(STRhu, reg, addr_base, off); break;
    case 4: ASMNC(STRsu, reg, addr_base, off); break;
    case 8: ASMNC(STRdu, reg, addr_base, off); break;
    case 16: ASMNC(STRqu, reg, addr_base, off); break;
    default: TPDE_UNREACHABLE("invalid register spill size");
    }
  }

  while (fp_mod) [[unlikely]] {
    u32 step = fp_mod > 0xff'f000 ? 0xff'f000 : fp_mod & 0xff'f000;
    ASM(SUBxi, DA_GP(29), DA_GP(29), step);
    fp_mod -= step;
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::load_from_stack(
    const AsmReg dst,
    const i32 frame_off,
    const u32 size,
    const bool sign_extend) noexcept {
  assert((size & (size - 1)) == 0);
  assert(util::align_up(frame_off, size) == frame_off);
  // We don't support stack frames that aren't encodeable with add/sub.
  assert(frame_off >= 0 && frame_off < 0x1'000'000);

  u32 off = frame_off;
  auto addr_base = AsmReg{AsmReg::FP};
  ScratchReg scratch{derived()};
  if (off >= 0x1000 * size) [[unlikely]] {
    // need to calculate this explicitely
    addr_base = dst.id() <= AsmReg::R30 ? dst : scratch.alloc_gp();
    ASM(ADDxi, addr_base, DA_GP(29), off & ~0xfff);
    off &= 0xfff;
  }

  this->assembler.text_ensure_space(4);
  if (dst.id() <= AsmReg::R30) {
    if (!sign_extend) {
      switch (size) {
      case 1: ASMNC(LDRBu, dst, addr_base, off); break;
      case 2: ASMNC(LDRHu, dst, addr_base, off); break;
      case 4: ASMNC(LDRwu, dst, addr_base, off); break;
      case 8: ASMNC(LDRxu, dst, addr_base, off); break;
      default: TPDE_UNREACHABLE("invalid register spill size");
      }
    } else {
      switch (size) {
      case 1: ASMNC(LDRSBwu, dst, addr_base, off); break;
      case 2: ASMNC(LDRSHwu, dst, addr_base, off); break;
      case 4: ASMNC(LDRSWxu, dst, addr_base, off); break;
      case 8: ASMNC(LDRxu, dst, addr_base, off); break;
      default: TPDE_UNREACHABLE("invalid register spill size");
      }
    }
    return;
  }

  assert(!sign_extend);

  switch (size) {
  case 1: ASMNC(LDRbu, dst, addr_base, off); break;
  case 2: ASMNC(LDRhu, dst, addr_base, off); break;
  case 4: ASMNC(LDRsu, dst, addr_base, off); break;
  case 8: ASMNC(LDRdu, dst, addr_base, off); break;
  case 16: ASMNC(LDRqu, dst, addr_base, off); break;
  default: TPDE_UNREACHABLE("invalid register spill size");
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::
    load_address_of_var_reference(const AsmReg dst,
                                  const AssignmentPartRef ap) noexcept {
  static_assert(Config::DEFAULT_VAR_REF_HANDLING);
  assert(-static_cast<i32>(ap.assignment->frame_off) < 0);
  // per-default, variable references are only used by
  // allocas
  if (!ASMIF(ADDxi, dst, DA_GP(29), ap.assignment->frame_off)) {
    materialize_constant(ap.assignment->frame_off, 0, 4, dst);
    ASM(ADDx_uxtw, dst, DA_GP(29), dst, 0);
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::mov(
    const AsmReg dst, const AsmReg src, const u32 size) noexcept {
  assert(dst.valid());
  assert(src.valid());
  if (dst.id() <= AsmReg::SP && src.id() <= AsmReg::SP) {
    assert(dst.id() != AsmReg::SP && src.id() != AsmReg::SP);
    if (size > 4) {
      ASM(MOVx, dst, src);
    } else {
      ASM(MOVw, dst, src);
    }
  } else if (dst.id() >= AsmReg::V0 && src.id() >= AsmReg::V0) {
    ASM(ORR16b, dst, src, src);
  } else if (dst.id() <= AsmReg::SP) {
    assert(dst.id() != AsmReg::SP);
    // gp<-vector
    assert(src.id() >= AsmReg::V0);
    assert(size <= 8);
    if (size <= 4) {
      ASM(FMOVws, dst, src);
    } else {
      ASM(FMOVxd, dst, src);
    }
  } else {
    // vector<-gp
    assert(src.id() <= AsmReg::R30);
    assert(dst.id() >= AsmReg::V0);
    assert(size <= 8);
    if (size <= 4) {
      ASM(FMOVsw, dst, src);
    } else {
      ASM(FMOVdx, dst, src);
    }
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
AsmReg CompilerA64<Adaptor, Derived, BaseTy, Config>::gval_expr_as_reg(
    GenericValuePart &gv) noexcept {
  auto &expr = std::get<typename GenericValuePart::Expr>(gv.state);

  ScratchReg scratch{derived()};
  if (!expr.has_base() && !expr.has_index()) {
    AsmReg dst = scratch.alloc_gp();
    derived()->materialize_constant(expr.disp, 0, 8, dst);
    expr.disp = 0;
  } else if (!expr.has_base() && expr.has_index()) {
    AsmReg index_reg = expr.index_reg();
    if (std::holds_alternative<ScratchReg>(expr.index)) {
      scratch = std::move(std::get<ScratchReg>(expr.index));
    } else {
      (void)scratch.alloc_gp();
    }
    AsmReg dst = scratch.cur_reg();
    if ((expr.scale & (expr.scale - 1)) == 0) {
      const auto shift = util::cnt_tz<u64>(expr.scale);
      ASM(LSLxi, dst, index_reg, shift);
    } else {
      ScratchReg scratch2{derived()};
      AsmReg tmp2 = scratch2.alloc_gp();
      derived()->materialize_constant(expr.scale, 0, 8, tmp2);
      ASM(MULx, dst, index_reg, tmp2);
    }
  } else if (expr.has_base() && expr.has_index()) {
    AsmReg base_reg = expr.base_reg();
    AsmReg index_reg = expr.index_reg();
    if (std::holds_alternative<ScratchReg>(expr.base)) {
      scratch = std::move(std::get<ScratchReg>(expr.base));
    } else if (std::holds_alternative<ScratchReg>(expr.index)) {
      scratch = std::move(std::get<ScratchReg>(expr.index));
    } else {
      (void)scratch.alloc_gp();
    }
    AsmReg dst = scratch.cur_reg();
    if ((expr.scale & (expr.scale - 1)) == 0) {
      const auto shift = util::cnt_tz<u64>(expr.scale);
      ASM(ADDx_lsl, dst, base_reg, index_reg, shift);
    } else {
      ScratchReg scratch2{derived()};
      AsmReg tmp2 = scratch2.alloc_gp();
      derived()->materialize_constant(expr.scale, 0, 8, tmp2);
      ASM(MULx, tmp2, index_reg, tmp2);
      ASM(ADDx, dst, base_reg, tmp2);
    }
  } else if (expr.has_base() && !expr.has_index()) {
    AsmReg base_reg = expr.base_reg();
    if (std::holds_alternative<ScratchReg>(expr.base)) {
      scratch = std::move(std::get<ScratchReg>(expr.base));
    } else {
      (void)scratch.alloc_gp();
    }
    AsmReg dst = scratch.cur_reg();
    if (ASMIF(ADDxi, dst, base_reg, expr.disp)) {
      expr.disp = 0;
    } else {
      ASM(MOVx, dst, base_reg);
    }
  } else {
    TPDE_UNREACHABLE("inconsistent GenericValuePart::Expr");
  }

  AsmReg dst = scratch.cur_reg();
  if (expr.disp != 0) {
    if (!ASMIF(ADDxi, dst, dst, expr.disp)) {
      ScratchReg scratch2{derived()};
      AsmReg tmp2 = scratch2.alloc_gp();
      derived()->materialize_constant(expr.disp, 0, 8, tmp2);
      ASM(ADDx, dst, dst, tmp2);
    }
  }

  gv.state = std::move(scratch);
  return dst;
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    ValuePartRef &val_ref, const AsmReg dst) noexcept {
  assert(!val_ref.has_assignment());
  const auto &data = val_ref.state.c;
  materialize_constant(data.data, data.bank, data.size, dst);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    ValuePartRef &val_ref, ScratchReg &dst) noexcept {
  assert(!val_ref.has_assignment());
  materialize_constant(val_ref, dst.alloc(val_ref.state.c.bank));
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    const u64 *data, const u32 bank, const u32 size, AsmReg dst) noexcept {
  const auto const_u64 = data[0];
  if (bank == 0) {
    assert(size <= 8);
    if (const_u64 == 0) {
      ASM(MOVZw, dst, 0);
      return;
    }

    this->assembler.text_ensure_space(5 * 4);
    this->assembler.text_write_ptr +=
        sizeof(u32) *
        de64_MOVconst(reinterpret_cast<u32 *>(this->assembler.text_write_ptr),
                      dst,
                      const_u64);
    return;
  }

  assert(bank == 1);
  if (size == 4) {
    if (ASMIF(FMOVsi, dst, std::bit_cast<float>((u32)const_u64))) {
    } else if (ASMIF(MOVId, dst, static_cast<u32>(const_u64))) {
    } else {
      ScratchReg scratch{derived()};
      const auto tmp = scratch.alloc_gp();
      this->assembler.text_ensure_space(5 * 4);
      this->assembler.text_write_ptr +=
          sizeof(u32) *
          de64_MOVconst(reinterpret_cast<u32 *>(this->assembler.text_write_ptr),
                        tmp,
                        (u32)const_u64);
      ASMNC(FMOVsw, dst, tmp);
    }
    return;
  }

  if (size == 8) {
    if (ASMIF(FMOVdi, dst, std::bit_cast<double>(const_u64))) {
    } else if (ASMIF(MOVId, dst, const_u64)) {
    } else {
      ScratchReg scratch{derived()};
      const auto tmp = scratch.alloc_gp();
      this->assembler.text_ensure_space(5 * 4);
      this->assembler.text_write_ptr +=
          sizeof(u32) *
          de64_MOVconst(reinterpret_cast<u32 *>(this->assembler.text_write_ptr),
                        tmp,
                        const_u64);
      ASMNC(FMOVdx, dst, tmp);
    }
    return;
  }

  // TODO(ts): have some facility to use a constant pool
  if (size == 16) {
    // TODO(ae): safe access...
    const auto high_u64 = data[1];
    if (const_u64 == high_u64 && ASMIF(MOVI2d, dst, const_u64)) {
      return;
    }
    if (high_u64 == 0 && ASMIF(MOVId, dst, const_u64)) {
      return;
    }

    ScratchReg scratch{derived()};
    const auto tmp = scratch.alloc_gp();

    auto rodata = this->assembler.get_data_section(true, false);
    std::span<const u8> raw_data{reinterpret_cast<const u8 *>(data), size};
    auto sym = this->assembler.sym_def_data(
        rodata, "", raw_data, 16, Assembler::SymBinding::LOCAL);
    this->assembler.text_ensure_space(8); // ensure contiguous instructions
    this->assembler.reloc_text(
        sym, R_AARCH64_ADR_PREL_PG_HI21, this->assembler.text_cur_off(), 0);
    ASMNC(ADRP, tmp, 0, 0);
    this->assembler.reloc_text(
        sym, R_AARCH64_LDST128_ABS_LO12_NC, this->assembler.text_cur_off(), 0);
    ASMNC(LDRqu, dst, tmp, 0);
    return;
  }

  TPDE_FATAL("unable to materialize constant");
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> class BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::materialize_constant(
    u64 const_u64, u32 bank, u32 size, AsmReg dst) noexcept {
  // probably not the most efficient but I'm hoping the compiler can optimize
  // this
  auto const_ref = ValuePartRef{this, &const_u64, size, bank};
  materialize_constant(const_ref, dst);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
AsmReg
    CompilerA64<Adaptor, Derived, BaseTy, Config>::select_fixed_assignment_reg(
        const u32 bank, IRValueRef) noexcept {
  // TODO(ts): why is this in here?
  assert(bank <= 1);
  u64 reg_mask = (1ull << 32) - 1;
  if (bank != 0) {
    reg_mask = ~reg_mask;
  }
  reg_mask &= ~fixed_assignment_nonallocatable_mask;

  const auto find_possible_regs = [this,
                                   reg_mask](const u64 preferred_regs) -> u64 {
    // try to first get an unused reg, otherwise an unfixed reg
    u64 free_regs = this->register_file.allocatable & ~this->register_file.used;
    u64 possible_regs = free_regs & preferred_regs & reg_mask;
    if (possible_regs == 0) {
      possible_regs = (this->register_file.used & ~this->register_file.fixed) &
                      preferred_regs & reg_mask;
    }
    return possible_regs;
  };

  u64 possible_regs;
  const auto call_conv = derived()->cur_calling_convention();
  if (derived()->cur_func_may_emit_calls()) {
    // we can only allocated fixed assignments from the callee-saved regs
    const u64 preferred_regs = call_conv.callee_saved_mask();
    possible_regs = find_possible_regs(preferred_regs);
  } else {
    // try allocating any non-callee saved register first, except the result
    // registers
    u64 preferred_regs =
        ~call_conv.result_regs_mask() & ~call_conv.callee_saved_mask();
    possible_regs = find_possible_regs(preferred_regs);
    if (possible_regs == 0) {
      // otherwise fallback to callee-saved regs
      preferred_regs = derived()->cur_calling_convention().callee_saved_mask();
      possible_regs = find_possible_regs(preferred_regs);
    }
  }

  if (possible_regs == 0) {
    return AsmReg::make_invalid();
  }

  // try to first get an unused reg, otherwise an unfixed reg
  if ((possible_regs & ~this->register_file.used) != 0) {
    return AsmReg{util::cnt_tz(possible_regs & ~this->register_file.used)};
  }

  for (const auto reg_id : util::BitSetIterator<>{possible_regs}) {
    const auto reg = AsmReg{reg_id};

    if (this->register_file.is_fixed(reg)) {
      continue;
    }

    const auto local_idx = this->register_file.reg_local_idx(reg);
    const auto part = this->register_file.reg_part(reg);

    if (local_idx == Base::INVALID_VAL_LOCAL_IDX) {
      continue;
    }
    auto *assignment = this->val_assignment(local_idx);
    auto ap = AssignmentPartRef{assignment, part};
    if (ap.modified()) {
      continue;
    }

    return reg;
  }

  return AsmReg::make_invalid();
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> class BaseTy,
          typename Config>
typename CompilerA64<Adaptor, Derived, BaseTy, Config>::Jump
    CompilerA64<Adaptor, Derived, BaseTy, Config>::invert_jump(
        Jump jmp) noexcept {
  switch (jmp.kind) {
  case Jump::Jeq: return jmp.change_kind(Jump::Jne);
  case Jump::Jne: return jmp.change_kind(Jump::Jeq);
  case Jump::Jcs: return jmp.change_kind(Jump::Jcc);
  case Jump::Jcc: return jmp.change_kind(Jump::Jcs);
  case Jump::Jmi: return jmp.change_kind(Jump::Jpl);
  case Jump::Jpl: return jmp.change_kind(Jump::Jmi);
  case Jump::Jvs: return jmp.change_kind(Jump::Jvc);
  case Jump::Jvc: return jmp.change_kind(Jump::Jvs);
  case Jump::Jhi: return jmp.change_kind(Jump::Jls);
  case Jump::Jls: return jmp.change_kind(Jump::Jhi);
  case Jump::Jge: return jmp.change_kind(Jump::Jlt);
  case Jump::Jlt: return jmp.change_kind(Jump::Jge);
  case Jump::Jgt: return jmp.change_kind(Jump::Jle);
  case Jump::Jle: return jmp.change_kind(Jump::Jgt);
  case Jump::jmp: return jmp;
  case Jump::Cbz: return jmp.change_kind(Jump::Cbnz);
  case Jump::Cbnz: return jmp.change_kind(Jump::Cbz);
  case Jump::Tbz: return jmp.change_kind(Jump::Tbnz);
  case Jump::Tbnz: return jmp.change_kind(Jump::Tbz);
  default: TPDE_UNREACHABLE("invalid jump kind");
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
typename CompilerA64<Adaptor, Derived, BaseTy, Config>::Jump
    CompilerA64<Adaptor, Derived, BaseTy, Config>::swap_jump(
        Jump jmp) noexcept {
  switch (jmp.kind) {
  case Jump::Jeq: return jmp.change_kind(Jump::Jeq);
  case Jump::Jne: return jmp.change_kind(Jump::Jne);
  case Jump::Jcc: return jmp.change_kind(Jump::Jhi);
  case Jump::Jcs: return jmp.change_kind(Jump::Jls);
  case Jump::Jhi: return jmp.change_kind(Jump::Jcc);
  case Jump::Jls: return jmp.change_kind(Jump::Jcs);
  case Jump::Jge: return jmp.change_kind(Jump::Jle);
  case Jump::Jlt: return jmp.change_kind(Jump::Jgt);
  case Jump::Jgt: return jmp.change_kind(Jump::Jlt);
  case Jump::Jle: return jmp.change_kind(Jump::Jge);
  case Jump::jmp: return jmp;
  case Jump::Jmi:
  case Jump::Jpl:
  case Jump::Jvs:
  case Jump::Jvc:
  case Jump::Cbz:
  case Jump::Cbnz:
  case Jump::Tbz:
  case Jump::Tbnz:
  default: TPDE_UNREACHABLE("invalid jump kind for swap_jump");
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::generate_branch_to_block(
    const Jump jmp,
    IRBlockRef target,
    const bool needs_split,
    const bool last_inst) noexcept {
  const auto target_idx = this->analyzer.block_idx(target);
  if (!needs_split || jmp.kind == Jump::jmp) {
    this->derived()->move_to_phi_nodes(target_idx);

    if (!last_inst || this->analyzer.block_idx(target) != this->next_block()) {
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
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::generate_raw_jump(
    Jump jmp, Assembler::Label target_label) noexcept {
  const auto is_pending = this->assembler.label_is_pending(target_label);
  this->assembler.text_ensure_space(4);
  if (jmp.kind == Jump::jmp) {
    if (is_pending) {
      ASMNC(B, 0);
      this->assembler.add_unresolved_entry(target_label,
                                           this->assembler.text_cur_off() - 4,
                                           Assembler::UnresolvedEntryKind::BR);
    } else {
      const auto label_off = this->assembler.label_offset(target_label);
      const auto cur_off = this->assembler.text_cur_off();
      assert(cur_off >= label_off);
      const auto diff = cur_off - label_off;
      assert((diff & 0b11) == 0);
      assert(diff < 128 * 1024 * 1024);

      ASMNC(B, -static_cast<ptrdiff_t>(diff) / 4);
    }
    return;
  }

  if (jmp.kind == Jump::Cbz || jmp.kind == Jump::Cbnz) {
    u32 off = 0;
    if (!is_pending) {
      const auto label_off = this->assembler.label_offset(target_label);
      const auto cur_off = this->assembler.text_cur_off();
      assert(cur_off >= label_off);
      off = cur_off - label_off;
      assert((off & 0b11) == 0);
      assert(off < 128 * 1024 * 1024);
    }

    if (off <= 1024 * 1024) {
      auto imm19 = -static_cast<ptrdiff_t>(off) / 4;
      if (jmp.kind == Jump::Cbz) {
        if (jmp.cmp_is_32) {
          ASMNC(CBZw, jmp.cmp_reg, imm19);
        } else {
          ASMNC(CBZx, jmp.cmp_reg, imm19);
        }
      } else {
        if (jmp.cmp_is_32) {
          ASMNC(CBNZw, jmp.cmp_reg, imm19);
        } else {
          ASMNC(CBNZx, jmp.cmp_reg, imm19);
        }
      }

      if (is_pending) {
        this->assembler.add_unresolved_entry(
            target_label,
            this->assembler.text_cur_off() - 4,
            Assembler::UnresolvedEntryKind::COND_BR);
      }
    } else {
      assert(!is_pending);
      this->assembler.text_ensure_space(2 * 4);

      if (jmp.kind == Jump::Cbz) {
        if (jmp.cmp_is_32) { // need to jump over 2 instructions
          ASMNC(CBNZw, jmp.cmp_reg, 2);
        } else {
          ASMNC(CBNZx, jmp.cmp_reg, 2);
        }
      } else {
        if (jmp.cmp_is_32) {
          ASMNC(CBZw, jmp.cmp_reg, 2);
        } else {
          ASMNC(CBZx, jmp.cmp_reg, 2);
        }
      }
      // + 4 since we alrady wrote the cb(n)z instruction
      ASMNC(B, -static_cast<ptrdiff_t>(off + 4) / 4);
    }
    return;
  }

  if (jmp.kind == Jump::Tbz || jmp.kind == Jump::Tbnz) {
    u32 off = 0;
    if (!is_pending) {
      const auto label_off = this->assembler.label_offset(target_label);
      const auto cur_off = this->assembler.text_cur_off();
      assert(cur_off >= label_off);
      off = cur_off - label_off;
      assert((off & 0b11) == 0);
      assert(off < 128 * 1024 * 1024);
    }

    if (off <= 32 * 1024) {
      auto imm14 = -static_cast<ptrdiff_t>(off) / 4;
      if (jmp.kind == Jump::Tbz) {
        ASMNC(TBZ, jmp.cmp_reg, jmp.test_bit, imm14);
      } else {
        ASMNC(TBNZ, jmp.cmp_reg, jmp.test_bit, imm14);
      }

      if (is_pending) {
        this->assembler.add_unresolved_entry(
            target_label,
            this->assembler.text_cur_off() - 4,
            Assembler::UnresolvedEntryKind::TEST_BR);
      }
    } else {
      assert(!is_pending);
      this->assembler.text_ensure_space(2 * 4);

      if (jmp.kind == Jump::Tbz) {
        // need to jump over 2 instructions
        ASMNC(TBNZ, jmp.cmp_reg, jmp.test_bit, 2);
      } else {
        ASMNC(TBZ, jmp.cmp_reg, jmp.test_bit, 2);
      }
      // + 4 since we alrady wrote the tb(n)z instruction
      ASMNC(B, -static_cast<ptrdiff_t>(off + 4) / 4);
    }
    return;
  }

  Da64Cond cond, cond_compl;
  switch (jmp.kind) {
  case Jump::Jeq:
    cond = DA_EQ;
    cond_compl = DA_NE;
    break;
  case Jump::Jne:
    cond = DA_NE;
    cond_compl = DA_EQ;
    break;
  case Jump::Jcs:
    cond = DA_CS;
    cond_compl = DA_CC;
    break;
  case Jump::Jcc:
    cond = DA_CC;
    cond_compl = DA_CS;
    break;
  case Jump::Jmi:
    cond = DA_MI;
    cond_compl = DA_PL;
    break;
  case Jump::Jpl:
    cond = DA_PL;
    cond_compl = DA_MI;
    break;
  case Jump::Jvs:
    cond = DA_VS;
    cond_compl = DA_VC;
    break;
  case Jump::Jvc:
    cond = DA_VC;
    cond_compl = DA_VS;
    break;
  case Jump::Jhi:
    cond = DA_HI;
    cond_compl = DA_LS;
    break;
  case Jump::Jls:
    cond = DA_LS;
    cond_compl = DA_HI;
    break;
  case Jump::Jge:
    cond = DA_GE;
    cond_compl = DA_LT;
    break;
  case Jump::Jlt:
    cond = DA_LT;
    cond_compl = DA_GE;
    break;
  case Jump::Jgt:
    cond = DA_GT;
    cond_compl = DA_LE;
    break;
  case Jump::Jle:
    cond = DA_LE;
    cond_compl = DA_GT;
    break;
  default: TPDE_UNREACHABLE("invalid jump kind");
  }


  u32 off = 0;
  if (!is_pending) {
    const auto label_off = this->assembler.label_offset(target_label);
    const auto cur_off = this->assembler.text_cur_off();
    assert(cur_off >= label_off);
    off = cur_off - label_off;
    assert((off & 0b11) == 0);
    assert(off < 128 * 1024 * 1024);
  }

  if (off <= 1024 * 1024) {
    ASMNC(BCOND, cond, -static_cast<ptrdiff_t>(off) / 4);

    if (is_pending) {
      this->assembler.add_unresolved_entry(
          target_label,
          this->assembler.text_cur_off() - 4,
          Assembler::UnresolvedEntryKind::COND_BR);
    }
  } else {
    assert(!is_pending);
    this->assembler.text_ensure_space(2 * 4);

    // 2 to skip over the branch following
    ASMNC(BCOND, cond_compl, 2);
    // + 4 since we alrady wrote the branch instruction
    ASMNC(B, -static_cast<ptrdiff_t>(off + 4) / 4);
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> class BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::generate_raw_set(
    Jump jmp, AsmReg dst) noexcept {
  this->assembler.text_ensure_space(4);
  switch (jmp.kind) {
  case Jump::Jeq: ASMNC(CSETw, dst, DA_EQ); break;
  case Jump::Jne: ASMNC(CSETw, dst, DA_NE); break;
  case Jump::Jcs: ASMNC(CSETw, dst, DA_CS); break;
  case Jump::Jcc: ASMNC(CSETw, dst, DA_CC); break;
  case Jump::Jmi: ASMNC(CSETw, dst, DA_MI); break;
  case Jump::Jpl: ASMNC(CSETw, dst, DA_PL); break;
  case Jump::Jvs: ASMNC(CSETw, dst, DA_VS); break;
  case Jump::Jvc: ASMNC(CSETw, dst, DA_VC); break;
  case Jump::Jhi: ASMNC(CSETw, dst, DA_HI); break;
  case Jump::Jls: ASMNC(CSETw, dst, DA_LS); break;
  case Jump::Jge: ASMNC(CSETw, dst, DA_GE); break;
  case Jump::Jlt: ASMNC(CSETw, dst, DA_LT); break;
  case Jump::Jgt: ASMNC(CSETw, dst, DA_GT); break;
  case Jump::Jle: ASMNC(CSETw, dst, DA_LE); break;
  case Jump::jmp: ASMNC(CSETw, dst, DA_AL); break;
  default: TPDE_UNREACHABLE("invalid condition for set/mask");
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> class BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::generate_raw_mask(
    Jump jmp, AsmReg dst) noexcept {
  this->assembler.text_ensure_space(4);
  switch (jmp.kind) {
  case Jump::Jeq: ASMNC(CSETMx, dst, DA_EQ); break;
  case Jump::Jne: ASMNC(CSETMx, dst, DA_NE); break;
  case Jump::Jcs: ASMNC(CSETMx, dst, DA_CS); break;
  case Jump::Jcc: ASMNC(CSETMx, dst, DA_CC); break;
  case Jump::Jmi: ASMNC(CSETMx, dst, DA_MI); break;
  case Jump::Jpl: ASMNC(CSETMx, dst, DA_PL); break;
  case Jump::Jvs: ASMNC(CSETMx, dst, DA_VS); break;
  case Jump::Jvc: ASMNC(CSETMx, dst, DA_VC); break;
  case Jump::Jhi: ASMNC(CSETMx, dst, DA_HI); break;
  case Jump::Jls: ASMNC(CSETMx, dst, DA_LS); break;
  case Jump::Jge: ASMNC(CSETMx, dst, DA_GE); break;
  case Jump::Jlt: ASMNC(CSETMx, dst, DA_LT); break;
  case Jump::Jgt: ASMNC(CSETMx, dst, DA_GT); break;
  case Jump::Jle: ASMNC(CSETMx, dst, DA_LE); break;
  case Jump::jmp: ASMNC(CSETMx, dst, DA_AL); break;
  default: TPDE_UNREACHABLE("invalid condition for set/mask");
  }
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::spill_before_call(
    const CallingConv calling_conv, const u64 except_mask) {
  // TODO FIXME: need to make sure that the upper 64bit of vector registers
  // are not treated as callee-saved
  for (auto reg_id : util::BitSetIterator<>{this->register_file.used &
                                            ~calling_conv.callee_saved_mask() &
                                            ~except_mask}) {
    auto reg = AsmReg{reg_id};
    assert(!this->register_file.is_fixed(reg));
    assert(this->register_file.reg_local_idx(reg) !=
           Base::INVALID_VAL_LOCAL_IDX);

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
          template <typename, typename, typename> typename BaseTy,
          typename Config>
void CompilerA64<Adaptor, Derived, BaseTy, Config>::generate_call(
    std::variant<Assembler::SymRef, ScratchReg, ValuePartRef> &&target,
    std::span<CallArg> arguments,
    std::span<ValuePart> results,
    CallingConv calling_conv,
    bool) {
  if (std::holds_alternative<ScratchReg>(target)) {
    assert(((1ull << std::get<ScratchReg>(target).cur_reg().id()) &
            calling_conv.arg_regs_mask()) == 0);
  }

  const auto stack_space_used = util::align_up(
      calling_conv.calculate_call_stack_space(this, arguments), 16);

  if (stack_space_used) {
    ASM(SUBxi, DA_SP, DA_SP, stack_space_used);
  }

  // Make sure arguments are locked up to the call.
  util::SmallVector<ScratchReg, 8> arg_regs;
  calling_conv.handle_call_args(this, arguments, arg_regs);

  u64 except_mask = 0;
  if (std::holds_alternative<ScratchReg>(target)) {
    except_mask = (1ull << std::get<ScratchReg>(target).cur_reg().id());
  } else if (std::holds_alternative<ValuePartRef>(target)) {
    auto &ref = std::get<ValuePartRef>(target);
    if (ref.assignment().register_valid()) {
      except_mask = (1ull << ref.assignment().full_reg_id());
    }
  }
  for (const auto &reg : arg_regs) {
    except_mask |= 1ull << reg.cur_reg().id();
  }
  spill_before_call(calling_conv, except_mask);

  if (std::holds_alternative<Assembler::SymRef>(target)) {
    this->assembler.text_ensure_space(4);
    ASMNC(BL, 0);
    this->assembler.reloc_text(std::get<Assembler::SymRef>(target),
                               R_AARCH64_CALL26,
                               this->assembler.text_cur_off() - 4);
  } else if (std::holds_alternative<ScratchReg>(target)) {
    auto &reg = std::get<ScratchReg>(target);
    assert(reg.has_reg());
    ASM(BLR, reg.cur_reg());

    if (((1ull << reg.cur_reg().id()) & calling_conv.callee_saved_mask()) ==
        0) {
      reg.reset();
    }
  } else {
    assert(std::holds_alternative<ValuePartRef>(target));
    auto &ref = std::get<ValuePartRef>(target);
    AsmReg reg;
    if (!ref.has_assignment()) {
      assert(((1ull << AsmReg::R9) & (calling_conv.callee_saved_mask() |
                                      calling_conv.arg_regs_mask())) == 0);
      this->register_file.clobbered |= (1ull << AsmReg::R9);
      reg = ref.reload_into_specific(derived(), AsmReg::R9);
    } else {
      reg = ref.load_to_reg();
      if (((1ull << reg.id()) & calling_conv.callee_saved_mask()) == 0) {
        ref.spill();
        ref.unlock();
        this->register_file.unmark_used(reg);
        ref.assignment().set_register_valid(false);
      }
    }

    ASM(BLR, reg);
  }

  arg_regs.clear();

  if (stack_space_used) {
    ASM(ADDxi, DA_SP, DA_SP, stack_space_used);
  }

  calling_conv.fill_call_results(this, results);
}

template <IRAdaptor Adaptor,
          typename Derived,
          template <typename, typename, typename> typename BaseTy,
          typename Config>
CompilerA64<Adaptor, Derived, BaseTy, Config>::ScratchReg
    CompilerA64<Adaptor, Derived, BaseTy, Config>::tls_get_addr(
        Assembler::SymRef sym, TLSModel model) noexcept {
  switch (model) {
  default: // TODO: implement optimized access for non-gd-model
  case TLSModel::GlobalDynamic: {
    ScratchReg r0_scratch{this};
    AsmReg r0 = r0_scratch.alloc_specific(AsmReg::R0);
    ScratchReg r1_scratch{this};
    AsmReg r1 = r1_scratch.alloc_specific(AsmReg::R1);
    // The call only clobbers flags, x0, x1, and lr. x0 and x1 are already fixed
    // in the scratch registers, so only make sure that lr isn't used otherwise.
    spill_before_call(CallingConv::SYSV_CC, ~(1ull << AsmReg{AsmReg::LR}.id()));

    this->assembler.text_ensure_space(0x18);
    this->assembler.reloc_text(
        sym, R_AARCH64_TLSDESC_ADR_PAGE21, this->assembler.text_cur_off(), 0);
    ASMNC(ADRP, r0, 0, 0);
    this->assembler.reloc_text(
        sym, R_AARCH64_TLSDESC_LD64_LO12, this->assembler.text_cur_off(), 0);
    ASMNC(LDRxu, r1, r0, 0);
    this->assembler.reloc_text(
        sym, R_AARCH64_TLSDESC_ADD_LO12, this->assembler.text_cur_off(), 0);
    ASMNC(ADDxi, r0, r0, 0);
    this->assembler.reloc_text(
        sym, R_AARCH64_TLSDESC_CALL, this->assembler.text_cur_off(), 0);
    ASMNC(BLR, r1);
    ASMNC(MRS, r1, 0xde82); // TPIDR_EL0
    // TODO: maybe return expr x0+x1.
    ASMNC(ADDx, r0, r1, r0);
    return r0_scratch;
  }
  }
}

} // namespace tpde::a64
