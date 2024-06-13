// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde/IRAdaptor.hpp"

template <bool B>
concept IsTrue = (B == true);

template <typename T>
concept TC = requires(T a) {
    { T::someBool } -> std::convertible_to<bool>;

    requires IsTrue<T::someBool> || requires {
        { a.someFunc() };
    };
};

struct Test {
    using IRValueRef = uint64_t;

    static constexpr bool someBool = true;

    // void someFunc() {}
};

static_assert(TC<Test>);
