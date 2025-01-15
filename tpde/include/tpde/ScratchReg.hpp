// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {
template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::ScratchReg {
  CompilerBase *compiler;
  // TODO(ts): get this using the CompilerConfig?
  AsmReg cur_reg = AsmReg::make_invalid();

  explicit ScratchReg(CompilerBase *compiler) : compiler(compiler) {}

  explicit ScratchReg(const ScratchReg &) = delete;
  ScratchReg(ScratchReg &&) noexcept;

  ~ScratchReg() noexcept { reset(); }

  ScratchReg &operator=(const ScratchReg &) = delete;
  ScratchReg &operator=(ScratchReg &&) noexcept;

  AsmReg alloc_specific(AsmReg reg) noexcept;

  AsmReg alloc_gp() noexcept { return alloc(Config::GP_BANK); }

  /// Allocate register in the specified bank, optionally excluding certain
  /// non-fixed registers. Spilling can be disabled for spill code to avoid
  /// recursion; if spilling is disabled, the allocation can fail.
  AsmReg alloc(u8 bank,
               u64 exclusion_mask = 0,
               bool spill_if_needed = true) noexcept;

  void reset() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::ScratchReg::ScratchReg(
    ScratchReg &&other) noexcept {
  this->compiler = other.compiler;
  this->cur_reg = other.cur_reg;
  other.cur_reg = AsmReg::make_invalid();
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ScratchReg &
    CompilerBase<Adaptor, Derived, Config>::ScratchReg::operator=(
        ScratchReg &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  reset();
  this->compiler = other.compiler;
  this->cur_reg = other.cur_reg;
  other.cur_reg = AsmReg::make_invalid();
  return *this;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ScratchReg::alloc_specific(
        AsmReg reg) noexcept {
  assert(!compiler->register_file.is_fixed(reg));
  reset();

  if (compiler->register_file.is_used(reg)) {
    AssignmentPartRef part{
        compiler->val_assignment(compiler->register_file.reg_local_idx(reg)),
        compiler->register_file.reg_part(reg)};
    part.spill_if_needed(compiler);
    part.set_register_valid(false);
    compiler->register_file.unmark_used(reg);
  }

  compiler->register_file.mark_used(reg, INVALID_VAL_LOCAL_IDX, 0);
  compiler->register_file.mark_clobbered(reg);
  compiler->register_file.mark_fixed(reg);
  cur_reg = reg;
  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ScratchReg::alloc(
        u8 bank, u64 exclusion_mask, bool spill_if_needed) noexcept {
  auto &reg_file = compiler->register_file;
  if (!cur_reg.invalid()) {
    if (bank == reg_file.reg_bank(cur_reg) &&
        (exclusion_mask & (1ull << cur_reg.id())) == 0) {
      return cur_reg;
    }
    reset();
  }

  // TODO(ts): try to first find a non callee-saved/clobbered register...
  auto reg = reg_file.find_first_free_excluding(bank, exclusion_mask);
  if (reg.invalid()) {
    if (!spill_if_needed) {
      return AsmReg::make_invalid();
    }

    // TODO(ts): use clock here?
    reg = reg_file.find_first_nonfixed_excluding(bank, exclusion_mask);
    if (reg.invalid()) [[unlikely]] {
      TPDE_LOG_ERR("ran out of registers for scratch registers");
      assert(0);
      exit(1);
    }

    AssignmentPartRef part{
        compiler->val_assignment(reg_file.reg_local_idx(reg)),
        reg_file.reg_part(reg)};
    part.spill_if_needed(compiler);
    part.set_register_valid(false);
    reg_file.unmark_used(reg);
  }

  reg_file.mark_used(reg, INVALID_VAL_LOCAL_IDX, 0);
  reg_file.mark_clobbered(reg);
  reg_file.mark_fixed(reg);
  cur_reg = reg;
  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ScratchReg::reset() noexcept {
  if (cur_reg.invalid()) {
    return;
  }

  compiler->register_file.unmark_fixed(cur_reg);
  compiler->register_file.unmark_used(cur_reg);
  cur_reg = AsmReg::make_invalid();
}
} // namespace tpde
