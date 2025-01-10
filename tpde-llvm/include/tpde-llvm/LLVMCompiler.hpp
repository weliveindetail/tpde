// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
class Module;
class Triple;
} // namespace llvm

namespace tpde_llvm {

class LLVMCompiler {
protected:
  LLVMCompiler() = default;

public:
  virtual ~LLVMCompiler();

  LLVMCompiler(const LLVMCompiler &) = delete;
  LLVMCompiler &operator=(const LLVMCompiler &) = delete;

  /// Create a compiler for the specified target triple. Returns null if the
  /// triple is not supported.
  static std::unique_ptr<LLVMCompiler>
      create(const llvm::Triple &triple) noexcept;

  /// Compile the module to an object file and emit it into the buffer. The
  /// module might be modified during compilation. Returns true on success.
  virtual bool compile_to_elf(llvm::Module &mod,
                              std::vector<uint8_t> &buf) noexcept = 0;
};

} // namespace tpde_llvm
