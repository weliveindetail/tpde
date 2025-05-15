// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <memory>

#include "tpde-llvm/LLVMCompiler.hpp"

namespace llvm {
class Triple;
} // namespace llvm

namespace tpde_llvm::x64 {

std::unique_ptr<LLVMCompiler> create_compiler(const llvm::Triple &) noexcept;

} // namespace tpde_llvm::x64
