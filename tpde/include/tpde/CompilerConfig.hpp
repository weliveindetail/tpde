// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "base.hpp"

namespace tpde {

template <typename T, typename Base>
concept SameBaseAs =
    std::is_same_v<std::remove_const_t<std::remove_reference_t<T>>, Base>;

template <typename T>
concept CompilerConfig = requires {
    { T::FRAME_INDEXING_NEGATIVE } -> SameBaseAs<bool>;
    { T::PLATFORM_POINTER_SIZE } -> SameBaseAs<u32>;
    { T::NUM_BANKS } -> SameBaseAs<u32>;

    typename T::Assembler;
    typename T::AsmReg;
};

struct CompilerConfigDefault {};

} // namespace tpde
