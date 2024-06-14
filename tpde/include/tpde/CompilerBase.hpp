// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "Analyzer.hpp"
#include "CompilerConfig.hpp"
#include "IRAdaptor.hpp"

namespace tpde {

// TODO(ts): formulate concept for full compiler so that there is *some* check
// whether all the required derived methods are implemented?

/// The base class for the compiler.
/// It implements the main platform independent compilation logic and houses the
/// analyzer
template <IRAdaptor Adaptor,
          typename Derived,
          CompilerConfig Config = CompilerConfigDefault>
struct CompilerBase {
    // some forwards for the IR type defs
    using IRValueRef = typename Adaptor::IRValueRef;
    using IRBlockRef = typename Adaptor::IRBlockRef;
    using IRFuncRef  = typename Adaptor::IRFuncRef;

    Adaptor          *adaptor;
    Analyzer<Adaptor> analyzer;

    /// shortcut for casting to the Derived class so that overloading works
    Derived *derived() { return static_cast<Derived *>(this); }

    const Derived *derived() const {
        return static_cast<const Derived *>(this);
    }

    /// Compile the functions returned by Adaptor::funcs
    ///
    /// \warning If you intend to call this multiple times, you must call reset
    ///   inbetween the calls.
    ///
    /// \returns Whether the compilation was successful
    bool compile();

    /// Reset any leftover data from the previous compilation such that it will
    /// not affect the next compilation
    void reset();
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::compile() {
    return false;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::reset() {
    analyzer.reset();
    adaptor.reset();
}


} // namespace tpde
