// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <gch/small_vector.hpp>

namespace tpde::util {

template <typename T,
          unsigned InlineCapacity =
              gch::default_buffer_size_v<std::allocator<T>>,
          typename Allocator = std::allocator<T>>
using SmallVector = gch::small_vector<T, InlineCapacity, Allocator>;

}
