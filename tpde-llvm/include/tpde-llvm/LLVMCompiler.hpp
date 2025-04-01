// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace llvm {
class GlobalValue;
class Module;
class Triple;
} // namespace llvm

namespace tpde_llvm {

class JITMapperImpl;
class JITMapper {
private:
  friend class JITMapperImpl;

  std::unique_ptr<JITMapperImpl> impl;

public:
  explicit JITMapper(std::unique_ptr<JITMapperImpl> impl) noexcept;
  ~JITMapper();

  JITMapper(const JITMapper &) = delete;
  JITMapper(JITMapper &&other) noexcept;

  JITMapper &operator=(const JITMapper &) = delete;
  JITMapper &operator=(JITMapper &&other) noexcept;

  void *lookup_global(llvm::GlobalValue *) noexcept;

  operator bool() const noexcept { return impl != nullptr; }
};

/// Compiler for LLVM modules
class LLVMCompiler {
protected:
  LLVMCompiler() = default;

public:
  virtual ~LLVMCompiler();

  LLVMCompiler(const LLVMCompiler &) = delete;
  LLVMCompiler &operator=(const LLVMCompiler &) = delete;

  /// Create a compiler for the specified target triple.
  /// \returns null if the triple is not supported.
  static std::unique_ptr<LLVMCompiler>
      create(const llvm::Triple &triple) noexcept;

  /// Compile the module to an object file and emit it into the buffer. The
  /// module might be modified during compilation.
  /// \returns true on success.
  virtual bool compile_to_elf(llvm::Module &mod,
                              std::vector<uint8_t> &buf) noexcept = 0;

  /// Compile the module and map it into memory, calling resolver to resolve
  /// references to external symbols. This function will also register unwind
  /// information. The module might be modified during compilation.
  /// \returns RAII container for the code that will free any allocated memory
  /// while mapping and deregister unwind information
  virtual JITMapper compile_and_map(
      llvm::Module &mod,
      std::function<void *(std::string_view)> resolver) noexcept = 0;
};

} // namespace tpde_llvm
