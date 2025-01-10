// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <memory>

#include "tpde-llvm/LLVMCompiler.hpp"

namespace tpde_llvm::arm64 {

std::unique_ptr<LLVMCompiler> create_compiler() noexcept;

} // namespace tpde_llvm::arm64
