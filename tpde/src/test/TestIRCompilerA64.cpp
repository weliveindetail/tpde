// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <fstream>

#include "tpde/arm64/CompilerA64.hpp"

#include "TestIR.hpp"
#include "TestIRCompilerA64.hpp"

namespace {
using namespace tpde;
using namespace tpde::test;

struct TestIRCompilerA64 : a64::CompilerA64<TestIRAdaptor, TestIRCompilerA64> {
  using Base = a64::CompilerA64<TestIRAdaptor, TestIRCompilerA64>;

  using IRValueRef = typename Base::IRValueRef;
  using IRFuncRef = typename Base::IRFuncRef;
  using ValuePartRef = typename Base::ValuePartRef;
  using ValLocalIdx = typename Base::ValLocalIdx;
  using ScratchReg = typename Base::ScratchReg;
  using AsmReg = typename Base::AsmReg;
  using InstRange = typename Base::InstRange;

  bool no_fixed_assignments;

  explicit TestIRCompilerA64(TestIRAdaptor *adaptor, bool no_fixed_assignments)
      : Base{adaptor}, no_fixed_assignments(no_fixed_assignments) {}

  [[nodiscard]] static a64::CallingConv cur_calling_convention() noexcept {
    return a64::CallingConv::SYSV_CC;
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

bool TestIRCompilerA64::compile_inst(IRValueRef val_idx, InstRange) noexcept {
  const TestIR::Value &value =
      this->analyzer.adaptor->ir->values[static_cast<u32>(val_idx)];
  if (value.type == TestIR::Value::Type::phi) {
    // ref-count
    this->val_ref(val_idx, 0);
    return true;
  }

  switch (value.op) {
    using enum TestIR::Value::Op;
  case add: return compile_add(val_idx);
  case sub: return compile_sub(val_idx);
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
    [[fallthrough]];
  }
  case terminate:
    this->gen_func_epilog();
    this->release_regs_after_return();
    return true;
  case alloca: return true;
  case br: {
    auto block_idx = ir()->value_operands[value.op_begin_idx];
    auto spilled = this->spill_before_branch();

    this->generate_branch_to_block(
        Jump::jmp, static_cast<IRBlockRef>(block_idx), false, true);

    this->release_spilled_regs(spilled);
    return true;
  }
  case zerofill: {
    auto size = ir()->value_operands[value.op_begin_idx];
    this->assembler.text_ensure_space(size);
    ASM(B, size / 4);
    this->assembler.text_write_ptr += (size - 4) & -4u;
    return true;
  }
  case condbr:
  case tbz: {
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

    Jump jump;
    if (value.op == condbr) {
      ASM(CMPxi, val_reg, 0);
      jump = Jump::Jne;
    } else {
      u32 bit = ir()->value_operands[value.op_begin_idx + 3];
      jump = Jump(Jump::Tbz, val_reg, u8(bit));
    }

    Jump inv_jump = invert_jump(jump);

    if (this->analyzer.block_ref(this->next_block()) == true_block) {
      this->generate_branch_to_block(
          inv_jump, false_block, false_needs_split, false);
      this->generate_branch_to_block(Jump::jmp, true_block, false, true);
    } else if (this->analyzer.block_ref(this->next_block()) == false_block) {
      this->generate_branch_to_block(jump, true_block, true_needs_split, false);
      this->generate_branch_to_block(Jump::jmp, false_block, false, true);
    } else if (!true_needs_split) {
      this->generate_branch_to_block(jump, true_block, false, false);
      this->generate_branch_to_block(Jump::jmp, false_block, false, true);
    } else {
      this->generate_branch_to_block(
          inv_jump, false_block, false_needs_split, false);
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
                        a64::CallingConv::SYSV_CC,
                        false);
    return true;
  }
  default: return false;
  }

  return false;
}

bool TestIRCompilerA64::compile_add(IRValueRef val_idx) noexcept {
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

  ASM(ADDx, res_reg, lhs_orig, rhs_reg);

  Base::set_value(result, res_reg);
  return true;
}

bool TestIRCompilerA64::compile_sub(IRValueRef val_idx) noexcept {
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

  ASM(SUBx, res_reg, lhs_orig, rhs_reg);

  Base::set_value(result, res_reg);
  return true;
}
} // namespace

bool test::compile_ir_arm64(TestIR *ir,
                            bool no_fixed_assignments,
                            const std::string &obj_out_path) {
  test::TestIRAdaptor adaptor{ir};
  TestIRCompilerA64 compiler{&adaptor, no_fixed_assignments};

  if (!compiler.compile()) {
    TPDE_LOG_ERR("Failed to compile IR");
    return false;
  }

  if (!obj_out_path.empty()) {
    const std::vector<u8> data = compiler.assembler.build_object_file();
    std::ofstream out_file{obj_out_path, std::ios::binary};
    if (!out_file.is_open()) {
      TPDE_LOG_ERR("Failed to open output file");
      return false;
    }
    out_file.write(reinterpret_cast<const char *>(data.data()), data.size());
  }

  return true;
}
