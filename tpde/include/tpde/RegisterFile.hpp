// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {


template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct tpde::CompilerBase<Adaptor, Derived, Config>::RegisterFile {
    // later add the possibility for more than 64 registers
    // for architectures that require it

    u64 used, free, fixed, clobbered;

    struct Assignment {
        IRValueRef value;
        u32        part;
    };

    std::array<Assignment, 64> assignments;

    [[nodiscard]] bool is_used(const u8 reg) const noexcept {
        assert(reg < 64);
        return (used & 1ull << reg) != 0;
    }

    [[nodiscard]] bool is_fixed(const u8 reg) const noexcept {
        assert(reg < 64);
        return (fixed & 1ull << reg) != 0;
    }

    void mark_used(const u8         reg,
                   const IRValueRef value,
                   const u32        part) noexcept {
        assert(reg < 64);
        assert(!is_used(reg));
        assert(!is_fixed(reg));
        used             |= (1ull << reg);
        free             &= ~(1ull << reg);
        assignments[reg]  = Assignment{.value = value, .part = part};
    }

    void mark_fixed(const u8 reg) noexcept {
        assert(reg < 64);
        fixed |= (1ull << reg);
    }

    void unmark_fixed(const u8 reg) noexcept {
        assert(reg < 64);
        fixed &= ~(1ull << reg);
    }

    void mark_clobbered(const u8 reg) noexcept {
        assert(reg < 64);
        clobbered |= (1ull << reg);
    }
};
} // namespace tpde
