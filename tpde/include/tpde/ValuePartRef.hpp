// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <array>
#include <cstring>
#include <span>

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::ValuePartRef {
  struct ConstantData {
    AsmReg reg = AsmReg::make_invalid();
    bool has_assignment = false;
    const u64 *data;
    u32 bank;
    u32 size;
  };

  struct ValueData {
    AsmReg reg = AsmReg::make_invalid(); // only valid if fixed/locked
    bool has_assignment = true;
    bool locked;
    CompilerBase::ValLocalIdx local_idx;
    u32 part;
    ValueAssignment *assignment;
  };

  union {
    ConstantData c;
    ValueData v;
  } state;

  CompilerBase *compiler;

  ValuePartRef(CompilerBase *compiler) noexcept
      : state{ConstantData{}}, compiler(compiler) {}

  ValuePartRef(CompilerBase *compiler, ValLocalIdx local_idx, u32 part) noexcept
      : state{
            .v = ValueData{
                           .locked = false,
                           .local_idx = local_idx,
                           .part = part,
                           .assignment = compiler->val_assignment(local_idx),
                           }
  }, compiler(compiler) {
    assert(assignment().variable_ref() || state.v.assignment->references_left);
#ifndef NDEBUG
    state.v.reg = AsmReg::make_invalid();
#endif
  }

  ValuePartRef(CompilerBase *compiler, const u64 *data, u32 size, u32 bank) noexcept
      : state{
            .c = ConstantData{.data = data, .bank = bank, .size = size}
  }, compiler(compiler) {
    assert(bank < Config::NUM_BANKS);
  }

  explicit ValuePartRef(const ValuePartRef &) = delete;

  ValuePartRef(ValuePartRef &&other) noexcept
      : state{other.state}, compiler(other.compiler) {
    other.state.c = ConstantData{};
  }

  ~ValuePartRef() noexcept { reset(); }

  ValuePartRef &operator=(const ValuePartRef &) = delete;

  ValuePartRef &operator=(ValuePartRef &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    reset();
    assert(compiler == other.compiler);
    this->state = other.state;
    other.state.c = ConstantData{};
    return *this;
  }

  bool has_assignment() const noexcept { return state.v.has_assignment; }

  bool is_const() const noexcept { return !state.c.has_assignment; }

  [[nodiscard]] AssignmentPartRef assignment() const noexcept {
    assert(has_assignment());
    return AssignmentPartRef{state.v.assignment, state.v.part};
  }

  /// Increment the reference count artificially
  void inc_ref_count() noexcept {
    if (has_assignment()) {
      ++state.v.assignment->references_left;
    }
  }

  /// Decrement the reference count artificially
  void dec_ref_count() noexcept {
    if (has_assignment()) {
      assert(state.v.assignment->references_left > 0);
      --state.v.assignment->references_left;
    }
  }

  /// Spill the value part to the stack frame
  void spill() noexcept {
    if (auto ap = assignment(); ap.register_valid()) {
      ap.spill_if_needed(compiler);
    }
  }

  /// If it is known that the value part has a register, this function can be
  /// used to quickly access it
  AsmReg cur_reg() noexcept {
    assert(state.v.reg.valid());
    return state.v.reg;
  }

  /// Is the value part currently in the specified register?
  bool is_in_reg(AsmReg reg) const noexcept {
    if (is_const()) {
      return false;
    }
    auto ap = assignment();
    return ap.register_valid() && AsmReg{ap.full_reg_id()} == reg;
  }

  /// Allocate a register for the value part, reload from the stack if this is
  /// desired, and lock the value into the register.
  AsmReg alloc_reg(bool reload = true) noexcept;

  /// Load the value into the specific register and update the assignment to
  /// reflect that
  ///
  /// \warning Do not overwrite the register content as it is not saved
  /// \note The target register or the current value part may not be fixed
  AsmReg move_into_specific(AsmReg reg) noexcept;

  /// Load the value into the specific register *without* updating the
  /// assignment (or freeing the assignment if the value is in the target
  /// register) meaning you are free to overwrite the register if desired
  ///
  /// \note The target register may not be fixed and will be free'd by this
  /// function
  ///
  /// \note This will spill the register if the value is currently in the
  /// specified register
  AsmReg reload_into_specific(CompilerBase *compiler, AsmReg reg) noexcept;

  /// This works similarily to reload_into_specific but will not free
  /// the target register
  AsmReg reload_into_specific_fixed(CompilerBase *compiler,
                                    AsmReg reg) noexcept;

  void lock() noexcept;
  void unlock() noexcept;

  bool can_salvage(u32 ref_adjust = 1) const noexcept;

  // only call when can_salvage returns true and a register is known to be
  // allocated
  AsmReg salvage() noexcept;

  ValLocalIdx local_idx() const noexcept {
    assert(has_assignment());
    return state.v.local_idx;
  }

  u32 part() const noexcept {
    assert(has_assignment());
    return state.v.part;
  }

  u32 ref_count() const noexcept {
    assert(has_assignment());
    return state.v.assignment->references_left;
  }

  u32 bank() const noexcept {
    return !has_assignment() ? state.c.bank : assignment().bank();
  }

  u32 part_size() const noexcept {
    return !has_assignment() ? state.c.size : assignment().part_size();
  }

  /// Reset the reference to the value part
  void reset() noexcept;

  void reset_without_refcount() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::alloc_reg(
        const bool reload) noexcept {
  u32 bank;
  if (has_assignment()) {
    auto ap = assignment();
    if (ap.fixed_assignment()) {
      assert(!ap.variable_ref());
      state.v.reg = AsmReg{ap.full_reg_id()};
      return state.v.reg;
    }

    if (ap.register_valid()) {
      lock();
      return AsmReg{ap.full_reg_id()};
    }

    bank = ap.bank();
  } else {
    if (state.c.reg.valid()) {
      return state.c.reg;
    }
    bank = state.c.bank;
  }

  auto &reg_file = compiler->register_file;
  auto reg = reg_file.find_first_free_excluding(bank, 0);
  // TODO(ts): need to grab this from the registerfile since the reg type
  // could be different
  if (reg.invalid()) {
    reg = reg_file.find_clocked_nonfixed_excluding(bank, 0);

    if (reg.invalid()) [[unlikely]] {
      TPDE_FATAL("ran out of registers for value part");
    }

    AssignmentPartRef evict_part{
        compiler->val_assignment(reg_file.reg_local_idx(reg)),
        reg_file.reg_part(reg)};
    evict_part.spill_if_needed(compiler);
    evict_part.set_register_valid(false);
    reg_file.unmark_used(reg);
  }

  reg_file.mark_clobbered(reg);
  if (has_assignment()) {
    reg_file.mark_used(reg, state.v.local_idx, state.v.part);
    auto ap = assignment();
    ap.set_full_reg_id(reg.id());
    ap.set_register_valid(true);

    // We must lock the value here, otherwise, load_from_stack could evict the
    // register again.
    lock();

    if (reload) {
      if (ap.variable_ref()) {
        compiler->derived()->load_address_of_var_reference(reg, ap);
      } else {
        compiler->derived()->load_from_stack(
            reg, ap.frame_off(), ap.part_size());
      }
    }
  } else {
    reg_file.mark_used(reg, INVALID_VAL_LOCAL_IDX, 0);
    reg_file.mark_fixed(reg);
    state.v.reg = reg;

    if (reload) {
      compiler->derived()->materialize_constant(
          state.c.data, state.c.bank, state.c.size, reg);
    }
  }

  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::move_into_specific(
        const AsmReg reg) noexcept {
  assert(has_assignment());

  auto ap = assignment();
  assert(!ap.fixed_assignment());
  auto &reg_file = compiler->register_file;
  if (ap.register_valid()) {
    if (ap.full_reg_id() == reg.id()) {
      return reg;
    }
  }

  if (reg_file.is_used(reg)) {
    assert(!reg_file.is_fixed(reg));

    auto ap =
        AssignmentPartRef{compiler->val_assignment(reg_file.reg_local_idx(reg)),
                          reg_file.reg_part(reg)};
    ap.spill_if_needed(compiler);
    ap.set_register_valid(false);
    reg_file.unmark_used(reg);
  }

  if (ap.register_valid()) {
    compiler->derived()->mov(reg, AsmReg{ap.full_reg_id()}, ap.part_size());

    reg_file.unmark_used(AsmReg{ap.full_reg_id()});
  } else {
    if (ap.variable_ref()) {
      compiler->derived()->load_address_of_var_reference(reg, ap);
    } else {
      compiler->derived()->load_from_stack(reg, ap.frame_off(), ap.part_size());
    }
    ap.set_register_valid(true);
  }

  ap.set_full_reg_id(reg.id());
  reg_file.mark_used(reg, state.v.local_idx, state.v.part);
  reg_file.mark_clobbered(reg);
  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::reload_into_specific(
        CompilerBase *compiler, const AsmReg reg) noexcept {
  if (is_const()) {
    // TODO(ts): store a compiler* in the constant data?
    // make sure the register is free
    ScratchReg tmp{compiler};
    tmp.alloc_specific(reg);

    compiler->derived()->materialize_constant(*this, reg);
    return reg;
  }

  assert(!state.v.locked);
  auto &reg_file = compiler->register_file;

  // Free the register if there is anything else in there
  if (reg_file.is_used(reg) &&
      (reg_file.reg_local_idx(reg) != state.v.local_idx ||
       reg_file.reg_part(reg) != state.v.part)) {
    assert(!reg_file.is_fixed(reg));
    auto ap =
        AssignmentPartRef{compiler->val_assignment(reg_file.reg_local_idx(reg)),
                          reg_file.reg_part(reg)};
    assert(!ap.fixed_assignment());
    ap.spill_if_needed(compiler);
    ap.set_register_valid(false);
    reg_file.unmark_used(reg);
  }

  auto ap = assignment();
  if (ap.register_valid()) {
    if (ap.full_reg_id() == reg.id()) {
      ap.spill_if_needed(compiler);
      ap.set_register_valid(false);
      assert(!reg_file.is_fixed(reg));
      reg_file.unmark_used(reg);
      return reg;
    }

    compiler->derived()->mov(reg, AsmReg{ap.full_reg_id()}, ap.part_size());
  } else {
    assert(!ap.fixed_assignment());
    if (ap.variable_ref()) {
      compiler->derived()->load_address_of_var_reference(reg, ap);
    } else {
      compiler->derived()->load_from_stack(reg, ap.frame_off(), ap.part_size());
    }
  }

  compiler->register_file.mark_clobbered(reg);
  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::
        reload_into_specific_fixed(CompilerBase *compiler,
                                   AsmReg reg) noexcept {
  if (is_const()) {
    // TODO(ts): store a compiler* in the constant data?
    compiler->derived()->materialize_constant(*this, reg);
    return reg;
  }

  assert(!state.v.locked);

  auto ap = assignment();
  if (ap.register_valid()) {
    assert(ap.full_reg_id() != reg.id());

    compiler->derived()->mov(reg, AsmReg{ap.full_reg_id()}, ap.part_size());
  } else {
    assert(!ap.fixed_assignment());
    if (ap.variable_ref()) {
      compiler->derived()->load_address_of_var_reference(reg, ap);
    } else {
      compiler->derived()->load_from_stack(reg, ap.frame_off(), ap.part_size());
    }
  }

  compiler->register_file.mark_clobbered(reg);
  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::lock() noexcept {
  assert(has_assignment());
  if (state.v.locked) {
    return;
  }

  auto ap = assignment();
  assert(ap.register_valid());

  const auto reg = AsmReg{ap.full_reg_id()};
  compiler->register_file.mark_fixed(reg);
  compiler->register_file.inc_lock_count(reg);

  state.v.reg = reg;
  state.v.locked = true;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::unlock() noexcept {
  assert(has_assignment());
  if (!state.v.locked) {
    return;
  }

  compiler->register_file.dec_lock_count(state.v.reg);
#ifndef NDEBUG
  state.v.reg = AsmReg::make_invalid();
#endif
  state.v.locked = false;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::ValuePartRef::can_salvage(
    const u32 ref_adjust) const noexcept {
  if (!has_assignment()) {
    return state.c.reg.valid();
  }

  if (!assignment().register_valid() || assignment().variable_ref()) {
    return false;
  }

  const auto &liveness =
      compiler->analyzer.liveness_info((u32)state.v.local_idx);
  if (ref_count() <= ref_adjust &&
      (liveness.last < compiler->cur_block_idx ||
       (liveness.last == compiler->cur_block_idx && !liveness.last_full))) {
    return true;
  }

  return false;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::salvage() noexcept {
  assert(can_salvage());
  if (!has_assignment()) {
    AsmReg reg = state.c.reg;
    compiler->register_file.unmark_fixed(reg);
    compiler->register_file.unmark_used(reg);
    state.c.reg = AsmReg::make_invalid();
    return reg;
  }

  auto ap = assignment();
  assert(ap.register_valid());
  auto cur_reg = AsmReg{ap.full_reg_id()};

  unlock();
  assert(ap.fixed_assignment() || !compiler->register_file.is_fixed(cur_reg));
  if (ap.fixed_assignment()) {
    compiler->register_file.dec_lock_count(cur_reg); // release fixed register
  }
  compiler->register_file.unmark_used(cur_reg);

  ap.set_register_valid(false);
  ap.set_fixed_assignment(false);
  return cur_reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::reset() noexcept {
  if (!has_assignment()) {
    if (!state.c.reg.valid()) {
      return;
    }
    compiler->register_file.unmark_fixed(state.c.reg);
    compiler->register_file.unmark_used(state.c.reg);
    state.c.reg = AsmReg::make_invalid();
    return;
  }

  unlock();

  auto &ref_count = state.v.assignment->references_left;
  assert(ref_count != 0 || assignment().variable_ref());
  if (ref_count != 0 && --ref_count == 0) {
    compiler->release_assignment(state.v.local_idx);
  }

  state.c.has_assignment = false;
  state.c.reg = AsmReg::make_invalid();
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::
    reset_without_refcount() noexcept {
  if (!has_assignment()) {
    if (!state.c.reg.valid()) {
      return;
    }
    compiler->register_file.unmark_fixed(state.c.reg);
    compiler->register_file.unmark_used(state.c.reg);
    state.c.reg = AsmReg::make_invalid();
    return;
  }

  if (state.v.locked) {
    unlock();
  }

  state.c = ConstantData{};
}
} // namespace tpde
