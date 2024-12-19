// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <fstream>

#include "LLVMAdaptor.hpp"
#include "LLVMCompilerBase.hpp"
#include "encode_template_x64.hpp"
#include "tpde/x64/CompilerX64.hpp"

namespace tpde_llvm::x64 {

struct CompilerConfig : tpde::x64::PlatformConfig {
  static constexpr bool DEFAULT_VAR_REF_HANDLING = false;
};

struct LLVMCompilerX64 : tpde::x64::CompilerX64<LLVMAdaptor,
                                                LLVMCompilerX64,
                                                LLVMCompilerBase,
                                                CompilerConfig>,
                         tpde_encodegen::EncodeCompiler<LLVMAdaptor,
                                                        LLVMCompilerX64,
                                                        LLVMCompilerBase,
                                                        CompilerConfig> {
  using Base = tpde::x64::CompilerX64<LLVMAdaptor,
                                      LLVMCompilerX64,
                                      LLVMCompilerBase,
                                      CompilerConfig>;
  using EncCompiler = EncodeCompiler<LLVMAdaptor,
                                     LLVMCompilerX64,
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

  static constexpr std::array<AsmReg, 2> LANDING_PAD_RES_REGS = {AsmReg::AX,
                                                                 AsmReg::DX};

  explicit LLVMCompilerX64(std::unique_ptr<LLVMAdaptor> &&adaptor)
      : Base{adaptor.get()}, adaptor(std::move(adaptor)) {
    static_assert(tpde::Compiler<LLVMCompilerX64, tpde::x64::PlatformConfig>);
  }

  [[nodiscard]] static tpde::x64::CallingConv
      cur_calling_convention() noexcept {
    return tpde::x64::CallingConv::SYSV_CC;
  }

  bool arg_is_int128(const IRValueRef val_idx) const noexcept {
    return this->adaptor->values[val_idx].type == LLVMBasicValType::i128;
  }

  void finish_func() noexcept;

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

void LLVMCompilerX64::finish_func() noexcept {
  Base::finish_func();

  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
    time_entry = nullptr;
  }
}

u32 LLVMCompilerX64::val_part_count(const IRValueRef val_idx) const noexcept {
  return this->adaptor->val_part_count(val_idx);
}

u32 LLVMCompilerX64::val_part_size(const IRValueRef val_idx,
                                   const u32 part_idx) const noexcept {
  return this->adaptor->val_part_size(val_idx, part_idx);
}

u8 LLVMCompilerX64::val_part_bank(const IRValueRef val_idx,
                                  const u32 part_idx) const noexcept {
  auto ty = this->adaptor->val_part_ty(val_idx, part_idx);
  switch (ty) {
    using enum LLVMBasicValType;
  case i1:
  case i8:
  case i16:
  case i32:
  case i64:
  case i128:
  case ptr: return 0;
  case f32:
  case f64:
  case v32:
  case v64:
  case v128:
  case v256:
  case v512: return 1;
  case none:
  case invalid:
  case complex: {
    assert(0);
    exit(1);
  }
  }
}

void LLVMCompilerX64::move_val_to_ret_regs(llvm::Value *val) noexcept {
  unsigned zext_width = 0;
  unsigned sext_width = 0;
  unsigned ext_width = 0;
  auto *fn = this->adaptor->cur_func;
  if (auto *ret_ty = fn->getReturnType(); ret_ty->isIntegerTy()) {
    auto bit_width = ret_ty->getIntegerBitWidth();
    ext_width = tpde::util::align_up(bit_width, 32);
    if (bit_width != ext_width) {
      if (fn->hasRetAttribute(llvm::Attribute::ZExt)) {
        zext_width = bit_width % 64;
      } else if (fn->hasRetAttribute(llvm::Attribute::SExt)) {
        sext_width = bit_width % 64;
      }
    }
  }

  const auto val_idx = llvm_val_idx(val);
  unsigned gp_reg_idx = 0;
  unsigned xmm_reg_idx = 0;
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
      reg = call_conv.ret_regs_vec()[xmm_reg_idx++];
    }

    if (val_ref.is_const) {
      this->materialize_constant(val_ref, reg);
      if (i == cnt - 1 && sext_width) {
        ext_int(reg, reg, /*sign=*/true, sext_width, ext_width);
      }
    } else {
      if (val_ref.assignment().fixed_assignment()) {
        val_ref.reload_into_specific(this, reg);
      } else {
        val_ref.move_into_specific(reg);
      }
      if (i == cnt - 1) {
        if (sext_width) {
          ext_int(reg, reg, /*sign=*/true, sext_width, ext_width);
        } else if (zext_width) {
          ext_int(reg, reg, /*sign=*/false, zext_width, ext_width);
        }
      }
    }
  }
}

void LLVMCompilerX64::load_address_of_var_reference(
    AsmReg dst, AssignmentPartRef ap) noexcept {
  const auto &info = variable_refs[ap.assignment->var_ref_custom_idx];
  if (info.alloca) {
    // default handling from CompilerX64
    assert(-static_cast<i32>(info.alloca_frame_off) < 0);
    ASM(LEA64rm,
        dst,
        FE_MEM(FE_BP, 0, FE_NOREG, -static_cast<i32>(info.alloca_frame_off)));
  } else {
    const auto sym = global_sym(
        llvm::cast<llvm::GlobalValue>(adaptor->values[info.val].val));
    assert(sym != Assembler::INVALID_SYM_REF);
    if (!info.local) {
      // mov the ptr from the GOT
      ASM(MOV64rm, dst, FE_MEM(FE_IP, 0, FE_NOREG, -1));
      this->assembler.reloc_text_got(
          sym, this->assembler.text_cur_off() - 4, -4);
    } else {
      // emit lea with relocation
      ASM(LEA64rm, dst, FE_MEM(FE_IP, 0, FE_NOREG, -1));
      this->assembler.reloc_text_pc32(
          sym, this->assembler.text_cur_off() - 4, -4);
    }
  }
}

void LLVMCompilerX64::ext_int(
    AsmReg dst, AsmReg src, bool sign, unsigned from, unsigned to) noexcept {
  assert(from < to && to <= 64);
  if (!sign) {
    switch (from) {
    case 8: ASM(MOVZXr32r8, dst, src); break;
    case 16: ASM(MOVZXr32r16, dst, src); break;
    case 32: ASM(MOV32rr, dst, src); break;
    default:
      if (from < 32) {
        if (dst != src) {
          ASM(MOV32rr, dst, src);
        }
        ASM(AND32ri, dst, (uint32_t{1} << from) - 1);
      } else if (dst != src) {
        ASM(MOV64ri, dst, (uint64_t{1} << from) - 1);
        ASM(AND64rr, dst, src);
      } else {
        ScratchReg tmp{this};
        AsmReg tmp_reg = tmp.alloc_gp();
        ASM(MOV64ri, tmp_reg, (uint64_t{1} << from) - 1);
        ASM(AND64rr, dst, tmp_reg);
      }
    }
  } else if (to <= 32) {
    switch (from) {
    case 8: ASM(MOVSXr32r8, dst, src); break;
    case 16: ASM(MOVSXr32r16, dst, src); break;
    default:
      if (dst != src) {
        ASM(MOV32rr, dst, src);
      }
      ASM(SHL32ri, dst, 32 - from);
      ASM(SAR32ri, dst, 32 - from);
    }
  } else {
    switch (from) {
    case 8: ASM(MOVSXr64r8, dst, src); break;
    case 16: ASM(MOVSXr64r16, dst, src); break;
    case 32: ASM(MOVSXr64r32, dst, src); break;
    default:
      if (dst != src) {
        ASM(MOV64rr, dst, src);
      }
      ASM(SHL64ri, dst, 64 - from);
      ASM(SAR64ri, dst, 64 - from);
    }
  }
}

LLVMCompilerX64::ScratchReg LLVMCompilerX64::ext_int(GenericValuePart op,
                                                     bool sign,
                                                     unsigned from,
                                                     unsigned to) noexcept {
  ScratchReg scratch{this};
  AsmReg src = gval_as_reg_reuse(op, scratch);
  ext_int(scratch.alloc_gp(), src, sign, from, to);
  return scratch;
}

void LLVMCompilerX64::create_frem_calls(const IRValueRef lhs,
                                        const IRValueRef rhs,
                                        ValuePartRef &&res_val,
                                        const bool is_double) noexcept {
  SymRef sym;
  if (is_double) {
    sym = get_or_create_sym_ref(sym_fmod, "fmod");
  } else {
    sym = get_or_create_sym_ref(sym_fmodf, "fmodf");
  }

  std::array<CallArg, 2> args = {
      {CallArg{lhs}, CallArg{rhs}}
  };

  std::variant<ValuePartRef, std::pair<ScratchReg, u8>> res =
      std::move(res_val);

  generate_call(
      sym, args, std::span{&res, 1}, tpde::x64::CallingConv::SYSV_CC, false);
}

bool LLVMCompilerX64::compile_unreachable(IRValueRef,
                                          llvm::Instruction *) noexcept {
  ASM(UD2);
  this->release_regs_after_return();
  return true;
}

bool LLVMCompilerX64::compile_alloca(IRValueRef inst_idx,
                                     llvm::Instruction *inst) noexcept {
  auto alloca = llvm::cast<llvm::AllocaInst>(inst);
  assert(this->adaptor->cur_has_dynamic_alloca());

  // refcount
  auto size_ref = this->val_ref(llvm_val_idx(alloca->getArraySize()), 0);
  ValuePartRef res_ref;

  auto &layout = adaptor->mod.getDataLayout();
  if (auto opt = alloca->getAllocationSize(layout); opt) {
    res_ref = this->result_ref_eager(inst_idx, 0);

    const auto size = *opt;
    assert(!size.isScalable());
    auto size_val = size.getFixedValue();
    size_val = tpde::util::align_up(size_val, 16);
    if (size_val > 0) {
      assert(size < 0x8000'0000);
      ASM(SUB64ri, FE_SP, size_val);
    }
  } else {
    const auto elem_size = layout.getTypeAllocSize(alloca->getAllocatedType());
    ScratchReg scratch{this};
    res_ref = this->result_ref_must_salvage(inst_idx, 0, std::move(size_ref));
    const auto res_reg = res_ref.cur_reg();

    if (elem_size == 0) {
      ASM(XOR32rr, res_reg, res_reg);
    } else if ((elem_size & (elem_size - 1)) == 0) {
      // elSize is power of two
      if (elem_size != 1) {
        const auto shift = __builtin_ctzll(elem_size);
        ASM(SHL64ri, res_reg, shift);
      }
    } else {
      if (elem_size <= 0xFFFF'FFFF) [[likely]] {
        ASM(IMUL64rri, res_reg, res_reg, elem_size);
      } else {
        auto tmp = scratch.alloc_gp();
        ASM(MOV64ri, tmp, elem_size);
        ASM(IMUL64rr, res_reg, tmp);
      }
    }

    ASM(SUB64rr, FE_SP, res_reg);
  }

  auto align = alloca->getAlign().value();
  if (align < 16) {
    align = 16;
  }

  // need to keep the stack aligned
  align = ~(align - 1);
  assert(align >> 32 == 0xFFFF'FFFF);

  ASM(AND64ri, FE_SP, align);
  ASM(MOV64rr, res_ref.cur_reg(), FE_SP);
  this->set_value(res_ref, res_ref.cur_reg());
  return true;
}

bool LLVMCompilerX64::compile_br(IRValueRef, llvm::Instruction *inst) noexcept {
  auto *br = llvm::cast<llvm::BranchInst>(inst);

  if (br->isUnconditional()) {
    auto spilled = this->spill_before_branch();

    generate_branch_to_block(
        Jump::jmp, adaptor->block_lookup[br->getSuccessor(0)], false, true);

    release_spilled_regs(spilled);
    return true;
  }

  const auto true_block = adaptor->block_lookup[br->getSuccessor(0)];
  const auto false_block = adaptor->block_lookup[br->getSuccessor(1)];

  {
    ScratchReg scratch{this};
    auto cond_ref = this->val_ref(llvm_val_idx(br->getCondition()), 0);
    const auto cond_reg = this->val_as_reg(cond_ref, scratch);
    ASM(TEST32ri, cond_reg, 1);
  }

  generate_conditional_branch(Jump::jne, true_block, false_block);

  return true;
}

void LLVMCompilerX64::generate_conditional_branch(
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

bool LLVMCompilerX64::compile_call_inner(
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
      byval_size = this->adaptor->mod.getDataLayout().getTypeAllocSize(
          call->getParamByValType(i));
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
                tpde::x64::CallingConv::SYSV_CC,
                var_arg);
  return true;
}

bool LLVMCompilerX64::compile_icmp(IRValueRef inst_idx,
                                   llvm::Instruction *inst,
                                   InstRange remaining) noexcept {
  auto *cmp = llvm::cast<llvm::ICmpInst>(inst);
  auto *cmp_ty = cmp->getOperand(0)->getType();
  assert(cmp_ty->isIntegerTy() || cmp_ty->isPointerTy());
  u32 int_width = 64;
  if (cmp_ty->isIntegerTy()) {
    int_width = cmp_ty->getIntegerBitWidth();
  }

  Jump jump;
  bool is_signed = false;
  switch (cmp->getPredicate()) {
    using enum llvm::CmpInst::Predicate;
  case ICMP_EQ: jump = Jump::je; break;
  case ICMP_NE: jump = Jump::jne; break;
  case ICMP_UGT: jump = Jump::ja; break;
  case ICMP_UGE: jump = Jump::jae; break;
  case ICMP_ULT: jump = Jump::jb; break;
  case ICMP_ULE: jump = Jump::jbe; break;
  case ICMP_SGT:
    jump = Jump::jg;
    is_signed = true;
    break;
  case ICMP_SGE:
    jump = Jump::jge;
    is_signed = true;
    break;
  case ICMP_SLT:
    jump = Jump::jl;
    is_signed = true;
    break;
  case ICMP_SLE:
    jump = Jump::jle;
    is_signed = true;
    break;
  default: __builtin_unreachable();
  }

  auto lhs = this->val_ref(llvm_val_idx(cmp->getOperand(0)), 0);
  auto rhs = this->val_ref(llvm_val_idx(cmp->getOperand(1)), 0);

  const auto try_fuse_compare =
      [&](std::variant<ValuePartRef, ScratchReg> &&lhs) {
        if ((remaining.from != remaining.to) &&
            (analyzer.liveness_info((u32)val_idx(inst_idx)).ref_count <= 2)) {
          // Check if the next instruction is a condbr
          auto *next = llvm::dyn_cast<llvm::Instruction>(
              adaptor->values[*remaining.from].val);

          if (next->getOpcode() == llvm::Instruction::Br) {
            // Check if the branch uses our result
            auto *branch = llvm::dyn_cast<llvm::BranchInst>(next);
            if (branch->isConditional() && branch->getCondition() == cmp) {
              // free the operands of the compare if possible
              if (std::holds_alternative<ValuePartRef>(lhs)) {
                std::get<ValuePartRef>(lhs).reset();
              } else {
                std::get<ScratchReg>(lhs).reset();
              }

              // generate the branch
              const auto true_block =
                  adaptor->block_lookup[branch->getSuccessor(0)];
              const auto false_block =
                  adaptor->block_lookup[branch->getSuccessor(1)];

              generate_conditional_branch(jump, true_block, false_block);

              this->adaptor->val_set_fused(*remaining.from, true);
              return;
            }
          }
        }

        if (std::holds_alternative<ValuePartRef>(lhs)) {
          auto res_ref = result_ref_salvage(
              inst_idx, 0, std::move(std::get<ValuePartRef>(lhs)));
          generate_raw_set(jump, res_ref.cur_reg());
          set_value(res_ref, res_ref.cur_reg());
        } else {
          auto res_ref = result_ref_lazy(inst_idx, 0);
          generate_raw_set(jump, std::get<ScratchReg>(lhs).cur_reg);
          set_value(res_ref, std::get<ScratchReg>(lhs));
        }
      };

  if (int_width == 128) {
    auto lhs_high = this->val_ref(llvm_val_idx(cmp->getOperand(0)), 1);
    auto rhs_high = this->val_ref(llvm_val_idx(cmp->getOperand(1)), 1);

    // for 128 bit compares, we need to swap the operands sometimes
    if ((jump == Jump::ja) || (jump == Jump::jbe) || (jump == Jump::jle) ||
        (jump == Jump::jg)) {
      std::swap(lhs, rhs);
      std::swap(lhs_high, rhs_high);
      jump = swap_jump(jump);
    }

    // Compare the ints using carried subtraction
    ScratchReg scratch1{this}, scratch2{this}, scratch3{this}, scratch4{this};
    if ((jump == Jump::je) || (jump == Jump::jne)) {
      // for eq,neq do something a bit quicker
      lhs.reload_into_specific_fixed(this, scratch1.alloc_gp());
      lhs_high.reload_into_specific_fixed(this, scratch2.alloc_gp());
      const auto rhs_reg = this->val_as_reg(rhs, scratch3);
      const auto rhs_reg_high = this->val_as_reg(rhs_high, scratch4);

      ASM(XOR64rr, scratch1.cur_reg, rhs_reg);
      ASM(XOR64rr, scratch2.cur_reg, rhs_reg_high);
      ASM(OR64rr, scratch1.cur_reg, scratch2.cur_reg);
    } else {
      const auto lhs_reg = this->val_as_reg(lhs, scratch1);
      auto lhs_high_tmp =
          lhs_high.reload_into_specific_fixed(this, scratch2.alloc_gp());
      const auto rhs_reg = this->val_as_reg(rhs, scratch3);
      const auto rhs_reg_high = this->val_as_reg(rhs_high, scratch4);

      ASM(CMP64rr, lhs_reg, rhs_reg);
      ASM(SBB64rr, lhs_high_tmp, rhs_reg_high);
    }
    lhs_high.reset_without_refcount();
    rhs_high.reset_without_refcount();
    rhs.reset();
    try_fuse_compare(std::move(lhs));
    return true;
  }

  if (lhs.is_const && !rhs.is_const) {
    std::swap(lhs, rhs);
    jump = swap_jump(jump);
  }

  GenericValuePart lhs_op = std::move(lhs);
  GenericValuePart rhs_op = std::move(rhs);

  if (int_width != 32 && int_width != 64) {
    unsigned ext_bits = tpde::util::align_up(int_width, 32);
    lhs_op = ext_int(std::move(lhs_op), is_signed, int_width, ext_bits);
    if (!rhs_op.is_imm()) {
      rhs_op = ext_int(std::move(rhs_op), is_signed, int_width, ext_bits);
    } else if (is_signed) {
      rhs_op = EncodeImm{u64(tpde::util::sext(rhs_op.imm64(), int_width))};
    }
  }

  const auto lhs_reg = gval_as_reg(lhs_op);
  if (int_width <= 32) {
    if (rhs_op.is_imm()) {
      ASM(CMP32ri, lhs_reg, i32(rhs_op.imm64()));
    } else {
      const auto rhs_reg = gval_as_reg(rhs_op);
      ASM(CMP32rr, lhs_reg, rhs_reg);
    }
  } else {
    if (rhs_op.is_imm() && i32(rhs_op.imm64()) == i64(rhs_op.imm64())) {
      ASM(CMP64ri, lhs_reg, rhs_op.imm64());
    } else {
      const auto rhs_reg = gval_as_reg(rhs_op);
      ASM(CMP64rr, lhs_reg, rhs_reg);
    }
  }

  rhs_op.reset();
  if (std::holds_alternative<ValuePartRef>(lhs_op.state)) {
    try_fuse_compare(std::move(std::get<ValuePartRef>(lhs_op.state)));
  } else {
    assert(std::holds_alternative<ScratchReg>(lhs_op.state));
    try_fuse_compare(std::move(std::get<ScratchReg>(lhs_op.state)));
  }
  return true;
}

void LLVMCompilerX64::compile_i32_cmp_zero(
    AsmReg reg, llvm::CmpInst::Predicate pred) noexcept {
  ASM(TEST64rr, reg, reg);
  switch (pred) {
  case llvm::CmpInst::ICMP_EQ: ASM(SETZ8r, reg); break;
  case llvm::CmpInst::ICMP_NE: ASM(SETNZ8r, reg); break;
  case llvm::CmpInst::ICMP_SGT: ASM(SETG8r, reg); break;
  case llvm::CmpInst::ICMP_SGE: ASM(SETGE8r, reg); break;
  case llvm::CmpInst::ICMP_SLT: ASM(SETL8r, reg); break;
  case llvm::CmpInst::ICMP_SLE: ASM(SETLE8r, reg); break;
  case llvm::CmpInst::ICMP_UGT: ASM(SETA8r, reg); break;
  case llvm::CmpInst::ICMP_UGE: ASM(SETNC8r, reg); break;
  case llvm::CmpInst::ICMP_ULT: ASM(SETC8r, reg); break;
  case llvm::CmpInst::ICMP_ULE: ASM(SETBE8r, reg); break;
  default: assert(0);
  }
  ASM(MOVZXr32r8, reg, reg);
}

LLVMCompilerX64::GenericValuePart
    LLVMCompilerX64::resolved_gep_to_addr(ResolvedGEP &resolved) noexcept {
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

LLVMCompilerX64::GenericValuePart
    LLVMCompilerX64::create_addr_for_alloca(u32 ref_idx) noexcept {
  const auto &info = this->variable_refs[ref_idx];
  assert(info.alloca);
  return GenericValuePart::Expr{AsmReg::BP, -(i32)info.alloca_frame_off};
}

void LLVMCompilerX64::switch_emit_cmp(ScratchReg &scratch,
                                      const AsmReg cmp_reg,
                                      const u64 case_value,
                                      const bool width_is_32) noexcept {
  if (width_is_32) {
    ASM(CMP32ri, cmp_reg, case_value);
  } else {
    if ((i64)((i32)case_value) == (i64)case_value) {
      ASM(CMP64ri, cmp_reg, case_value);
    } else {
      const auto tmp = scratch.alloc_gp();
      auto const_ref = ValuePartRef{case_value, 0, 8};
      materialize_constant(const_ref, tmp);
      ASM(CMP64rr, cmp_reg, tmp);
    }
  }
}

void LLVMCompilerX64::switch_emit_cmpeq(const Label case_label,
                                        const AsmReg cmp_reg,
                                        const u64 case_value,
                                        const bool width_is_32) noexcept {
  ScratchReg scratch{this};
  switch_emit_cmp(scratch, cmp_reg, case_value, width_is_32);
  generate_raw_jump(Jump::je, case_label);
}

bool LLVMCompilerX64::switch_emit_jump_table(Label default_label,
                                             std::span<Label> labels,
                                             AsmReg cmp_reg,
                                             u64 low_bound,
                                             u64 high_bound,
                                             bool width_is_32) noexcept {
  ScratchReg scratch{this};
  if (low_bound != 0) {
    switch_emit_cmp(scratch, cmp_reg, low_bound, width_is_32);
    generate_raw_jump(Jump::jb, default_label);
  }
  switch_emit_cmp(scratch, cmp_reg, high_bound, width_is_32);
  generate_raw_jump(Jump::ja, default_label);

  if (width_is_32) {
    // zero-extend cmp_reg since we use the full width
    ASM(MOV32rr, cmp_reg, cmp_reg);
  }

  if (low_bound != 0) {
    if ((i64)((i32)low_bound) == (i64)low_bound) {
      ASM(SUB64ri, cmp_reg, low_bound);
    } else {
      ScratchReg tmp_scratch{this};
      const auto tmp = tmp_scratch.alloc_gp();
      auto const_ref = ValuePartRef{low_bound, 0, 8};
      materialize_constant(const_ref, tmp);
      ASM(SUB64rr, cmp_reg, tmp);
    }
  }

  auto tmp = scratch.alloc_gp();
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
  return true;
}

void LLVMCompilerX64::switch_emit_binary_step(const Label case_label,
                                              const Label gt_label,
                                              const AsmReg cmp_reg,
                                              const u64 case_value,
                                              const bool width_is_32) noexcept {
  switch_emit_cmpeq(case_label, cmp_reg, case_value, width_is_32);
  generate_raw_jump(Jump::ja, gt_label);
}

void LLVMCompilerX64::create_helper_call(std::span<IRValueRef> args,
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

  generate_call(sym, arg_vec, res_vec, tpde::x64::CallingConv::SYSV_CC, false);
}

bool LLVMCompilerX64::handle_intrin(IRValueRef inst_idx,
                                    llvm::Instruction *inst,
                                    llvm::Function *fn) noexcept {
  const auto intrin_id = fn->getIntrinsicID();
  switch (intrin_id) {
  case llvm::Intrinsic::vastart: {
    auto list_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    ScratchReg scratch1{this}, scratch2{this};
    auto list_reg = this->val_as_reg(list_ref, scratch1);
    auto tmp_reg = scratch1.alloc_gp();

    u64 combined_off = (((static_cast<u64>(vec_arg_count) * 16) + 48) << 32) |
                       (static_cast<u64>(scalar_arg_count) * 8);
    ASM(MOV64ri, tmp_reg, combined_off);
    ASM(MOV64mr, FE_MEM(list_reg, 0, FE_NOREG, 0), tmp_reg);

    assert(-static_cast<i32>(reg_save_frame_off) < 0);
    ASM(LEA64rm, tmp_reg, FE_MEM(FE_BP, 0, FE_NOREG, -(i32)reg_save_frame_off));
    ASM(MOV64mr, FE_MEM(list_reg, 0, FE_NOREG, 16), tmp_reg);

    ASM(LEA64rm, tmp_reg, FE_MEM(FE_BP, 0, FE_NOREG, (i32)var_arg_stack_off));
    ASM(MOV64mr, FE_MEM(list_reg, 0, FE_NOREG, 8), tmp_reg);
    return true;
  }
  case llvm::Intrinsic::vacopy: {
    auto dst_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

    ScratchReg scratch1{this}, scratch2{this}, scratch3{this};
    const auto src_reg = this->val_as_reg(src_ref, scratch1);
    const auto dst_reg = this->val_as_reg(dst_ref, scratch2);

    const auto tmp_reg = scratch3.alloc(1);
    ASM(SSE_MOVDQUrm, tmp_reg, FE_MEM(src_reg, 0, FE_NOREG, 0));
    ASM(SSE_MOVDQUmr, FE_MEM(dst_reg, 0, FE_NOREG, 0), tmp_reg);

    ASM(SSE_MOVQrm, tmp_reg, FE_MEM(src_reg, 0, FE_NOREG, 16));
    ASM(SSE_MOVQmr, FE_MEM(dst_reg, 0, FE_NOREG, 16), tmp_reg);
    return true;
  }
  case llvm::Intrinsic::stacksave: {
    auto res_ref = this->result_ref_eager(inst_idx, 0);
    ASM(MOV64rr, res_ref.cur_reg(), FE_SP);
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  case llvm::Intrinsic::stackrestore: {
    auto val_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    ScratchReg scratch{this};
    auto val_reg = this->val_as_reg(val_ref, scratch);
    ASM(MOV64rr, FE_SP, val_reg);
    return true;
  }
  case llvm::Intrinsic::trap: {
    ASM(UD2);
    return true;
  }
  default: return false;
  }
}

bool LLVMCompilerX64::handle_overflow_intrin_128(OverflowOp op,
                                                 GenericValuePart lhs_lo,
                                                 GenericValuePart lhs_hi,
                                                 GenericValuePart rhs_lo,
                                                 GenericValuePart rhs_hi,
                                                 ScratchReg &res_lo,
                                                 ScratchReg &res_hi,
                                                 ScratchReg &res_of) noexcept {
  using EncodeFnTy = bool (LLVMCompilerX64::*)(GenericValuePart,
                                               GenericValuePart,
                                               GenericValuePart,
                                               GenericValuePart,
                                               ScratchReg &,
                                               ScratchReg &,
                                               ScratchReg &);
  EncodeFnTy encode_fn = nullptr;
  switch (op) {
  case OverflowOp::uadd:
    encode_fn = &LLVMCompilerX64::encode_of_add_u128;
    break;
  case OverflowOp::sadd:
    encode_fn = &LLVMCompilerX64::encode_of_add_i128;
    break;
  case OverflowOp::usub:
    encode_fn = &LLVMCompilerX64::encode_of_sub_u128;
    break;
  case OverflowOp::ssub:
    encode_fn = &LLVMCompilerX64::encode_of_sub_i128;
    break;
  case OverflowOp::umul:
    encode_fn = &LLVMCompilerX64::encode_of_mul_u128;
    break;
  case OverflowOp::smul:
    encode_fn = &LLVMCompilerX64::encode_of_mul_i128;
    break;
  default: __builtin_unreachable();
  }

  return (this->*encode_fn)(std::move(lhs_lo),
                            std::move(lhs_hi),
                            std::move(rhs_lo),
                            std::move(rhs_hi),
                            res_lo,
                            res_hi,
                            res_of);
}

extern bool compile_llvm(llvm::Module &mod, std::vector<u8> &out_buf) {
  auto adaptor = std::make_unique<LLVMAdaptor>(mod.getContext(), mod);
  auto compiler = std::make_unique<LLVMCompilerX64>(std::move(adaptor));

  if (!compiler->compile()) {
    return false;
  }

  llvm::TimeTraceProfilerEntry *time_entry = nullptr;
  if (llvm::timeTraceProfilerEnabled()) {
    time_entry = llvm::timeTraceProfilerBegin("TPDE_EmitObj", "");
  }
  std::vector<u8> data = compiler->assembler.build_object_file();
  out_buf = std::move(data);
  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
  }

  return true;
}

} // namespace tpde_llvm::x64
