// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde-llvm/LLVMCompiler.hpp"

#include <llvm/TargetParser/Triple.h>
#include <memory>

#include "arm64/LLVMCompilerArm64.hpp"
#include "x64/LLVMCompilerX64.hpp"

namespace tpde_llvm {

LLVMCompiler::~LLVMCompiler() = default;

std::unique_ptr<LLVMCompiler>
    LLVMCompiler::create(const llvm::Triple &triple) noexcept {
  if (!triple.isOSBinFormatELF()) {
    return nullptr;
  }

  switch (triple.getArch()) {
  case llvm::Triple::x86_64: return x64::create_compiler();
  case llvm::Triple::aarch64: return arm64::create_compiler();
  default: return nullptr;
  }
}

} // namespace tpde_llvm
