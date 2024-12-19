// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "TestIRCompiler.hpp"

namespace tpde::test {
using namespace tpde::x64;

bool TestIRCompilerX64::compile_inst(IRValueRef val_idx, InstRange) noexcept {
  const TestIR::Value &value =
      this->analyzer.adaptor->ir->values[static_cast<u32>(val_idx)];

  switch (value.type) {
    using enum TestIR::Value::Type;
  case normal: {
    switch (value.op) {
      using enum TestIR::Value::Op;
    case add: return compile_add(val_idx);
    case sub: return compile_sub(val_idx);
    default: assert(0); return false;
    }
  }
  case phi: {
    // ref-count
    this->val_ref(val_idx, 0);
    return true;
  }
  case ret: {
    if (value.op_count == 1) {
      const auto op = static_cast<IRValueRef>(
          this->adaptor->ir->value_operands[value.op_begin_idx]);

      ValuePartRef val_ref = this->val_ref(op, 0);
      const auto call_conv = this->cur_calling_convention();
      if (val_ref.assignment().fixed_assignment()) {
        val_ref.reload_into_specific(this, call_conv.ret_regs_gp()[0]);
      } else {
        val_ref.move_into_specific(call_conv.ret_regs_gp()[0]);
      }
    } else {
      assert(value.op_count == 0);
    }

    this->gen_func_epilog();
    this->release_regs_after_return();
    return true;
  }
  case alloca: return true;
  case br: {
    auto block_idx = ir()->value_operands[value.op_begin_idx];
    auto spilled = this->spill_before_branch();

    this->generate_branch_to_block(
        Jump::jmp, static_cast<IRBlockRef>(block_idx), false, true);

    this->release_spilled_regs(spilled);
    return true;
  }
  case condbr: {
    auto val_idx =
        static_cast<IRValueRef>(ir()->value_operands[value.op_begin_idx]);
    auto true_block =
        static_cast<IRBlockRef>(ir()->value_operands[value.op_begin_idx + 1]);
    auto false_block =
        static_cast<IRBlockRef>(ir()->value_operands[value.op_begin_idx + 2]);

    auto val = this->val_ref(val_idx, 0);

    auto true_needs_split = this->branch_needs_split(true_block);
    auto false_needs_split = this->branch_needs_split(false_block);

    ScratchReg scratch{this};
    auto val_reg = this->val_as_reg(val, scratch);

    auto spilled = this->spill_before_branch();

    ASM(CMP64ri, val_reg, 0);
    if (this->analyzer.block_ref(this->next_block()) == true_block) {
      this->generate_branch_to_block(
          Jump::je, false_block, false_needs_split, false);
      this->generate_branch_to_block(Jump::jmp, true_block, false, true);
    } else if (this->analyzer.block_ref(this->next_block()) == false_block) {
      this->generate_branch_to_block(
          Jump::jne, true_block, true_needs_split, false);
      this->generate_branch_to_block(Jump::jmp, false_block, false, true);
    } else if (!true_needs_split) {
      this->generate_branch_to_block(Jump::jne, true_block, false, false);
      this->generate_branch_to_block(Jump::jmp, false_block, false, true);
    } else {
      this->generate_branch_to_block(
          Jump::je, false_block, false_needs_split, false);
      this->generate_branch_to_block(Jump::jmp, true_block, false, true);
    }

    this->release_spilled_regs(spilled);
    return true;
  }
  case call: {
    const auto func_idx = value.call_func_idx;
    auto operands = std::span<IRValueRef>{
        reinterpret_cast<IRValueRef *>(ir()->value_operands.data() +
                                       value.op_begin_idx),
        value.op_count};

    std::variant<ValuePartRef, std::pair<ScratchReg, u8>> res =
        this->result_ref_lazy(val_idx, 0);

    util::SmallVector<CallArg, 8> arguments{};
    for (auto op : operands) {
      arguments.push_back(CallArg{op});
    }

    this->generate_call(this->func_syms[func_idx],
                        arguments,
                        std::span{&res, 1},
                        CallingConv::SYSV_CC,
                        false);
    return true;
  }
  default: assert(0); __builtin_unreachable();
  }

  return false;
}

bool TestIRCompilerX64::compile_add(IRValueRef val_idx) noexcept {
  const TestIR::Value &value = ir()->values[static_cast<u32>(val_idx)];

  const auto lhs_idx =
      static_cast<IRValueRef>(ir()->value_operands[value.op_begin_idx]);
  const auto rhs_idx =
      static_cast<IRValueRef>(ir()->value_operands[value.op_begin_idx + 1]);

  ValuePartRef lhs = Base::val_ref(lhs_idx, 0);
  ValuePartRef rhs = Base::val_ref(rhs_idx, 0);

  AsmReg lhs_orig{};
  ValuePartRef result = Base::result_ref_salvage_with_original(
      val_idx, 0, std::move(lhs), lhs_orig);
  auto res_reg = result.cur_reg();

  ScratchReg scratch{this};
  auto rhs_reg = Base::val_as_reg(rhs, scratch);

  if (res_reg.id() == lhs_orig.id()) {
    ASM(ADD64rr, res_reg, rhs_reg);
  } else {
    ASM(LEA64rm, res_reg, FE_MEM(lhs_orig, 1, rhs_reg, 0));
  }

  Base::set_value(result, res_reg);
  return true;
}

bool TestIRCompilerX64::compile_sub(IRValueRef val_idx) noexcept {
  const TestIR::Value &value = ir()->values[static_cast<u32>(val_idx)];

  const auto lhs_idx =
      static_cast<IRValueRef>(ir()->value_operands[value.op_begin_idx]);
  const auto rhs_idx =
      static_cast<IRValueRef>(ir()->value_operands[value.op_begin_idx + 1]);

  ValuePartRef lhs = Base::val_ref(lhs_idx, 0);
  ValuePartRef rhs = Base::val_ref(rhs_idx, 0);

  ValuePartRef result =
      Base::result_ref_must_salvage(val_idx, 0, std::move(lhs));
  auto res_reg = result.cur_reg();

  ScratchReg scratch{this};
  auto rhs_reg = Base::val_as_reg(rhs, scratch);

  ASM(SUB64rr, res_reg, rhs_reg);

  Base::set_value(result, res_reg);
  return true;
}
} // namespace tpde::test
