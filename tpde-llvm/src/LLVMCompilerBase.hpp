// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/CompilerBase.hpp"

#include "LLVMAdaptor.hpp"

namespace tpde_llvm {
template <typename Derived, typename Config>
struct LLVMCompilerBase : CompilerBase<LLVMAdaptor, Derived, Config> {
    // TODO
};
} // namespace tpde_llvm
