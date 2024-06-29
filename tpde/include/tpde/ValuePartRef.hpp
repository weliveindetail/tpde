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

    [[nodiscard]] AssignmentPartRef assignment() const noexcept {
        assert(!is_const);
        return AssignmentPartRef{state.v.assignment, state.v.part};
    }

    void spill() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValuePartRef::spill() noexcept {
    assert(!is_const);
    assert(0);
}
} // namespace tpde
