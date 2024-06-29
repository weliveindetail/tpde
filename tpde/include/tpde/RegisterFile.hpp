// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {


template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::RegisterFile {
    // later add the possibility for more than 64 registers
    // for architectures that require it

    u64 used, free, fixed, clobbered;

    struct Assignment {
        ValLocalIdx local_idx;
        u32         part;
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

    void mark_used(const u8          reg,
                   const ValLocalIdx local_idx,
                   const u32         part) noexcept {
        assert(reg < 64);
        assert(!is_used(reg));
        assert(!is_fixed(reg));
        used             |= (1ull << reg);
        free             &= ~(1ull << reg);
        assignments[reg]  = Assignment{.local_idx = local_idx, .part = part};
    }

    void unmark_used(const u8 reg) noexcept {
        assert(reg < 64);
        assert(is_used(reg));
        assert(!is_fixed(reg));
        used &= ~(1ull << reg);
        free |= (1ull << reg);
    }

    void mark_fixed(const u8 reg) noexcept {
        assert(reg < 64);
        assert(is_used(reg));
        fixed |= (1ull << reg);
    }

    void unmark_fixed(const u8 reg) noexcept {
        assert(reg < 64);
        assert(is_used(reg));
        assert(is_fixed(reg));
        fixed &= ~(1ull << reg);
    }

    void mark_clobbered(const u8 reg) noexcept {
        assert(reg < 64);
        clobbered |= (1ull << reg);
    }

    [[nodiscard]] ValLocalIdx reg_local_idx(const u8 reg) const noexcept {
        assert(is_used(reg));
        return assignments[reg].local_idx;
    }

    [[nodiscard]] u32 reg_part(const u8 reg) const noexcept {
        assert(is_used(reg));
        return assignments[reg].part;
    }

    [[nodiscard]] u8
        find_first_free_excluding(const u8  bank,
                                  const u64 exclusion_mask) const noexcept {
        assert(bank <= 1);
        const u64 free_mask = ((0xFFFF'FFFF << bank) & free) & ~exclusion_mask;
        if (free_mask == 0) {
            return ~0u;
        }
        return util::cnt_tz(free_mask);
    }

    [[nodiscard]] u8
        find_first_nonfixed_excluding(const u8  bank,
                                      const u64 exclusion_mask) const noexcept {
        assert(bank <= 1);
        const u64 nonfixed_mask =
            ((0xFFFF'FFFF << bank) & ((free | used) & ~fixed))
            & ~exclusion_mask;
        if (nonfixed_mask == 0) {
            return ~0u;
        }
        return util::cnt_tz(nonfixed_mask);
    }

    [[nodiscard]] static u8 reg_bank(const u8 reg) noexcept {
        return (reg >> 5);
    }
};
} // namespace tpde
