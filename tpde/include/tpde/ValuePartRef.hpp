// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::ValuePartRef {
    struct ConstantData {
        u64 const_parts[2];
        u32 bank; // TODO(ts): multiple banks?
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

    ~ValuePartRef() noexcept { reset(); }

    [[nodiscard]] AssignmentPartRef assignment() const noexcept {
        assert(!is_const);
        return AssignmentPartRef{state.v.assignment, state.v.part};
    }

    void spill() noexcept;
    void reset() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::spill() noexcept {
    assert(!is_const);
    assert(0);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::reset() noexcept {
    if (is_const) {
        return;
    }

    if (state.v.locked) {
        auto ap = AssignmentPartRef{state.v.assignment, state.v.part};
        assert(ap.reg_valid());
        assert(state.v.compiler->register_file.is_fixed(ap.full_reg_id()));

        if (state.v.compiler->register_file.dec_lock_count(ap.full_reg_id())
            == 0) {
            state.v.compiler->register_file.unmark_fixed(ap.full_reg_id());
        }
        state.v.locked = false;
    }

    if (state.v.assignment->references_left == 0) {
        assert(assignment().variable_ref());
        is_const = true;
        state.c  = ConstantData{0, 0, 0};
        return;
    }

    if (--state.v.assignment->references_left != 0) {
        is_const = true;
        state.c  = ConstantData{0, 0, 0};
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

        return;
    } else {
        state.v.compiler->free_assignment(state.v.local_idx);
    }

    is_const = true;
    state.c  = ConstantData{0, 0, 0};
}
} // namespace tpde
