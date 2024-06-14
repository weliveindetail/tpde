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

} // namespace tpde::util
