// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::ValuePartRef {
    struct ConstantData {
        u64 const_parts[2];
        u32 bank;
        u32 size;
    };

    struct ValueData {
        CompilerBase::ValLocalIdx local_idx;
        u32                       part;
        ValueAssignment          *assignment;
        CompilerBase             *compiler;
        bool                      locked;
    };

    union {
        ConstantData c;
        ValueData    v;
    } state;

    bool is_const;

    ValuePartRef() noexcept
        : state{
            ConstantData{0, 0, 0}
    }, is_const(true) {}

    ValuePartRef(CompilerBase *compiler, ValLocalIdx local_idx, u32 part)
        : state {
        .v = ValueData {
            .local_idx = local_idx,
            .part = part,
            .assignment = compiler->val_assignment(local_idx),
            .compiler = compiler,
            .locked = false
        }
    }, is_const(false) {}

    explicit ValuePartRef(const ValuePartRef &) = delete;

    ValuePartRef(ValuePartRef &&other) noexcept {
        this->state                  = other.state;
        this->is_const               = other.is_const;
        other.is_const               = true;
        other.state.c.bank           = 0;
        other.state.c.const_parts[0] = 0;
        other.state.c.const_parts[1] = 0;
    }

    ~ValuePartRef() noexcept { reset(); }

    ValuePartRef &operator=(const ValuePartRef &) = delete;

    ValuePartRef &operator=(ValuePartRef &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        reset();
        this->state                  = other.state;
        this->is_const               = other.is_const;
        other.state.c.bank           = 0;
        other.state.c.const_parts[0] = 0;
        other.state.c.const_parts[1] = 0;
    }

    [[nodiscard]] AssignmentPartRef assignment() const noexcept {
        assert(!is_const);
        return AssignmentPartRef{state.v.assignment, state.v.part};
    }

    /// Increment the reference count artificially
    void inc_ref_count() noexcept;

    /// Spill the value part to the stack frame
    void spill() noexcept;

    /// If it is known that the value part has a register, this function can be
    /// used to quickly access it
    AsmReg cur_reg() noexcept;

    /// Allocate a register for the value part and reload from the stack if this
    /// is desired
    AsmReg alloc_reg(bool reload = true) noexcept;

    /// Load the value into the specific register and update the assignment to
    /// reflect that
    ///
    /// \warning Do not overwrite the register content as it is not saved
    /// \note The target register may not be fixed
    AsmReg move_into_specific(AsmReg reg) noexcept;

    /// Load the value into the specific register *without* updating the
    /// assignment (or freeing the assignment if the value is in the target
    /// register) meaning you are free to overwrite the register if desired
    ///
    /// \note This will spill the register if the value is currently in the
    /// specified register
    AsmReg reload_into_specific(AsmReg reg) noexcept;

    void lock() noexcept;
    void unlock() noexcept;

    bool can_salvage(u32 ref_adjust = 1) const noexcept;

    ValLocalIdx local_idx() const noexcept {
        assert(!is_const);
        return state.v.local_idx;
    }

    u32 part() const noexcept {
        assert(!is_const);
        return state.v.part;
    }

    u32 ref_count() const noexcept {
        assert(!is_const);
        return state.v.assignment->references_left;
    }

    u32 bank() const noexcept {
        return is_const ? state.c.bank : assignment().bank();
    }

    u32 part_size() const noexcept {
        return is_const ? state.c.size : assignment().part_size();
    }

    /// Reset the reference to the value part
    void reset() noexcept;

    void reset_without_refcount() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::
    inc_ref_count() noexcept {
    if (is_const) {
        return;
    }

    ++state.v.assignment->references_left;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::spill() noexcept {
    assert(!is_const);

    auto ap = assignment();
    if (!ap.register_valid()) {
        return;
    }

    ap.spill_if_needed(state.v.compiler);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::cur_reg() noexcept {
    assert(!is_const);

    auto ap = assignment();
    assert(ap.register_valid());

    return AsmReg{ap.full_reg_id()};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::alloc_reg(
        const bool reload) noexcept {
    assert(!is_const);

    auto pa = assignment();
    if (pa.register_valid()) {
        return AsmReg{pa.full_reg_id()};
    }

    auto &reg_file = state.v.compiler->register_file;
    auto  reg      = reg_file.find_first_free_excluding(pa.bank(), 0);
    // TODO(ts): need to grab this from the registerfile since the reg type
    // could be different
    if (reg.invalid()) {
        reg = reg_file.find_clocked_nonfixed_excluding(pa.bank(), 0);

        if (reg.invalid()) {
            TPDE_LOG_ERR("ran out of registers for value part");
            assert(0);
            exit(1);
        }

        AssignmentPartRef part{
            state.v.compiler->val_assignment(reg_file.reg_local_idx(reg)),
            reg_file.reg_part(reg)};
        part.spill_if_needed(state.v.compiler);
        part.set_register_valid(false);
        reg_file.unmark_used(reg);
    }

    reg_file.mark_used(reg, state.v.local_idx, state.v.part);
    auto ap = assignment();
    ap.set_full_reg_id(reg.id());
    ap.set_register_valid(true);

    if (reload) {
        if (ap.variable_ref()) {
            state.v.compiler->derived()->load_address_of_var_reference(reg, ap);
        } else {
            state.v.compiler->derived()->load_from_stack(
                reg, ap.frame_off(), ap.part_size());
        }
    }

    return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::move_into_specific(
        const AsmReg reg) noexcept {
    assert(!is_const);

    auto  ap       = assignment();
    auto &reg_file = state.v.compiler->register_file;
    if (ap.register_valid()) {
        if (ap.full_reg_id() == reg.id()) {
            return reg;
        }
    }

    if (reg_file.is_used(reg)) {
        assert(!reg_file.is_fixed(reg));

        auto ap = AssignmentPartRef{
            state.v.compiler->val_assignment(reg_file.reg_local_idx(reg)),
            reg_file.reg_part(reg)};
        ap.spill_if_needed(state.v.compiler);
        ap.set_register_valid(false);
        reg_file.unmark_used(reg);
    }

    if (ap.register_valid()) {
        state.v.compiler->derived()->mov(reg, AsmReg{ap.full_reg_id()});
    } else {
        if (ap.variable_ref()) {
            state.v.compiler->derived()->load_address_of_var_reference(reg, ap);
        } else {
            state.v.compiler->derived()->load_from_stack(
                reg, ap.frame_off(), ap.part_size());
        }
        ap.set_register_valid(true);
    }

    ap.set_full_reg_id(reg.id());
    reg_file.mark_used(reg, state.v.local_idx, state.v.part);
    return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ValuePartRef::reload_into_specific(
        const AsmReg reg) noexcept {
    if (is_const) {
        // TODO(ts): materialize constant
        assert(0);
        exit(1);
    }

    auto ap = assignment();
    if (ap.register_valid()) {
        if (ap.full_reg_id() == reg.id()) {
            ap.spill_if_needed(state.v.compiler);
            ap.set_register_valid(false);
            return reg;
        }

        state.v.compiler->derived()->mov(reg, AsmReg{ap.full_reg_id()});
    } else {
        if (ap.variable_ref()) {
            state.v.compiler->derived()->load_address_of_var_reference(reg, ap);
        } else {
            state.v.compiler->derived()->load_from_stack(
                reg, ap.frame_off(), ap.part_size());
        }
    }

    return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::lock() noexcept {
    if (state.v.locked) {
        return;
    }

    auto ap = assignment();
    assert(ap.register_valid());

    const auto reg = AsmReg{ap.full_reg_id()};
    state.v.compiler->register_file.mark_fixed(reg);
    state.v.compiler->register_file.inc_lock_count(reg);

    state.v.locked = true;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::unlock() noexcept {
    if (!state.v.locked) {
        return;
    }

    auto ap = assignment();
    assert(ap.register_valid());

    const auto reg = AsmReg{ap.full_reg_id()};
    if (state.v.compiler->register_file.dec_lock_count(reg) == 0) {
        state.v.compiler->register_file.unmark_fixed(reg);
    }

    state.v.locked = false;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::ValuePartRef::can_salvage(
    const u32 ref_adjust) const noexcept {
    if (is_const) {
        return false;
    }

    const auto &liveness =
        state.v.compiler->analyzer.liveness_info((u32)state.v.local_idx);
    if (ref_count() <= ref_adjust
        && (liveness.last < state.v.compiler->cur_block_idx
            || (liveness.last == state.v.compiler->cur_block_idx
                && !liveness.last_full))) {
        return true;
    }

    return false;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::reset() noexcept {
    if (is_const) {
        return;
    }

    if (state.v.locked) {
        unlock();
    }

    if (state.v.assignment->references_left == 0) {
        assert(assignment().variable_ref());
        is_const = true;
        state.c  = ConstantData{
             {0, 0},
             0
        };
        return;
    }

    if (--state.v.assignment->references_left != 0) {
        is_const = true;
        state.c  = ConstantData{
             {0, 0},
             0
        };
        return;
    }

    if (const auto &liveness = state.v.compiler->analyzer.liveness_info(
            static_cast<u32>(state.v.local_idx));
        liveness.last_full
        && liveness.last != state.v.compiler->cur_block_idx) {
        // need to wait until release
        auto &free_list_head =
            state.v.compiler->assignments
                .delayed_free_lists[static_cast<u32>(liveness.last)];
        state.v.assignment->next_delayed_free_entry = free_list_head;
        free_list_head                              = state.v.local_idx;
    } else {
        state.v.compiler->free_assignment(state.v.local_idx);
    }

    is_const = true;
    state.c  = ConstantData{
         {0, 0},
         0
    };
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::
    reset_without_refcount() noexcept {
    if (is_const) {
        return;
    }

    if (state.v.locked) {
        unlock();
    }

    is_const = true;
    state.c  = ConstantData{
         {0, 0},
         0
    };
}
} // namespace tpde
