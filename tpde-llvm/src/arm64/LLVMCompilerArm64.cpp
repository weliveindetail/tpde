// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <fstream>

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicsAArch64.h>

#include "LLVMAdaptor.hpp"
#include "LLVMCompilerBase.hpp"
#include "encode_template_arm64.hpp"
#include "tpde/arm64/CompilerA64.hpp"
#include "tpde/base.hpp"

namespace tpde_llvm::arm64 {

struct CompilerConfig : tpde::a64::PlatformConfig {
  static constexpr bool DEFAULT_VAR_REF_HANDLING = false;
};

struct LLVMCompilerArm64 : tpde::a64::CompilerA64<LLVMAdaptor,
                                                  LLVMCompilerArm64,
                                                  LLVMCompilerBase,
                                                  CompilerConfig>,
                           tpde_encodegen::EncodeCompiler<LLVMAdaptor,
                                                          LLVMCompilerArm64,
                                                          LLVMCompilerBase,
                                                          CompilerConfig> {
  using Base = tpde::a64::CompilerA64<LLVMAdaptor,
                                      LLVMCompilerArm64,
                                      LLVMCompilerBase,
                                      CompilerConfig>;
  using EncCompiler = EncodeCompiler<LLVMAdaptor,
                                     LLVMCompilerArm64,
                                     LLVMCompilerBase,
                                     CompilerConfig>;

  struct EncodeImm : GenericValuePart::Immediate {
    explicit EncodeImm(const u32 value)
        : Immediate{.const_u64 = value, .bank = 0, .size = 4} {}

    explicit EncodeImm(const u64 value)
        : Immediate{.const_u64 = value, .bank = 0, .size = 8} {}

    explicit EncodeImm(const float value)
        : Immediate{
              .const_u64 = std::bit_cast<u32>(value), .bank = 1, .size = 4} {}

    explicit EncodeImm(const double value)
        : Immediate{
              .const_u64 = std::bit_cast<u64>(value), .bank = 1, .size = 8} {}
  };

  std::unique_ptr<LLVMAdaptor> adaptor;

  static constexpr std::array<AsmReg, 2> LANDING_PAD_RES_REGS = {AsmReg::R0,
                                                                 AsmReg::R1};

  explicit LLVMCompilerArm64(std::unique_ptr<LLVMAdaptor> &&adaptor)
      : Base{adaptor.get()}, adaptor(std::move(adaptor)) {
    static_assert(tpde::Compiler<LLVMCompilerArm64, tpde::a64::PlatformConfig>);
  }

  void reset() noexcept {
    // TODO: move to LLVMCompilerBase
    Base::reset();
    EncCompiler::reset();
  }

  [[nodiscard]] static tpde::a64::CallingConv
      cur_calling_convention() noexcept {
    return tpde::a64::CallingConv::SYSV_CC;
  }

  bool arg_is_int128(const IRValueRef val_idx) const noexcept {
    return this->adaptor->values[val_idx].type == LLVMBasicValType::i128;
  }

  bool arg_allow_split_reg_stack_passing(
      const IRValueRef val_idx) const noexcept {
    // we allow splitting the value if it is an aggregate but not if it is an
    // i128 or array
    if (arg_is_int128(val_idx)) {
      return false;
    }
    if (this->adaptor->values[val_idx].type == LLVMBasicValType::complex) {
      if (this->adaptor->values[val_idx].val->getType()->isArrayTy()) {
        return false;
      }
    }
    return true;
  }

  void finish_func(u32 func_idx) noexcept;

  u32 val_part_count(IRValueRef) const noexcept;

  u32 val_part_size(IRValueRef, u32) const noexcept;

  u8 val_part_bank(IRValueRef, u32) const noexcept;

  void move_val_to_ret_regs(llvm::Value *) noexcept;

  void load_address_of_var_reference(AsmReg dst, AssignmentPartRef ap) noexcept;

  void ext_int(
      AsmReg dst, AsmReg src, bool sign, unsigned from, unsigned to) noexcept;
  ScratchReg ext_int(GenericValuePart op,
                     bool sign,
                     unsigned from,
                     unsigned to) noexcept;

  void extract_element(IRValueRef vec,
                       unsigned idx,
                       LLVMBasicValType ty,
                       ScratchReg &out) noexcept;
  void insert_element(IRValueRef vec,
                      unsigned idx,
                      LLVMBasicValType ty,
                      GenericValuePart el) noexcept;

  void create_frem_calls(IRValueRef lhs,
                         IRValueRef rhs,
                         ValuePartRef &&res,
                         bool is_double) noexcept;

  bool compile_unreachable(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_alloca(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_br(IRValueRef, llvm::Instruction *) noexcept;
  void generate_conditional_branch(Jump jmp,
                                   IRBlockRef true_target,
                                   IRBlockRef false_target) noexcept;
  bool compile_inline_asm(IRValueRef, llvm::CallBase *) noexcept;
  bool compile_call_inner(IRValueRef,
                          llvm::CallBase *,
                          std::variant<SymRef, ValuePartRef> &,
                          bool) noexcept;
  bool compile_icmp(IRValueRef, llvm::Instruction *, InstRange) noexcept;
  void compile_i32_cmp_zero(AsmReg reg, llvm::CmpInst::Predicate p) noexcept;

  GenericValuePart resolved_gep_to_addr(ResolvedGEP &resolved) noexcept;
  GenericValuePart create_addr_for_alloca(u32 ref_idx) noexcept;

  void switch_emit_cmp(ScratchReg &scratch,
                       AsmReg cmp_reg,
                       u64 case_value,
                       bool width_is_32) noexcept;
  void switch_emit_cmpeq(Label case_label,
                         AsmReg cmp_reg,
                         u64 case_value,
                         bool width_is_32) noexcept;
  bool switch_emit_jump_table(Label default_label,
                              std::span<Label> labels,
                              AsmReg cmp_reg,
                              u64 low_bound,
                              u64 high_bound,
                              bool width_is_32) noexcept;
  void switch_emit_binary_step(Label case_label,
                               Label gt_label,
                               AsmReg cmp_reg,
                               u64 case_value,
                               bool width_is_32) noexcept;

  void create_helper_call(std::span<IRValueRef> args,
                          std::span<ValuePartRef> results,
                          SymRef sym) noexcept;

  bool
      handle_intrin(IRValueRef, llvm::Instruction *, llvm::Function *) noexcept;

  bool handle_overflow_intrin_128(OverflowOp op,
                                  GenericValuePart lhs_lo,
                                  GenericValuePart lhs_hi,
                                  GenericValuePart rhs_lo,
                                  GenericValuePart rhs_hi,
                                  ScratchReg &res_lo,
                                  ScratchReg &res_hi,
                                  ScratchReg &res_of) noexcept;
};

void LLVMCompilerArm64::finish_func(u32 func_idx) noexcept {
  Base::finish_func(func_idx);

  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
    time_entry = nullptr;
  }
}

u32 LLVMCompilerArm64::val_part_count(const IRValueRef val_idx) const noexcept {
  return this->adaptor->val_part_count(val_idx);
}

u32 LLVMCompilerArm64::val_part_size(const IRValueRef val_idx,
                                     const u32 part_idx) const noexcept {
  return this->adaptor->val_part_size(val_idx, part_idx);
}

u8 LLVMCompilerArm64::val_part_bank(const IRValueRef val_idx,
                                    const u32 part_idx) const noexcept {
  auto ty = this->adaptor->val_part_ty(val_idx, part_idx);
  return this->adaptor->basic_ty_part_bank(ty);
}

void LLVMCompilerArm64::move_val_to_ret_regs(llvm::Value *val) noexcept {
  unsigned zext_width = 0;
  unsigned sext_width = 0;
  auto *fn = this->adaptor->cur_func;
  if (auto *ret_ty = fn->getReturnType(); ret_ty->isIntegerTy()) {
    if (auto bit_width = ret_ty->getIntegerBitWidth(); bit_width < 64) {
      if (fn->hasRetAttribute(llvm::Attribute::ZExt)) {
        zext_width = bit_width % 64;
      } else if (fn->hasRetAttribute(llvm::Attribute::SExt)) {
        sext_width = bit_width % 64;
      }
    }
  }

  const auto val_idx = llvm_val_idx(val);
  unsigned gp_reg_idx = 0;
  unsigned fp_reg_idx = 0;
  for (unsigned i = 0, cnt = val_part_count(val_idx); i != cnt; i++) {
    auto val_ref = this->val_ref(val_idx, i);
    if (i != cnt - 1) {
      val_ref.inc_ref_count();
    }

    const auto call_conv = this->cur_calling_convention();
    AsmReg reg;
    // TODO: handle out-of-register case
    if (val_ref.bank() == 0) {
      reg = call_conv.ret_regs_gp()[gp_reg_idx++];
    } else {
      reg = call_conv.ret_regs_vec()[fp_reg_idx++];
    }

    if (val_ref.is_const) {
      this->materialize_constant(val_ref, reg);
      if (i == cnt - 1 && sext_width) {
        ext_int(reg, reg, /*sign=*/true, sext_width, 64);
      }
    } else {
      if (val_ref.assignment().fixed_assignment()) {
        val_ref.reload_into_specific(this, reg);
      } else {
        val_ref.move_into_specific(reg);
      }
      if (i == cnt - 1) {
        if (sext_width) {
          ext_int(reg, reg, /*sign=*/true, sext_width, 64);
        } else if (zext_width) {
          ext_int(reg, reg, /*sign=*/false, zext_width, 64);
        }
      }
    }
  }
}

void LLVMCompilerArm64::load_address_of_var_reference(
    AsmReg dst, AssignmentPartRef ap) noexcept {
  const auto &info = variable_refs[ap.assignment->var_ref_custom_idx];
  if (info.alloca) {
    // default handling from CompilerA64
    assert(-static_cast<i32>(info.alloca_frame_off) < 0);
    // per-default, variable references are only used by
    // allocas
    if (!ASMIF(ADDxi, dst, DA_GP(29), info.alloca_frame_off)) {
      materialize_constant(info.alloca_frame_off, 0, 4, dst);
      ASM(ADDx_uxtw, dst, DA_GP(29), dst, 0);
    }
  } else {
    const auto sym = global_sym(
        llvm::cast<llvm::GlobalValue>(adaptor->values[info.val].val));
    assert(sym.valid());
    // These pairs must be contiguous, avoid possible veneers in between.
    this->assembler.text_ensure_space(8);
    if (!info.local) {
      // mov the ptr from the GOT
      this->assembler.reloc_text(
          sym, R_AARCH64_ADR_GOT_PAGE, this->assembler.text_cur_off(), 0);
      ASMNC(ADRP, dst, 0, 0);
      this->assembler.reloc_text(
          sym, R_AARCH64_LD64_GOT_LO12_NC, this->assembler.text_cur_off(), 0);
      ASMNC(LDRxu, dst, dst, 0);
    } else {
      // emit lea with relocation
      this->assembler.reloc_text(
          sym, R_AARCH64_ADR_PREL_PG_HI21, this->assembler.text_cur_off(), 0);
      ASMNC(ADRP, dst, 0, 0);
      this->assembler.reloc_text(
          sym, R_AARCH64_ADD_ABS_LO12_NC, this->assembler.text_cur_off(), 0);
      ASMNC(ADDxi, dst, dst, 0);
    }
  }
}

void LLVMCompilerArm64::ext_int(
    AsmReg dst, AsmReg src, bool sign, unsigned from, unsigned to) noexcept {
  assert(from < to && to <= 64);
  (void)to;
  if (sign) {
    if (to <= 32) {
      ASM(SBFXw, dst, src, 0, from);
    } else {
      ASM(SBFXx, dst, src, 0, from);
    }
  } else {
    if (to <= 32) {
      ASM(UBFXw, dst, src, 0, from);
    } else {
      ASM(UBFXx, dst, src, 0, from);
    }
  }
}

LLVMCompilerArm64::ScratchReg LLVMCompilerArm64::ext_int(GenericValuePart op,
                                                         bool sign,
                                                         unsigned from,
                                                         unsigned to) noexcept {
  ScratchReg scratch{this};
  AsmReg src = gval_as_reg_reuse(op, scratch);
  ext_int(scratch.alloc_gp(), src, sign, from, to);
  return scratch;
}

void LLVMCompilerArm64::extract_element(IRValueRef vec,
                                        unsigned idx,
                                        LLVMBasicValType ty,
                                        ScratchReg &out_reg) noexcept {
  assert(this->val_part_count(vec) == 1);

  ScratchReg tmp{this};
  ValuePartRef vec_ref = this->val_ref(vec, 0);
  AsmReg vec_reg = this->val_as_reg(vec_ref, tmp);
  // TODO: reuse vec_reg if possible
  AsmReg dst_reg = out_reg.alloc(this->adaptor->basic_ty_part_bank(ty));
  switch (ty) {
    using enum LLVMBasicValType;
  case i8: ASM(UMOVwb, dst_reg, vec_reg, idx); break;
  case i16: ASM(UMOVwh, dst_reg, vec_reg, idx); break;
  case i32: ASM(UMOVws, dst_reg, vec_reg, idx); break;
  case i64:
  case ptr: ASM(UMOVxd, dst_reg, vec_reg, idx); break;
  case f32: ASM(DUPs, dst_reg, vec_reg, idx); break;
  case f64: ASM(DUPd, dst_reg, vec_reg, idx); break;
  default: TPDE_UNREACHABLE("unexpected vector element type");
  }

  vec_ref.reset_without_refcount();
}

void LLVMCompilerArm64::insert_element(IRValueRef vec,
                                       unsigned idx,
                                       LLVMBasicValType ty,
                                       GenericValuePart el) noexcept {
  assert(this->val_part_count(vec) == 1);

  ScratchReg tmp{this};
  ValuePartRef vec_ref = this->val_ref(vec, 0);
  AsmReg vec_reg = this->val_as_reg(vec_ref, tmp);
  AsmReg src_reg = this->gval_as_reg(el);
  switch (ty) {
    using enum LLVMBasicValType;
  case i8: ASM(INSbw, vec_reg, idx, src_reg); break;
  case i16: ASM(INShw, vec_reg, idx, src_reg); break;
  case i32: ASM(INSsw, vec_reg, idx, src_reg); break;
  case i64:
  case ptr: ASM(INSdx, vec_reg, idx, src_reg); break;
  case f32: ASM(INSs, vec_reg, idx, src_reg, 0); break;
  case f64: ASM(INSd, vec_reg, idx, src_reg, 0); break;
  default: TPDE_UNREACHABLE("unexpected vector element type");
  }

  assert(vec_ref.assignment().register_valid());
  vec_ref.assignment().set_modified(true);
  vec_ref.reset_without_refcount();
}

void LLVMCompilerArm64::create_frem_calls(const IRValueRef lhs,
                                          const IRValueRef rhs,
                                          ValuePartRef &&res_val,
                                          const bool is_double) noexcept {
  SymRef sym;
  if (is_double) {
    sym = get_libfunc_sym(LibFunc::fmod);
  } else {
    sym = get_libfunc_sym(LibFunc::fmodf);
  }

  std::array<CallArg, 2> args = {
      {CallArg{lhs}, CallArg{rhs}}
  };

  std::variant<ValuePartRef, std::pair<ScratchReg, u8>> res =
      std::move(res_val);

  generate_call(
      sym, args, std::span{&res, 1}, tpde::a64::CallingConv::SYSV_CC, false);
}

bool LLVMCompilerArm64::compile_unreachable(IRValueRef,
                                            llvm::Instruction *) noexcept {
  ASM(UDF, 1);
  this->release_regs_after_return();
  return true;
}

bool LLVMCompilerArm64::compile_alloca(IRValueRef inst_idx,
                                       llvm::Instruction *inst) noexcept {
  auto alloca = llvm::cast<llvm::AllocaInst>(inst);
  assert(this->adaptor->cur_has_dynamic_alloca());

  // refcount
  auto size_ref = this->val_ref(llvm_val_idx(alloca->getArraySize()), 0);
  ValuePartRef res_ref;

  auto &layout = adaptor->mod->getDataLayout();
  if (auto opt = alloca->getAllocationSize(layout); opt) {
    res_ref = this->result_ref_eager(inst_idx, 0);

    const auto size = *opt;
    assert(!size.isScalable());
    auto size_val = size.getFixedValue();
    size_val = tpde::util::align_up(size_val, 16);
    if (size_val >= 0x10'0000) {
      ScratchReg scratch{this};
      auto tmp = scratch.alloc_gp();
      materialize_constant(size_val, 0, 8, tmp);
      ASM(SUBx_uxtx, DA_SP, DA_SP, tmp, 0);
    } else if (size_val > 0) {
      if (size_val >= 0x1000) {
        ASM(SUBxi, DA_SP, DA_SP, size_val & 0xff'f000);
      }
      ASM(SUBxi, DA_SP, DA_SP, size_val & 0xfff);
    }

    if (auto align = alloca->getAlign().value(); align >= 16) {
      // TODO(ae): we could avoid one move here
      align = ~(align - 1);
      ASM(MOV_SPx, res_ref.cur_reg(), DA_SP);
      ASM(ANDxi, DA_SP, res_ref.cur_reg(), align);
    }

    ASM(MOV_SPx, res_ref.cur_reg(), DA_SP);

    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }

  const auto elem_size = layout.getTypeAllocSize(alloca->getAllocatedType());
  ScratchReg scratch{this};
  res_ref = this->result_ref_must_salvage(inst_idx, 0, std::move(size_ref));
  const auto res_reg = res_ref.cur_reg();

  if (elem_size == 0) {
    ASM(MOVZw, res_reg, 0);
  } else if ((elem_size & (elem_size - 1)) == 0) {
    const auto shift = __builtin_ctzll(elem_size);
    if (shift <= 4) {
      ASM(SUBx_uxtx, res_reg, DA_SP, res_reg, shift);
    } else {
      ASM(LSLxi, res_reg, res_reg, shift);
      ASM(SUBx_uxtx, res_reg, DA_SP, res_reg, 0);
    }
  } else {
    ScratchReg scratch{this};
    auto tmp = scratch.alloc_gp();
    materialize_constant(elem_size, 0, 8, tmp);
    ASM(MULx, res_reg, res_reg, tmp);
    ASM(SUBx_uxtx, res_reg, DA_SP, res_reg, 0);
  }

  auto align = alloca->getAlign().value();
  if (align < 16) {
    align = 16;
  }

  // need to keep the stack aligned
  align = ~(align - 1);
  assert(align >> 32 == 0xFFFF'FFFF);

  ASM(ANDxi, res_reg, res_reg, align);
  ASM(MOV_SPx, DA_SP, res_reg);
  this->set_value(res_ref, res_ref.cur_reg());
  return true;
}

bool LLVMCompilerArm64::compile_br(IRValueRef,
                                   llvm::Instruction *inst) noexcept {
  auto *br = llvm::cast<llvm::BranchInst>(inst);

  if (br->isUnconditional()) {
    auto spilled = this->spill_before_branch();

    generate_branch_to_block(
        Jump::jmp, adaptor->block_lookup_idx(br->getSuccessor(0)), false, true);

    release_spilled_regs(spilled);
    return true;
  }

  const auto true_block = adaptor->block_lookup_idx(br->getSuccessor(0));
  const auto false_block = adaptor->block_lookup_idx(br->getSuccessor(1));

  // TODO: use Tbz/Tbnz. Must retain register until branch.
  {
    ScratchReg scratch{this};
    auto cond_ref = this->val_ref(llvm_val_idx(br->getCondition()), 0);
    const auto cond_reg = this->val_as_reg(cond_ref, scratch);
    ASM(TSTwi, cond_reg, 1);
  }

  generate_conditional_branch(Jump::Jne, true_block, false_block);

  return true;
}

void LLVMCompilerArm64::generate_conditional_branch(
    Jump jmp, IRBlockRef true_target, IRBlockRef false_target) noexcept {
  const auto next_block = this->analyzer.block_ref(this->next_block());

  const auto true_needs_split = this->branch_needs_split(true_target);
  const auto false_needs_split = this->branch_needs_split(false_target);

  const auto spilled = this->spill_before_branch();

  if (next_block == true_target ||
      (next_block != false_target && true_needs_split)) {
    generate_branch_to_block(
        invert_jump(jmp), false_target, false_needs_split, false);
    generate_branch_to_block(Jump::jmp, true_target, false, true);
  } else if (next_block == false_target) {
    generate_branch_to_block(jmp, true_target, true_needs_split, false);
    generate_branch_to_block(Jump::jmp, false_target, false, true);
  } else {
    assert(!true_needs_split);
    this->generate_branch_to_block(jmp, true_target, false, false);
    this->generate_branch_to_block(Jump::jmp, false_target, false, true);
  }

  this->release_spilled_regs(spilled);
}

bool LLVMCompilerArm64::compile_inline_asm(IRValueRef,
                                           llvm::CallBase *call) noexcept {
  auto inline_asm = llvm::cast<llvm::InlineAsm>(call->getCalledOperand());
  // TODO: handle inline assembly that actually does something
  if (!inline_asm->getAsmString().empty() || inline_asm->isAlignStack() ||
      !call->getType()->isVoidTy() || call->arg_size() != 0) {
    return false;
  }

  auto constraints = inline_asm->ParseConstraints();
  for (const llvm::InlineAsm::ConstraintInfo &ci : constraints) {
    if (ci.Type != llvm::InlineAsm::isClobber) {
      continue;
    }
    for (const auto &code : ci.Codes) {
      if (code != "{memory}") {
        return false;
      }
    }
  }

  return true;
}

bool LLVMCompilerArm64::compile_call_inner(
    IRValueRef inst_idx,
    llvm::CallBase *call,
    std::variant<SymRef, ValuePartRef> &target,
    bool var_arg) noexcept {
  tpde::util::SmallVector<CallArg, 16> args;
  tpde::util::SmallVector<std::variant<ValuePartRef, std::pair<ScratchReg, u8>>,
                          4>
      results;

  const auto num_args = call->arg_size();
  args.reserve(num_args);

  for (u32 i = 0; i < num_args; ++i) {
    auto *op = call->getArgOperand(i);
    const auto op_idx = llvm_val_idx(op);
    auto flag = CallArg::Flag::none;
    u32 byval_align = 0, byval_size = 0;

    if (call->paramHasAttr(i, llvm::Attribute::AttrKind::ZExt)) {
      flag = CallArg::Flag::zext;
    } else if (call->paramHasAttr(i, llvm::Attribute::AttrKind::SExt)) {
      flag = CallArg::Flag::sext;
    } else if (call->paramHasAttr(i, llvm::Attribute::AttrKind::ByVal)) {
      flag = CallArg::Flag::byval;
      byval_align = call->getParamAlign(i).valueOrOne().value();
      byval_size = this->adaptor->mod->getDataLayout().getTypeAllocSize(
          call->getParamByValType(i));
    } else if (call->paramHasAttr(i, llvm::Attribute::AttrKind::StructRet)) {
      flag = CallArg::Flag::sret;
    }
    assert(!call->paramHasAttr(i, llvm::Attribute::AttrKind::InAlloca));
    assert(!call->paramHasAttr(i, llvm::Attribute::AttrKind::Preallocated));

    args.push_back(CallArg{op_idx, flag, byval_align, byval_size});
  }

  if (!call->getType()->isVoidTy()) {
    const auto res_part_count = val_part_count(inst_idx);
    for (u32 part_idx = 0; part_idx < res_part_count; ++part_idx) {
      auto res_ref = this->result_ref_lazy(inst_idx, part_idx);
      if (part_idx != res_part_count - 1) {
        res_ref.inc_ref_count();
      }
      results.push_back(std::move(res_ref));
    }
  }

  std::variant<SymRef, ScratchReg, ValuePartRef> call_target;
  if (std::holds_alternative<SymRef>(target)) {
    call_target = std::get<SymRef>(target);
  } else {
    call_target = std::move(std::get<ValuePartRef>(target));
  }

  generate_call(std::move(call_target),
                args,
                results,
                tpde::a64::CallingConv::SYSV_CC,
                var_arg);
  return true;
}

bool LLVMCompilerArm64::compile_icmp(IRValueRef inst_idx,
                                     llvm::Instruction *inst,
                                     InstRange remaining) noexcept {
  auto *cmp = llvm::cast<llvm::ICmpInst>(inst);
  auto *cmp_ty = cmp->getOperand(0)->getType();
  assert(cmp_ty->isIntegerTy() || cmp_ty->isPointerTy());
  u32 int_width = 64;
  if (cmp_ty->isIntegerTy()) {
    int_width = cmp_ty->getIntegerBitWidth();
  }

  Jump::Kind jump;
  bool is_signed = false;
  switch (cmp->getPredicate()) {
    using enum llvm::CmpInst::Predicate;
  case ICMP_EQ: jump = Jump::Jeq; break;
  case ICMP_NE: jump = Jump::Jne; break;
  case ICMP_UGT: jump = Jump::Jhi; break;
  case ICMP_UGE: jump = Jump::Jhs; break;
  case ICMP_ULT: jump = Jump::Jlo; break;
  case ICMP_ULE: jump = Jump::Jls; break;
  case ICMP_SGT:
    jump = Jump::Jgt;
    is_signed = true;
    break;
  case ICMP_SGE:
    jump = Jump::Jge;
    is_signed = true;
    break;
  case ICMP_SLT:
    jump = Jump::Jlt;
    is_signed = true;
    break;
  case ICMP_SLE:
    jump = Jump::Jle;
    is_signed = true;
    break;
  default: TPDE_UNREACHABLE("invalid icmp predicate");
  }

  llvm::BranchInst *fuse_br = nullptr;
  llvm::Instruction *fuse_ext = nullptr;
  if (!cmp->user_empty() && remaining.from != remaining.to &&
      (analyzer.liveness_info((u32)val_idx(inst_idx)).ref_count <= 2) &&
      *cmp->user_begin() == cmp->getNextNode()) {
    auto *fuse_inst = cmp->getNextNode();
    assert(cmp->hasNUses(1));
    if (auto *br = llvm::dyn_cast<llvm::BranchInst>(fuse_inst)) {
      assert(br->isConditional() && br->getCondition() == cmp);
      fuse_br = br;
    } else if (llvm::isa<llvm::ZExtInst, llvm::SExtInst>(fuse_inst) &&
               fuse_inst->getType()->getIntegerBitWidth() <= 64) {
      fuse_ext = fuse_inst;
    }
  }

  auto lhs = this->val_ref(llvm_val_idx(cmp->getOperand(0)), 0);
  auto rhs = this->val_ref(llvm_val_idx(cmp->getOperand(1)), 0);
  ScratchReg res_scratch{this};

  if (int_width == 128) {
    auto lhs_high = this->val_ref(llvm_val_idx(cmp->getOperand(0)), 1);
    auto rhs_high = this->val_ref(llvm_val_idx(cmp->getOperand(1)), 1);

    ScratchReg scratch1{this}, scratch2{this}, scratch3{this}, scratch4{this};
    const auto lhs_reg = this->val_as_reg(lhs, scratch1);
    const auto lhs_reg_high = this->val_as_reg(lhs_high, scratch2);
    const auto rhs_reg = this->val_as_reg(rhs, scratch3);
    const auto rhs_reg_high = this->val_as_reg(rhs_high, scratch4);
    if ((jump == Jump::Jeq) || (jump == Jump::Jne)) {
      // Use CCMP for equality
      ASM(CMPx, lhs_reg, rhs_reg);
      ASM(CCMPx, lhs_reg_high, rhs_reg_high, 0, DA_EQ);
    } else {
      // Compare the ints using carried subtraction
      ASM(CMPx, lhs_reg, rhs_reg);
      ASM(SBCSx, DA_ZR, lhs_reg_high, rhs_reg_high);
    }
    lhs_high.reset_without_refcount();
    rhs_high.reset_without_refcount();
    lhs.reset();
    rhs.reset();
  } else {
    if (lhs.is_const && !rhs.is_const) {
      std::swap(lhs, rhs);
      jump = swap_jump(jump).kind;
    }

    GenericValuePart lhs_op = std::move(lhs);
    GenericValuePart rhs_op = std::move(rhs);

    if (int_width != 32 && int_width != 64) {
      unsigned ext_bits = tpde::util::align_up(int_width, 32);
      lhs_op = ext_int(std::move(lhs_op), is_signed, int_width, ext_bits);

      if (!rhs_op.is_imm()) {
        rhs_op = ext_int(std::move(rhs_op), is_signed, int_width, ext_bits);
      }
    }

    AsmReg lhs_reg = gval_as_reg_reuse(lhs_op, res_scratch);

    if (rhs_op.is_imm()) {
      u64 imm = rhs_op.imm().const_u64;
      if (imm == 0 && fuse_br && (jump == Jump::Jeq || jump == Jump::Jne)) {
        // Generate CBZ/CBNZ if possible. However, lhs_reg might be the register
        // corresponding to a PHI node, which gets modified before the branch.
        // We have to detect this case and generate a copy into a separate
        // register. This case is not easy to detect here, though. Therefore,
        // for now we always copy the value into a register that we own.
        // TODO: copy only when lhs_reg belongs to an overwritten PHI node.
        if (res_scratch.cur_reg.invalid()) {
          AsmReg src_reg = lhs_reg;
          lhs_reg = res_scratch.alloc_gp();
          this->mov(lhs_reg, src_reg, int_width <= 32 ? 4 : 8);
          lhs_op.reset();
        }
        rhs_op.reset();

        auto jump_kind = jump == Jump::Jeq ? Jump::Cbz : Jump::Cbnz;
        Jump cbz{jump_kind, lhs_reg, int_width <= 32};
        auto true_block = adaptor->block_lookup_idx(fuse_br->getSuccessor(0));
        auto false_block = adaptor->block_lookup_idx(fuse_br->getSuccessor(1));
        generate_conditional_branch(cbz, true_block, false_block);
        this->adaptor->val_set_fused(*remaining.from, true);
        return true;
      }

      if (is_signed && int_width != 64 && int_width != 32) {
        u64 mask = (1ull << int_width) - 1;
        u64 shift = 64 - int_width;
        imm = ((i64)((imm & mask) << shift)) >> shift;
      }

      ScratchReg rhs_tmp{this};
      if (int_width <= 32) {
        if (!ASMIF(CMPwi, lhs_reg, imm)) {
          this->materialize_constant(imm, 0, 4, rhs_tmp.alloc_gp());
          ASM(CMPw, lhs_reg, rhs_tmp.cur_reg);
        }
      } else {
        if (!ASMIF(CMPxi, lhs_reg, imm)) {
          this->materialize_constant(imm, 0, 4, rhs_tmp.alloc_gp());
          ASM(CMPx, lhs_reg, rhs_tmp.cur_reg);
        }
      }
    } else {
      const auto rhs_reg = gval_as_reg(rhs_op);
      if (int_width <= 32) {
        ASM(CMPw, lhs_reg, rhs_reg);
      } else {
        ASM(CMPx, lhs_reg, rhs_reg);
      }
    }

    lhs_op.reset();
    rhs_op.reset();
  }

  if (fuse_br) {
    auto true_block = adaptor->block_lookup_idx(fuse_br->getSuccessor(0));
    auto false_block = adaptor->block_lookup_idx(fuse_br->getSuccessor(1));
    generate_conditional_branch(jump, true_block, false_block);
    this->adaptor->val_set_fused(*remaining.from, true);
  } else if (fuse_ext) {
    auto res_ref = result_ref_lazy(*remaining.from, 0);
    if (llvm::isa<llvm::ZExtInst>(fuse_ext)) {
      generate_raw_set(jump, res_scratch.alloc_gp());
    } else {
      generate_raw_mask(jump, res_scratch.alloc_gp());
    }
    set_value(res_ref, res_scratch);
    this->adaptor->val_set_fused(*remaining.from, true);
  } else {
    auto res_ref = result_ref_lazy(inst_idx, 0);
    generate_raw_set(jump, res_scratch.alloc_gp());
    set_value(res_ref, res_scratch);
  }

  return true;
}

void LLVMCompilerArm64::compile_i32_cmp_zero(
    AsmReg reg, llvm::CmpInst::Predicate pred) noexcept {
  Da64Cond cond = DA_AL;
  switch (pred) {
  case llvm::CmpInst::ICMP_EQ: cond = DA_EQ; break;
  case llvm::CmpInst::ICMP_NE: cond = DA_NE; break;
  case llvm::CmpInst::ICMP_SGT: cond = DA_GT; break;
  case llvm::CmpInst::ICMP_SGE: cond = DA_GE; break;
  case llvm::CmpInst::ICMP_SLT: cond = DA_LT; break;
  case llvm::CmpInst::ICMP_SLE: cond = DA_LE; break;
  case llvm::CmpInst::ICMP_UGT: cond = DA_HI; break;
  case llvm::CmpInst::ICMP_UGE: cond = DA_HS; break;
  case llvm::CmpInst::ICMP_ULT: cond = DA_LO; break;
  case llvm::CmpInst::ICMP_ULE: cond = DA_LS; break;
  default: TPDE_UNREACHABLE("invalid icmp_zero predicate");
  }
  ASM(CMPwi, reg, 0);
  ASM(CSETw, reg, cond);
}

LLVMCompilerArm64::GenericValuePart
    LLVMCompilerArm64::resolved_gep_to_addr(ResolvedGEP &resolved) noexcept {
  ScratchReg base_scratch{this}, index_scratch{this};
  const AsmReg base = this->val_as_reg(resolved.base, base_scratch);

  GenericValuePart::Expr addr{};
  if (base_scratch.cur_reg.valid()) {
    addr.base = std::move(base_scratch);
  } else {
    if (resolved.base.can_salvage()) {
      base_scratch.alloc_specific(resolved.base.salvage());
      addr.base = std::move(base_scratch);
    } else {
      addr.base = base;
    }
  }

  if (resolved.scale) {
    assert(resolved.index);
    // check for sign-extension
    // TODO(ts): I think technically we need the LLVM bitwidth, no?
    const auto idx_size = resolved.index->part_size();
    if (idx_size == 1 || idx_size == 2 || idx_size == 4) {
      ScratchReg scratch{this};
      if (idx_size == 1) {
        this->encode_sext_8_to_64(std::move(*resolved.index), scratch);
      } else if (idx_size == 2) {
        this->encode_sext_16_to_64(std::move(*resolved.index), scratch);
      } else {
        assert(idx_size == 4);
        this->encode_sext_32_to_64(std::move(*resolved.index), scratch);
      }
      addr.index = std::move(scratch);
    } else {
      assert(idx_size == 8);
      addr.index = this->val_as_reg(*resolved.index, index_scratch);
    }
  }

  addr.scale = resolved.scale;
  addr.disp = resolved.displacement;

  return addr;
}

LLVMCompilerArm64::GenericValuePart
    LLVMCompilerArm64::create_addr_for_alloca(u32 ref_idx) noexcept {
  const auto &info = this->variable_refs[ref_idx];
  assert(info.alloca);
  return GenericValuePart::Expr{AsmReg::R29, info.alloca_frame_off};
}

void LLVMCompilerArm64::switch_emit_cmp(ScratchReg &scratch,
                                        const AsmReg cmp_reg,
                                        const u64 case_value,
                                        const bool width_is_32) noexcept {
  if (width_is_32) {
    if (!ASMIF(CMPwi, cmp_reg, case_value)) {
      const auto tmp = scratch.alloc_gp();
      materialize_constant(case_value, 0, 4, tmp);
      ASM(CMPw, cmp_reg, tmp);
    }
  } else {
    if (!ASMIF(CMPxi, cmp_reg, case_value)) {
      const auto tmp = scratch.alloc_gp();
      materialize_constant(case_value, 0, 4, tmp);
      ASM(CMPx, cmp_reg, tmp);
    }
  }
}

void LLVMCompilerArm64::switch_emit_cmpeq(const Label case_label,
                                          const AsmReg cmp_reg,
                                          const u64 case_value,
                                          const bool width_is_32) noexcept {
  ScratchReg scratch{this};
  switch_emit_cmp(scratch, cmp_reg, case_value, width_is_32);
  generate_raw_jump(Jump::Jeq, case_label);
}

bool LLVMCompilerArm64::switch_emit_jump_table(Label default_label,
                                               std::span<Label> labels,
                                               AsmReg cmp_reg,
                                               u64 low_bound,
                                               u64 high_bound,
                                               bool width_is_32) noexcept {
  (void)default_label;
  (void)labels;
  (void)cmp_reg;
  (void)low_bound;
  (void)high_bound;
  (void)width_is_32;
  // TODO: implement adr/adrp to get address of jump table
#if 0
    ScratchReg scratch{this};
    if (low_bound != 0) {
        switch_emit_cmp(scratch, cmp_reg, low_bound, width_is_32);
        generate_raw_jump(Jump::Jcc, default_label);
    }
    switch_emit_cmp(scratch, cmp_reg, high_bound, width_is_32);
    generate_raw_jump(Jump::Jhi, default_label);

    if (width_is_32) {
        // zero-extend cmp_reg since we use the full width
        ASM(MOVw, cmp_reg, cmp_reg);
    }

    if (low_bound != 0 && !ASMIF(CMPxi, cmp_reg, low_bound)) {
        const auto tmp = scratch.alloc_gp();
        materialize_constant(low_bound, 0, 4, tmp);
        ASM(CMPx, cmp_reg, tmp);
    }

    auto  tmp        = scratch.alloc_gp();
    Label jump_table = assembler.label_create();
    ASM(LEA64rm, tmp, FE_MEM(FE_IP, 0, FE_NOREG, -1));
    // we reuse the jump offset stuff since the patch procedure is the same
    assembler.label_add_unresolved_jump_offset(jump_table,
                                               assembler.text_cur_off() - 4);
    // load the 4 byte displacement from the jump table
    ASM(MOVSXr64m32, cmp_reg, FE_MEM(tmp, 4, cmp_reg, 0));
    ASM(ADD64rr, tmp, cmp_reg);
    ASM(JMPr, tmp);

    assembler.emit_jump_table(jump_table, labels);
#endif
  return false;
}

void LLVMCompilerArm64::switch_emit_binary_step(
    const Label case_label,
    const Label gt_label,
    const AsmReg cmp_reg,
    const u64 case_value,
    const bool width_is_32) noexcept {
  switch_emit_cmpeq(case_label, cmp_reg, case_value, width_is_32);
  generate_raw_jump(Jump::Jhi, gt_label);
}

void LLVMCompilerArm64::create_helper_call(std::span<IRValueRef> args,
                                           std::span<ValuePartRef> results,
                                           SymRef sym) noexcept {
  tpde::util::SmallVector<CallArg, 8> arg_vec{};
  for (auto arg : args) {
    arg_vec.push_back(CallArg{arg});
  }

  tpde::util::SmallVector<std::variant<ValuePartRef, std::pair<ScratchReg, u8>>>
      res_vec{};
  for (auto &res : results) {
    res_vec.push_back(std::move(res));
  }

  generate_call(sym, arg_vec, res_vec, tpde::a64::CallingConv::SYSV_CC, false);
}

bool LLVMCompilerArm64::handle_intrin(IRValueRef inst_idx,
                                      llvm::Instruction *inst,
                                      llvm::Function *fn) noexcept {
  const auto intrin_id = fn->getIntrinsicID();
  switch (intrin_id) {
  case llvm::Intrinsic::vastart: {
    auto list_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    ScratchReg scratch1{this}, scratch2{this};
    auto list_reg = this->val_as_reg(list_ref, scratch1);
    auto tmp_reg = scratch1.alloc_gp();

    // next stack param
    ASM(LDRxu, tmp_reg, DA_GP(29), reg_save_frame_off + 192);
    ASM(STRxu, tmp_reg, list_reg, 0);
    // gr_top = end of GP arg reg save area
    ASM(ADDxi, tmp_reg, DA_GP(29), reg_save_frame_off + 64);
    ASM(STRxu, tmp_reg, list_reg, 8);
    // vr_top = end of VR arg reg save area
    if (reg_save_frame_off + 64 + 128 == 0) {
      ASM(STRxu, DA_GP(29), list_reg, 16);
    } else {
      ASM(ADDxi, tmp_reg, DA_GP(29), reg_save_frame_off + 64 + 128);
      ASM(STRxu, tmp_reg, list_reg, 16);
    }

    uint32_t gr_offs = 0 - ((8 - scalar_arg_count) * 8);
    uint32_t vr_offs = 0 - ((8 - vec_arg_count) * 16);
    materialize_constant(gr_offs, 0, sizeof(uint32_t), tmp_reg);
    ASM(STRwu, tmp_reg, list_reg, 24);
    materialize_constant(vr_offs, 0, sizeof(uint32_t), tmp_reg);
    ASM(STRwu, tmp_reg, list_reg, 28);
    return true;
  }
  case llvm::Intrinsic::vacopy: {
    auto dst_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

    ScratchReg scratch1{this}, scratch2{this};
    ScratchReg scratch3{this}, scratch4{this};
    const auto src_reg = this->val_as_reg(src_ref, scratch1);
    const auto dst_reg = this->val_as_reg(dst_ref, scratch2);

    const auto tmp_reg1 = scratch3.alloc(1);
    const auto tmp_reg2 = scratch4.alloc(1);
    ASM(LDPq, tmp_reg1, tmp_reg2, src_reg, 0);
    ASM(STPq, tmp_reg1, tmp_reg2, dst_reg, 0);
    return true;
  }
  case llvm::Intrinsic::stacksave: {
    auto res_ref = this->result_ref_eager(inst_idx, 0);
    ASM(MOV_SPx, res_ref.cur_reg(), DA_SP);
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  case llvm::Intrinsic::stackrestore: {
    auto val_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    ScratchReg scratch{this};
    auto val_reg = this->val_as_reg(val_ref, scratch);
    ASM(MOV_SPx, DA_SP, val_reg);
    return true;
  }
  case llvm::Intrinsic::trap: {
    ASM(UDF, 1);
    return true;
  }
  case llvm::Intrinsic::aarch64_crc32cx: {
    auto lhs_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto rhs_ref = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    ScratchReg scratch{this};
    AsmReg lhs_reg;
    auto res_ref = this->result_ref_salvage_with_original(
        inst_idx, 0, std::move(lhs_ref), lhs_reg);
    auto rhs_reg = this->val_as_reg(rhs_ref, scratch);
    ASM(CRC32CX, res_ref.cur_reg(), lhs_reg, rhs_reg);
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  default: return false;
  }
}

bool LLVMCompilerArm64::handle_overflow_intrin_128(
    OverflowOp op,
    GenericValuePart lhs_lo,
    GenericValuePart lhs_hi,
    GenericValuePart rhs_lo,
    GenericValuePart rhs_hi,
    ScratchReg &res_lo,
    ScratchReg &res_hi,
    ScratchReg &res_of) noexcept {
  switch (op) {
  case OverflowOp::uadd: {
    AsmReg lhs_lo_reg = gval_as_reg_reuse(lhs_lo, res_lo);
    AsmReg rhs_lo_reg = gval_as_reg_reuse(rhs_lo, res_lo);
    AsmReg res_lo_reg = res_lo.alloc_gp();
    ASM(ADDSx, res_lo_reg, lhs_lo_reg, rhs_lo_reg);
    AsmReg lhs_hi_reg = gval_as_reg_reuse(lhs_hi, res_hi);
    AsmReg rhs_hi_reg = gval_as_reg_reuse(rhs_hi, res_hi);
    AsmReg res_hi_reg = res_hi.alloc_gp();
    ASM(ADCSx, res_hi_reg, lhs_hi_reg, rhs_hi_reg);
    AsmReg res_of_reg = res_of.alloc_gp();
    ASM(CSETw, res_of_reg, DA_CS);
    return true;
  }
  case OverflowOp::sadd: {
    AsmReg lhs_lo_reg = gval_as_reg_reuse(lhs_lo, res_lo);
    AsmReg rhs_lo_reg = gval_as_reg_reuse(rhs_lo, res_lo);
    AsmReg res_lo_reg = res_lo.alloc_gp();
    ASM(ADDSx, res_lo_reg, lhs_lo_reg, rhs_lo_reg);
    AsmReg lhs_hi_reg = gval_as_reg_reuse(lhs_hi, res_hi);
    AsmReg rhs_hi_reg = gval_as_reg_reuse(rhs_hi, res_hi);
    AsmReg res_hi_reg = res_hi.alloc_gp();
    ASM(ADCSx, res_hi_reg, lhs_hi_reg, rhs_hi_reg);
    AsmReg res_of_reg = res_of.alloc_gp();
    ASM(CSETw, res_of_reg, DA_VS);
    return true;
  }

  case OverflowOp::usub: {
    AsmReg lhs_lo_reg = gval_as_reg_reuse(lhs_lo, res_lo);
    AsmReg rhs_lo_reg = gval_as_reg_reuse(rhs_lo, res_lo);
    AsmReg res_lo_reg = res_lo.alloc_gp();
    ASM(SUBSx, res_lo_reg, lhs_lo_reg, rhs_lo_reg);
    AsmReg lhs_hi_reg = gval_as_reg_reuse(lhs_hi, res_hi);
    AsmReg rhs_hi_reg = gval_as_reg_reuse(rhs_hi, res_hi);
    AsmReg res_hi_reg = res_hi.alloc_gp();
    ASM(SBCSx, res_hi_reg, lhs_hi_reg, rhs_hi_reg);
    AsmReg res_of_reg = res_of.alloc_gp();
    ASM(CSETw, res_of_reg, DA_CC);
    return true;
  }
  case OverflowOp::ssub: {
    AsmReg lhs_lo_reg = gval_as_reg_reuse(lhs_lo, res_lo);
    AsmReg rhs_lo_reg = gval_as_reg_reuse(rhs_lo, res_lo);
    AsmReg res_lo_reg = res_lo.alloc_gp();
    ASM(SUBSx, res_lo_reg, lhs_lo_reg, rhs_lo_reg);
    AsmReg lhs_hi_reg = gval_as_reg_reuse(lhs_hi, res_hi);
    AsmReg rhs_hi_reg = gval_as_reg_reuse(rhs_hi, res_hi);
    AsmReg res_hi_reg = res_hi.alloc_gp();
    ASM(SBCSx, res_hi_reg, lhs_hi_reg, rhs_hi_reg);
    AsmReg res_of_reg = res_of.alloc_gp();
    ASM(CSETw, res_of_reg, DA_VS);
    return true;
  }

  case OverflowOp::umul:
  case OverflowOp::smul: {
#if 0
        const auto frame_off = allocate_stack_slot(1);
        ScratchReg scratch{this};
        AsmReg tmp = scratch.alloc_gp();
        if (!ASMIF(ADDxi, tmp, DA_GP(29), frame_off)) {
            materialize_constant(frame_off, 0, 4, tmp);
            ASM(ADDx, tmp, DA_GP(29), tmp);
        }
        // TODO: generate call
        free_stack_slot(frame_off, 1);
#endif
    return false;
  }

  default: TPDE_UNREACHABLE("invalid operation");
  }
}

std::unique_ptr<LLVMCompiler> create_compiler() noexcept {
  auto adaptor = std::make_unique<LLVMAdaptor>();
  return std::make_unique<LLVMCompilerArm64>(std::move(adaptor));
}

} // namespace tpde_llvm::arm64
