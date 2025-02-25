// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "CompilerConfig.hpp"
#include "base.hpp"
#include <concepts>

#ifdef ARG
  #error ARG is used as a temporary preprocessor macro
#endif

#define ARG(x) std::declval<x>()

namespace tpde {

template <bool B>
concept IsTrue = (B == true);

template <typename T>
concept ValueParts = requires(T a) {
  /// Provides the number of parts for a value
  { a.count() } -> std::convertible_to<u32>;

  /// Provides the size in bytes of a value part (must be a power of two)
  { a.size_bytes(ARG(u32)) } -> std::convertible_to<u32>;

  /// Provides the bank for a value part
  { a.reg_bank(ARG(u32)) } -> std::convertible_to<u8>;
};

template <typename T, typename Config>
concept Compiler = CompilerConfig<Config> && requires(T a) {
  // mostly platform things
  { T::NUM_FIXED_ASSIGNMENTS } -> SameBaseAs<u32[Config::NUM_BANKS]>;

  // (func_idx)
  { a.start_func(ARG(u32)) };

  { a.gen_func_prolog_and_args() };

  // This has to call assembler->finish_func
  // (func_idx)
  { a.finish_func(ARG(u32)) };

  { a.reset_register_file() };

  // (reg_to_spill, frame_off, size)
  { a.spill_reg(ARG(typename Config::AsmReg), ARG(u32), ARG(u32)) };

  // (dst_reg, frame_off, size)
  { a.load_from_stack(ARG(typename Config::AsmReg), ARG(u32), ARG(u32)) };

  // (dst_reg, ap_ref)
  {
    a.load_address_of_var_reference(ARG(typename Config::AsmReg),
                                    ARG(typename T::AssignmentPartRef))
  };

  // (dst_reg, src_reg, size)
  {
    a.mov(ARG(typename Config::AsmReg), ARG(typename Config::AsmReg), ARG(u32))
  };

  // (value); might allocate register
  {
    a.gval_expr_as_reg(ARG(typename T::GenericValuePart &))
  } -> std::same_as<typename Config::AsmReg>;

  {
    a.select_fixed_assignment_reg(ARG(u32), ARG(typename T::IRValueRef))
  } -> std::same_as<typename Config::AsmReg>;


  // mostly implementor stuff

  { a.cur_func_may_emit_calls() } -> std::convertible_to<bool>;

  {
    a.try_force_fixed_assignment(ARG(typename T::IRValueRef))
  } -> std::convertible_to<bool>;

  { a.val_parts(ARG(typename T::IRValueRef)) } -> ValueParts;

  /// Provides the implementation to return special ValuePartRefs, e.g. for
  /// constants/globals
  ///
  /// (value, part)
  {
    a.val_ref_special(ARG(typename T::IRValueRef), ARG(u32))
  } -> std::same_as<std::optional<typename T::ValuePartRef>>;

  /// The compiler numbers functions and this gives the derived implementation
  /// a chance to save that mapping
  ///
  /// (func_ref, idx)
  { a.define_func_idx(ARG(typename T::IRFuncRef), ARG(u32)) };

  /// When the default variable reference handling is disabled, the
  /// implementation has to provide a few helpers to tell us what to do
  requires IsTrue<Config::DEFAULT_VAR_REF_HANDLING> || requires {
    /// Called when starting to compile a func.
    /// This should initialize all variable references
    /// (even the ones from TPDE so static stack slots)
    { a.setup_var_ref_assignments() };
  };

  {
    a.compile_inst(ARG(typename T::IRInstRef), ARG(typename T::InstRange))
  } -> std::convertible_to<bool>;


  // all the base class stuff for auto-complete
  {
    a.val_ref(ARG(typename T::IRValueRef), ARG(u32))
  } -> std::same_as<typename T::ValuePartRef>;
};

} // namespace tpde
