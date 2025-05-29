// SPDX-License-Identifier: LicenseRef-Proprietary

#include "JITMapper.hpp"

#include "tpde-llvm/LLVMCompiler.hpp"
#include "tpde/AssemblerElf.hpp"
#include "tpde/ElfMapper.hpp"

#include <llvm/Support/TimeProfiler.h>

namespace tpde_llvm {

bool JITMapperImpl::map(tpde::AssemblerElfBase &assembler,
                        tpde::ElfMapper::SymbolResolver resolver) noexcept {
  llvm::TimeTraceScope time_scope("TPDE_JITMap");
  return mapper.map(assembler, resolver);
}

JITMapper::JITMapper(std::unique_ptr<JITMapperImpl> impl) noexcept
    : impl(std::move(impl)) {}

JITMapper::~JITMapper() = default;

JITMapper::JITMapper(JITMapper &&other) noexcept = default;
JITMapper &JITMapper::operator=(JITMapper &&other) noexcept = default;

void *JITMapper::lookup_global(llvm::GlobalValue *gv) noexcept {
  return impl ? impl->lookup_global(gv) : nullptr;
}

} // namespace tpde_llvm
