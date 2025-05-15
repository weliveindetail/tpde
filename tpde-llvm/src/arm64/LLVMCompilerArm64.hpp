// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <memory>

#include "tpde-llvm/LLVMCompiler.hpp"

namespace llvm {
class Triple;
} // namespace llvm

namespace tpde_llvm::arm64 {

std::unique_ptr<LLVMCompiler> create_compiler(const llvm::Triple &) noexcept;

} // namespace tpde_llvm::arm64
