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

  ValuePartRef(CompilerBase *compiler, u32 bank = 0) noexcept
      : state{ConstantData{.data = nullptr, .bank = bank}}, compiler(compiler) {
    assert(bank < Config::NUM_BANKS);
  }

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
    assert(data && "constant data must not be null");
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

  bool is_const() const noexcept {
    return !state.c.has_assignment && state.c.data;
  }

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

  bool has_reg() const noexcept {
    if (!has_assignment()) {
      return state.c.reg.valid();
    } else {
      return state.v.locked;
    }
  }

private:
  AsmReg alloc_reg_impl(u64 exclusion_mask, bool reload) noexcept;
  AsmReg alloc_specific_impl(AsmReg reg, bool reload) noexcept;

public:
  /// Allocate and lock a register for the value part, *without* reloading the
  /// value. Does nothing if a register is already allocated.
  AsmReg alloc_reg(u64 exclusion_mask = 0) noexcept {
    return alloc_reg_impl(exclusion_mask, /*reload=*/false);
  }

  /// Allocate and lock a specific register for the value part, spilling the
  /// register if it is currently used (must not be fixed), *without* reloading
  /// or copying the value into the new register. An existing register is
  /// discarded. Value part must not be a fixed assignment.
  void alloc_specific(AsmReg reg) noexcept { alloc_specific_impl(reg, false); }

  /// Allocate, fill, and lock a register for the value part, reloading from
  /// the stack or materializing the constant if necessary. Does nothing if a
  /// register is already allocated.
  AsmReg load_to_reg() noexcept { return alloc_reg_impl(0, /*reload=*/true); }

  /// Allocate, fill, and lock a specific register for the value part, spilling
  /// the register if it is currently used (must not be fixed). The value is
  /// moved (assignment updated) or reloaded to this register. Value part must
  /// not be a fixed assignment.
  ///
  /// \warning Do not overwrite the register content as it is not saved
  /// \note The target register or the current value part may not be fixed
  void load_to_specific(AsmReg reg) noexcept { alloc_specific_impl(reg, true); }

  void move_into_specific(AsmReg reg) noexcept { load_to_specific(reg); }

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
                                    AsmReg reg,
                                    unsigned size = 0) noexcept;

  void lock() noexcept;
  void unlock() noexcept;

  void set_value(ValuePartRef &&other) noexcept;

  bool can_salvage(u32 ref_adjust = 1) const noexcept;

private:
  AsmReg salvage_keep_used() noexcept;

public:
  // only call when can_salvage returns true and a register is known to be
  // allocated
  AsmReg salvage() noexcept {
    AsmReg reg = salvage_keep_used();
    compiler->register_file.unmark_used(reg);
    return reg;
  }

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
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::alloc_reg_impl(
        u64 exclusion_mask, const bool reload) noexcept {
  u32 bank;
  if (has_assignment()) {
    auto ap = assignment();
    if (ap.fixed_assignment()) {
      assert(!ap.variable_ref());
      state.v.reg = AsmReg{ap.full_reg_id()};
      assert(compiler->register_file.is_used(state.v.reg));
      assert(compiler->register_file.is_fixed(state.v.reg));
      assert(compiler->register_file.reg_local_idx(state.v.reg) == local_idx());
      assert((exclusion_mask & (1 << state.v.reg.id())) == 0 &&
             "moving fixed registers in alloc_reg is unsupported");
      return state.v.reg;
    }

    if (ap.register_valid()) {
      lock();
      // TODO: implement this if needed
      assert((exclusion_mask & (1 << state.v.reg.id())) == 0 &&
             "moving registers in alloc_reg is unsupported");
      return AsmReg{ap.full_reg_id()};
    }

    bank = ap.bank();
  } else {
    if (state.c.reg.valid()) {
      // TODO: implement this if needed
      assert((exclusion_mask & (1 << state.c.reg.id())) == 0 &&
             "moving temporary registers in alloc_reg is unsupported");
      return state.c.reg;
    }
    bank = state.c.bank;
  }

  auto &reg_file = compiler->register_file;
  auto reg = reg_file.find_first_free_excluding(bank, exclusion_mask);
  // TODO(ts): need to grab this from the registerfile since the reg type
  // could be different
  if (reg.invalid()) {
    reg = reg_file.find_clocked_nonfixed_excluding(bank, exclusion_mask);

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
      assert(is_const() && "cannot reload temporary value");
      compiler->derived()->materialize_constant(
          state.c.data, state.c.bank, state.c.size, reg);
    }
  }

  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::alloc_specific_impl(
        AsmReg reg, const bool reload) noexcept {
  if (has_assignment()) {
    auto ap = assignment();
    assert(!ap.fixed_assignment());

    if (ap.register_valid() && ap.full_reg_id() == reg.id()) {
      lock();
      return AsmReg{ap.full_reg_id()};
    }

    unlock();
  } else if (state.c.reg == reg) {
    return state.c.reg;
  }

  auto &reg_file = compiler->register_file;
  if (reg_file.is_used(reg)) {
    assert(!reg_file.is_fixed(reg));

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
    auto old_reg = AsmReg::make_invalid();
    if (ap.register_valid()) {
      old_reg = AsmReg{ap.full_reg_id()};
    }

    ap.set_full_reg_id(reg.id());
    ap.set_register_valid(true);

    // We must lock the value here, otherwise, load_from_stack could evict the
    // register again.
    lock();

    if (reload) {
      if (old_reg.valid()) {
        compiler->derived()->mov(reg, old_reg, ap.part_size());
        reg_file.unmark_used(old_reg);
      } else if (ap.variable_ref()) {
        compiler->derived()->load_address_of_var_reference(reg, ap);
      } else {
        compiler->derived()->load_from_stack(
            reg, ap.frame_off(), ap.part_size());
      }
    }
  } else {
    reg_file.mark_used(reg, INVALID_VAL_LOCAL_IDX, 0);
    reg_file.mark_fixed(reg);

    if (reload) {
      if (state.c.reg.valid()) {
        // TODO: size
        compiler->derived()->mov(reg, state.c.reg, 8);
        reg_file.unmark_fixed(state.c.reg);
        reg_file.unmark_used(state.c.reg);
      } else {
        assert(is_const() && "cannot reload temporary value");
        compiler->derived()->materialize_constant(
            state.c.data, state.c.bank, state.c.size, reg);
      }
    }

    state.c.reg = reg;
  }

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
                                   AsmReg reg,
                                   unsigned size) noexcept {
  if (is_const()) {
    // TODO(ts): store a compiler* in the constant data?
    compiler->derived()->materialize_constant(*this, reg);
    return reg;
  }
  if (!has_assignment()) {
    assert(has_reg());
    assert(reg != cur_reg());
    // TODO: value size
    assert(size != 0);
    compiler->derived()->mov(reg, cur_reg(), size);
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
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::set_value(
    ValuePartRef &&other) noexcept {
  auto &reg_file = compiler->register_file;
  if (!has_assignment()) {
    assert(!is_const()); // probably don't want to allow mutating constants
    // This is a temporary, which might currently have a register. We want to
    // have a temporary register that holds the value at the end.
    if (!other.has_assignment()) {
      // When other is a temporary/constant, just take the value and drop our
      // own register (if we have any).
      *this = std::move(other);
      return;
    }

    if (!other.can_salvage()) {
      // We cannot take the register of other, so copy the value
      AsmReg cur_reg = alloc_reg();
      other.reload_into_specific_fixed(compiler, cur_reg);
      other.reset();
      return;
    }

    // We can take the register of other.
    reset();

    state.c.reg = other.salvage_keep_used();
    reg_file.mark_fixed(state.c.reg);
    reg_file.update_reg_assignment(state.c.reg, INVALID_VAL_LOCAL_IDX, 0);
    return;
  }

  // Update the value of the assignment part
  auto ap = assignment();
  assert(!ap.variable_ref() && "cannot update variable ref");

  if (ap.fixed_assignment() || !other.can_salvage()) {
    // Source value owns no register or it is not reusable: copy value
    AsmReg cur_reg = alloc_reg();
    other.reload_into_specific_fixed(compiler, cur_reg, ap.part_size());
    other.reset();
    ap.set_register_valid(true);
    ap.set_modified(true);
    return;
  }

  // Reuse register of other assignment
  if (ap.register_valid()) {
    // If we currently have a register, drop it
    unlock();
    AsmReg cur_reg{ap.full_reg_id()};
    assert(!reg_file.is_fixed(cur_reg));
    reg_file.unmark_used(cur_reg);
  }

  AsmReg new_reg = other.salvage_keep_used();
  reg_file.update_reg_assignment(new_reg, local_idx(), part());
  ap.set_full_reg_id(new_reg.id());
  ap.set_register_valid(true);
  ap.set_modified(true);
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
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::
        salvage_keep_used() noexcept {
  assert(can_salvage());
  if (!has_assignment()) {
    AsmReg reg = state.c.reg;
    compiler->register_file.unmark_fixed(reg);
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
