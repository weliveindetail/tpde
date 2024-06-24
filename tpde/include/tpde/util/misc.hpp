// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <type_traits>

#include "tpde/base.hpp"

namespace tpde::util {

template <typename T>
constexpr T align_up(T val, std::type_identity_t<T> align) {
    // align must be a power of two
    assert((align & (align - 1)) == 0);
    return (val + (align - 1)) & ~(align - 1);
}

template <typename T>
T cnt_tz(T) {
    static_assert(false);
}

template <>
inline u8 cnt_tz<u8>(const u8 val) {
    return __builtin_ctz(val);
}

template <>
inline u16 cnt_tz<u16>(const u16 val) {
    return __builtin_ctz(val);
}

template <>
inline u32 cnt_tz<u32>(const u32 val) {
    return __builtin_ctz(val);
}

template <>
inline u64 cnt_tz<u64>(const u64 val) {
    return __builtin_ctzll(val);
}

struct BitSetIterator {
    u64 set;

    struct Iter {
        u64 set;

        Iter &operator++() noexcept {
            set = set & (set - 1);
            return *this;
        }

        [[nodiscard]] u64 operator*() const noexcept {
            assert(set != 0);
            return cnt_tz(set);
        }

        [[nodiscard]] bool operator!=(const Iter &rhs) const noexcept {
            return rhs.set != set;
        }
    };

    explicit BitSetIterator(const u64 set) : set(set) {}

    [[nodiscard]] Iter begin() const noexcept { return Iter{.set = set}; }

    [[nodiscard]] Iter end() const noexcept { return Iter{.set = 0}; }
};

} // namespace tpde::util
