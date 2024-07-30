// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <llvm/IR/LLVMContext.h>

namespace tpde_llvm::x64 {
extern bool compile_llvm(llvm::LLVMContext &ctx,
                         llvm::Module      &mod,
                         const char        *out_path);
}
