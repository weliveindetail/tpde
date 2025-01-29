// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "TestIR.hpp"
#include "tpde/x64/CompilerX64.hpp"

namespace tpde::test {
struct TestIRCompilerX64 : x64::CompilerX64<TestIRAdaptor, TestIRCompilerX64> {
  using Base = x64::CompilerX64<TestIRAdaptor, TestIRCompilerX64>;

  using IRValueRef = typename Base::IRValueRef;
  using IRFuncRef = typename Base::IRFuncRef;
  using ValuePartRef = typename Base::ValuePartRef;
  using ValLocalIdx = typename Base::ValLocalIdx;
  using ScratchReg = typename Base::ScratchReg;
  using AsmReg = typename Base::AsmReg;
  using InstRange = typename Base::InstRange;

  bool no_fixed_assignments;

  explicit TestIRCompilerX64(TestIRAdaptor *adaptor, bool no_fixed_assignments)
      : Base{adaptor}, no_fixed_assignments(no_fixed_assignments) {}

  [[nodiscard]] static x64::CallingConv cur_calling_convention() noexcept {
    return x64::CallingConv::SYSV_CC;
  }

  static bool arg_is_int128(IRValueRef) noexcept { return false; }
  static bool arg_allow_split_reg_stack_passing(IRValueRef) noexcept {
    return false;
  }

  bool cur_func_may_emit_calls() const noexcept {
    return this->ir()->functions[this->adaptor->cur_func].has_call;
  }

  u32 val_part_count(IRValueRef) const noexcept { return 1; }

  u32 val_part_size(IRValueRef, u32) const noexcept { return 8; }

  u8 val_part_bank(IRValueRef, u32) const noexcept { return 0; }

  AsmReg select_fixed_assignment_reg(const u32 bank,
                                     const IRValueRef value) noexcept {
    if (no_fixed_assignments && !try_force_fixed_assignment(value)) {
      return AsmReg::make_invalid();
    }

    return Base::select_fixed_assignment_reg(bank, value);
  }

  bool try_force_fixed_assignment(const IRValueRef value) const noexcept {
    return ir()->values[static_cast<u32>(value)].force_fixed_assignment;
  }

  std::optional<ValuePartRef> val_ref_special(ValLocalIdx local_idx,
                                              u32 part) noexcept {
    (void)local_idx;
    (void)part;
    return {};
  }

  void define_func_idx(IRFuncRef func, const u32 idx) noexcept {
    assert(static_cast<u32>(func) == idx);
    (void)func;
    (void)idx;
  }

  [[nodiscard]] bool compile_inst(IRValueRef, InstRange) noexcept;

  TestIR *ir() noexcept { return this->adaptor->ir; }

  const TestIR *ir() const noexcept { return this->adaptor->ir; }

  bool compile_add(IRValueRef) noexcept;
  bool compile_sub(IRValueRef) noexcept;
};
} // namespace tpde::test
