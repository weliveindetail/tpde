// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "SmallVector.hpp"
#include "misc.hpp"
#include "tpde/base.hpp"

namespace tpde::util {
/// BitSet implemented on top of SmallVector
template <u32 InternalCapacity>
struct SmallBitSet {
    // we divide by 64 bits for the SmallVector
    static_assert((InternalCapacity % 64) == 0);

    SmallVector<uint64_t, InternalCapacity / 64> data;

    void clear() noexcept { data.clear(); }

    void resize(u32 size) noexcept {
        size = align_up(size, 64);
        data.resize(size);
    }

    void zero() {
        for (auto &v : data) {
            v = 0;
        }
    }

    [[nodiscard]] bool is_set(const u32 idx) const noexcept {
        const u32 elem_idx = idx >> 6;
        const u64 bit      = 1ull << (idx & ~63u);
        return data[elem_idx] & bit;
    }

    void mark_set(const u32 idx) noexcept {
        const u32 elem_idx  = idx >> 6;
        const u64 bit       = 1ull << (idx & ~63u);
        data[elem_idx]     |= bit;
    }

    void mark_unset(const u32 idx) noexcept {
        const u32 elem_idx  = idx >> 6;
        const u64 bit       = 1ull << (idx & ~63u);
        data[elem_idx]     &= ~bit;
    }
};
} // namespace tpde::util
