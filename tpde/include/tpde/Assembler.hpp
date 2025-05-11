// SPDX-FileCopyrightText: 2025 Contributors to TPDE <https://tpde.org>
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
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

  /// Create a (pending) label
  { a.label_create() } -> std::same_as<typename T::Label>;
  { a.label_is_pending(ARG(typename T::Label)) } -> std::convertible_to<bool>;
  { a.label_offset(ARG(typename T::Label)) } -> std::convertible_to<u32>;
  { a.label_place(ARG(typename T::Label)) };

  /// Predefine a function symbol
  /// args: func_link_name, local, weak
  {
    a.sym_predef_func(ARG(std::string_view), ARG(bool), ARG(bool))
  } -> std::same_as<typename T::SymRef>;

  /// Add an undefined symbol
  /// args: name, local, weak
  { a.sym_add_undef(ARG(std::string_view), ARG(bool), ARG(bool)) };
};

} // namespace tpde

#undef ARG
