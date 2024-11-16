// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <llvm/IR/LLVMContext.h>

namespace tpde_llvm::arm64 {

extern bool compile_llvm(llvm::Module &mod, std::vector<uint8_t> &out_buf);

} // namespace tpde_llvm::arm64
