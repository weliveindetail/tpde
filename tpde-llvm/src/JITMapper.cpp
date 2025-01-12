// SPDX-License-Identifier: LicenseRef-Proprietary

#include "JITMapper.hpp"

#include "tpde-llvm/LLVMCompiler.hpp"
#include "tpde/AssemblerElf.hpp"
#include "tpde/ElfMapper.hpp"

#include <llvm/Support/TimeProfiler.h>

namespace tpde_llvm {

bool JITMapperImpl::map(tpde::AssemblerElfBase &assembler,
                        tpde::ElfMapper::SymbolResolver resolver) noexcept {
  llvm::TimeTraceProfilerEntry *time_entry = nullptr;
  if (llvm::timeTraceProfilerEnabled()) {
    time_entry = llvm::timeTraceProfilerBegin("TPDE_JITMap", "");
  }
  bool success = mapper.map(assembler, resolver);
  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
  }
  return success;
}

JITMapper::JITMapper(std::unique_ptr<JITMapperImpl> impl) noexcept
    : impl(std::move(impl)) {}

JITMapper::~JITMapper() = default;

void *JITMapper::lookup_global(llvm::GlobalValue *gv) noexcept {
  return impl ? impl->lookup_global(gv) : nullptr;
}

} // namespace tpde_llvm
