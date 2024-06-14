// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "base.hpp"
#include <concepts>
#include <string_view>

#ifdef ARG
    #error ARG is used as a temporary preprocessor macro
#endif

#define ARG(x) std::declval<x>()

namespace tpde {

template <bool B>
concept IsFalse = (B == false);

/// Concept describing an iterator over some range
///
/// It is purposefully kept simple (and probably wrong)
template <typename T, typename Value>
concept IRIter = requires(T i, T i2) {
    { *i } -> std::convertible_to<Value>;
    { ++i } -> std::convertible_to<T>;
    { i != i2 } -> std::convertible_to<bool>;
};

/// Concept describing a very simple range
///
/// It is purposefully kept simple since the full concepts from std::ranges want
/// too much code
template <typename T, typename Value>
concept IRRange = requires(T r) {
    { r.begin() } -> IRIter<Value>;
    { r.end() } -> IRIter<Value>;
};

/// PHI-Nodes are a special case of IRValues and need to be inspected more
/// thoroughly by the compiler. Therefore, they need to expose their special
/// properties, namely the number of incoming values, the value and block for
/// each slot and the incoming value for each block
template <typename T, typename IRValue, typename IRBlockRef>
concept PHIRef = requires(T r) {
    /// Provides the number of incoming values
    { r.incoming_count() } -> std::convertible_to<u32>;

    /// Provides the incoming value for a slot
    {
        r.incoming_val_for_slot(std::declval<u32>())
    } -> std::convertible_to<IRValue>;

    /// Provides the incoming block for a slot
    {
        r.incoming_block_for_slot(std::declval<u32>())
    } -> std::convertible_to<IRBlockRef>;

    /// Provides the incoming value for a specific block
    {
        r.incoming_val_for_block(std::declval<IRBlockRef>())
    } -> std::convertible_to<IRValue>;
};

/// The IRAdaptor specifies the interface with which the IR-independent parts of
/// the compiler interact with the source IR
///
/// It provides type definitions for values and blocks, ways to iterate over
/// blocks, values and their arguments.
/// Additionally, it exposes information about functions in the IR.
///
/// Since we also expect that most IRAdaptors will allocate some space or have
/// some available in the storage of values/blocks, we ask the adaptor to also
/// store some information about values/blocks for setting/retrieval by the
/// compiler.
template <typename T>
concept IRAdaptor = requires(T a) {
    /// A reference to a value in your IR. Should not be greater than 8 bytes
    typename T::IRValueRef;
    requires sizeof(typename T::IRValueRef) <= 8;

    /// A reference to a block in your IR. Should not be greater than 8 bytes
    typename T::IRBlockRef;
    requires sizeof(typename T::IRBlockRef) <= 8;

    /// A reference to a function in your IR. Should not be greater than 8 bytes
    typename T::IRFuncRef;
    requires sizeof(typename T::IRFuncRef) <= 8;

    /// An invalid value reference
    { T::INVALID_VALUE_REF } -> std::same_as<typename T::IRValueRef>;
    requires requires(typename T::IRValueRef r) {
        { r == T::INVALID_VALUE_REF } -> std::convertible_to<bool>;
        { r != T::INVALID_VALUE_REF } -> std::convertible_to<bool>;
    };


    /// An invalid block reference
    { T::INVALID_BLOCK_REF } -> std::same_as<typename T::IRBlockRef>;
    requires requires(typename T::IRBlockRef r) {
        { r == T::INVALID_BLOCK_REF } -> std::convertible_to<bool>;
        { r != T::INVALID_BLOCK_REF } -> std::convertible_to<bool>;
    };

    /// An invalid function reference
    { T::INVALID_FUNC_REF } -> std::same_as<typename T::IRFuncRef>;
    requires requires(typename T::IRFuncRef r) {
        { r == T::INVALID_FUNC_REF } -> std::convertible_to<bool>;
        { r != T::INVALID_FUNC_REF } -> std::convertible_to<bool>;
    };

    /// Can the adaptor provide information about the highest value index in the
    /// function to be compiled ahead of time?
    { T::TPDE_PROVIDES_HIGHEST_VAL_IDX } -> std::same_as<bool>;

    // Can the adaptor store two 32 bit values for efficient access through the
    // block reference?
    // { T::TPDE_CAN_STORE_BLOCK_AUX } -> std::same_as<bool>;
    // TODO(ts): think about if this is optional


    // general module information

    /// Provides the number of functions to compile
    { a.func_count() } -> std::convertible_to<u32>;

    /// Provides an iterator over all functions to be compiled
    { a.funcs() } -> IRRange<typename T::IRFuncRef>;


    // information about functions that needs to be always available because of
    // calls

    /// Provides the linkage name of the specified function
    {
        a.func_link_name(ARG(typename T::IRFuncRef))
    } -> std::convertible_to<std::string_view>;

    /// Is the specified function not going to be compiled?
    { a.func_extern(ARG(typename T::IRFuncRef)) } -> std::convertible_to<bool>;

    /// Is the specified function only visible to the current module
    { a.func_local(ARG(typename T::IRFuncRef)) } -> std::convertible_to<bool>;

    /// Does the specified function have weak linkage
    {
        a.func_has_weak_linkage(ARG(typename T::IRFuncRef))
    } -> std::convertible_to<bool>;


    // information about the current function

    /// Does the current function need unwind info
    { a.cur_needs_unwind_info() } -> std::convertible_to<bool>;

    /// Does the current function take a variable number of arguments
    { a.cur_is_vararg() } -> std::convertible_to<bool>;

    /// Provides the highest value index in the current function.
    /// Only needs to be implemented if TPDE_FUNC_VAL_COUNT_ESTIMATE is true
    requires IsFalse<T::TPDE_FUNC_VAL_COUNT_ESTIMATE> || requires {
        { a.cur_highest_val_idx() } -> std::convertible_to<u32>;
    };

    /// Provides the personality function for the current function or
    /// INVALID_FUNC_REF otherwise
    { a.cur_personality_func() } -> std::same_as<typename T::IRFuncRef>;

    /// Provides an iterator over the arguments of the current function
    { a.cur_args() } -> IRRange<typename T::IRValueRef>;

    // TODO(ts): func byval, sret

    /// Provides an iterator of static allocas
    { a.cur_static_allocas() } -> IRRange<typename T::IRValueRef>;

    /// Provides a reference to the entry block of the current function
    { a.cur_entry_block() } -> std::convertible_to<typename T::IRBlockRef>;


    // information about blocks

    /// Provides the sibling of a block.
    /// Blocks need to be in an arbitrary but consistent order
    {
        a.block_sibling(ARG(typename T::IRBlockRef))
    } -> std::convertible_to<typename T::IRBlockRef>;

    /// Provides an iterator over the successors of a block
    {
        a.block_succs(ARG(typename T::IRBlockRef))
    } -> IRRange<typename T::IRBlockRef>;

    /// Provides an iterator over the values in a block
    {
        a.block_values(ARG(typename T::IRBlockRef))
    } -> IRRange<typename T::IRValueRef>;

    /// Provides an iterator over the PHIs in a block
    {
        a.block_phis(ARG(typename T::IRBlockRef))
    } -> IRRange<typename T::IRValueRef>;


    // information about values

    /// Provides the local index for a value
    ///
    /// The compiler maintains a few per-value data structures which are
    /// implemented as arrays for efficiency reasons. To facilitate access to
    /// them we ask the IRAdaptor to assign each value (including globals and
    /// possibly functions if they are exposed to the compiler) a local index in
    /// the context of the current function
    {
        a.val_local_idx(ARG(typename T::IRValueRef))
    } -> std::convertible_to<u32>;

    // TODO(ts): add separate function to get local idx which is only used in
    // the analyzer if the adaptor wants to lazily assign indices?

    /// Provides an iterator over the operands of a value
    {
        a.val_operands(ARG(typename T::IRValueRef))
    } -> IRRange<typename T::IRValueRef>;

    /// Should a value be ignored in the liveness analysis?
    /// This is recommended for values classified as variable references
    /// such as globals or allocas
    {
        a.val_ignore_in_liveness_analysis(ARG(typename T::IRValueRef))
    } -> std::convertible_to<bool>;

    /// Is the specified value an argument of the current function
    { a.val_is_arg(ARG(typename T::IRValueRef)) } -> std::convertible_to<bool>;

    /// Is the specified value a PHI?
    // TODO(ts): drop this and try to exclusively access PHIs through the
    // IRRange provided by the block?
    { a.val_is_phi(ARG(typename T::IRValueRef)) } -> std::convertible_to<bool>;

    /// Provides the PHIRef for a PHI
    {
        a.val_as_phi(ARG(typename T::IRValueRef))
    } -> PHIRef<typename T::IRValueRef, typename T::IRBlockRef>;

    /// Provides the allocation size for an alloca
    {
        a.val_alloca_size(ARG(typename T::IRValueRef))
    } -> std::convertible_to<u32>;


    // compilation lifecycle

    /// The compilation was started
    { a.start_compile() };

    /// The compilation was finished
    { a.end_compile() };

    /// The compiler is now compiling the specified function
    { a.switch_func(ARG(typename T::IRFuncRef)) };

    /// The compiler is being resetted. If there is any data remaining that
    /// would cause problems with recompiling it should be cleared
    { a.reset() };
};

#undef ARG

} // namespace tpde
