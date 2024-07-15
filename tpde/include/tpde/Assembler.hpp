// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "IRAdaptor.hpp"
#include "base.hpp"

#ifdef ARG
    #error ARG is used as a temporary preprocessor macro
#endif

#define ARG(x) std::declval<x>()

namespace tpde {

template <typename T, IRAdaptor Adaptor>
concept Assembler = requires(T a) {
    /// We need to be able to reference function and data objects
    typename T::SymRef;

    /// For referencing local symbols we use a Label construct
    typename T::Label;

    /// Initialize
    /// args: generate_object
    { T(ARG(bool)) };

    { a.start_func(ARG(typename T::SymRef)) };
    { a.end_func() };

    /// Create a (pending) label
    { a.label_create() } -> std::same_as<typename T::Label>;
    { a.label_is_pending(ARG(typename T::Label)) } -> std::convertible_to<bool>;
    { a.label_offset(ARG(typename T::Label)) } -> std::convertible_to<u32>;
    { a.label_place(ARG(typename T::Label)) };

    /// Predefine a function symbol
    /// args: func, local, weak
    {
        a.sym_predef_func(
            ARG(typename Adaptor::IRFuncRef), ARG(bool), ARG(bool))
    } -> std::same_as<typename T::SymRef>;
};

}

#undef ARG
