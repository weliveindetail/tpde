// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <fstream>

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>

#include "LLVMAdaptor.hpp"
#include "LLVMCompilerBase.hpp"
#include "encode_template_x64.hpp"
#include "tpde/base.hpp"
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

  std::unique_ptr<LLVMAdaptor> adaptor;

  static constexpr std::array<AsmReg, 2> LANDING_PAD_RES_REGS = {AsmReg::AX,
                                                                 AsmReg::DX};

  explicit LLVMCompilerX64(std::unique_ptr<LLVMAdaptor> &&adaptor)
      : Base{adaptor.get()}, adaptor(std::move(adaptor)) {
    static_assert(tpde::Compiler<LLVMCompilerX64, tpde::x64::PlatformConfig>);
  }

  void reset() noexcept {
    // TODO: move to LLVMCompilerBase
    Base::reset();
    EncCompiler::reset();
  }

  [[nodiscard]] static tpde::x64::CallingConv
      cur_calling_convention() noexcept {
    return tpde::x64::CallingConv::SYSV_CC;
  }

  bool arg_is_int128(const IRValueRef value) const noexcept {
    return value->getType()->isIntegerTy(128);
  }

  bool arg_allow_split_reg_stack_passing(
      const IRValueRef val_idx) const noexcept {
    // we allow splitting the value if it is an aggregate but not if it is an
    // i128
    return !arg_is_int128(val_idx);
  }

  void finish_func(u32 func_idx) noexcept;

  void move_val_to_ret_regs(llvm::Value *) noexcept;

  void load_address_of_var_reference(AsmReg dst, AssignmentPartRef ap) noexcept;

  void ext_int(
      AsmReg dst, AsmReg src, bool sign, unsigned from, unsigned to) noexcept;
  ScratchReg ext_int(GenericValuePart op,
                     bool sign,
                     unsigned from,
                     unsigned to) noexcept;

  bool compile_unreachable(const llvm::Instruction *,
                           const ValInfo &,
                           u64) noexcept;
  bool compile_alloca(const llvm::Instruction *, const ValInfo &, u64) noexcept;
  bool compile_br(const llvm::Instruction *, const ValInfo &, u64) noexcept;
  void generate_conditional_branch(Jump jmp,
                                   IRBlockRef true_target,
                                   IRBlockRef false_target) noexcept;
  bool compile_inline_asm(const llvm::CallBase *) noexcept;
  bool compile_call_inner(const llvm::CallBase *,
                          std::variant<SymRef, ValuePartRef> &,
                          bool) noexcept;
  bool compile_icmp(const llvm::Instruction *, const ValInfo &, u64) noexcept;
  void compile_i32_cmp_zero(AsmReg reg, llvm::CmpInst::Predicate p) noexcept;

  void resolved_gep_to_base_reg(ResolvedGEP &resolved) noexcept;
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
                          std::span<ValuePart> results,
                          SymRef sym) noexcept;

  bool handle_intrin(const llvm::IntrinsicInst *) noexcept;

  bool handle_overflow_intrin_128(OverflowOp op,
                                  GenericValuePart lhs_lo,
                                  GenericValuePart lhs_hi,
                                  GenericValuePart rhs_lo,
                                  GenericValuePart rhs_hi,
                                  ScratchReg &res_lo,
                                  ScratchReg &res_hi,
                                  ScratchReg &res_of) noexcept;
};

void LLVMCompilerX64::finish_func(u32 func_idx) noexcept {
  Base::finish_func(func_idx);

  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
    time_entry = nullptr;
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

  unsigned gp_reg_idx = 0;
  unsigned xmm_reg_idx = 0;
  unsigned cnt = this->adaptor->val_part_count(val);
  auto vr = this->val_ref(val);
  for (unsigned i = 0; i != cnt; i++) {
    auto val_ref = vr.part(i);

    const auto call_conv = this->cur_calling_convention();
    AsmReg reg;
    // TODO: handle out-of-register case
    if (val_ref.bank() == CompilerConfig::GP_BANK) {
      reg = call_conv.ret_regs_gp()[gp_reg_idx++];
    } else {
      reg = call_conv.ret_regs_vec()[xmm_reg_idx++];
    }

    // TODO: fix registers to ensure results are not clobbered by following part
    if (val_ref.is_const()) {
      val_ref.reload_into_specific_fixed(reg);
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
    assert(info.alloca_frame_off <= 0);
    ASM(LEA64rm, dst, FE_MEM(FE_BP, 0, FE_NOREG, info.alloca_frame_off));
  } else {
    auto *global = llvm::cast<llvm::GlobalValue>(info.val);
    const auto sym = global_sym(global);
    assert(sym.valid());
    if (global->isThreadLocal()) {
      // LLVM historically allowed TLS globals to be used as pointers and
      // generate TLS access calls when the pointer is used. This caused
      // problems with coroutines, leading to the addition of the intrinsic
      // llvm.threadlocal.address in 2022; deprecation of the original behavior
      // was considered. Clang now only generates the intrinsic, and other
      // front-ends should do that, too.
      //
      // Here, generating a function call would be highly problematic. This
      // method gets called when allocating/locking ValuePartRefs; it is quite
      // likely that some registers are already fixed at this point. Doing a
      // regular function call would require to spill/move all these values,
      // adding fragile code for a somewhat-deprecated feature. Therefore, we
      // only support access to thread-local variables through the intrinsic.
      TPDE_FATAL("thread-local variable access without intrinsic");
    }
    // External weak might be undefined, hence cannot use relative addressing.
    if (!global->isDSOLocal() || global->hasExternalWeakLinkage()) {
      // mov the ptr from the GOT
      ASM(MOV64rm, dst, FE_MEM(FE_IP, 0, FE_NOREG, -1));
      this->assembler.reloc_text(
          sym, R_X86_64_GOTPCREL, this->assembler.text_cur_off() - 4, -4);
    } else {
      // emit lea with relocation
      ASM(LEA64rm, dst, FE_MEM(FE_IP, 0, FE_NOREG, -1));
      this->assembler.reloc_text(
          sym, R_X86_64_PC32, this->assembler.text_cur_off() - 4, -4);
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

bool LLVMCompilerX64::compile_unreachable(const llvm::Instruction *,
                                          const ValInfo &,
                                          u64) noexcept {
  ASM(UD2);
  this->release_regs_after_return();
  return true;
}

bool LLVMCompilerX64::compile_alloca(const llvm::Instruction *inst,
                                     const ValInfo &,
                                     u64) noexcept {
  const auto *alloca = llvm::cast<llvm::AllocaInst>(inst);
  assert(this->adaptor->cur_has_dynamic_alloca());

  // refcount
  auto [size_vr, size_ref] = this->val_ref_single(alloca->getArraySize());
  auto [res_vr, res_ref] = this->result_ref_single(alloca);

  auto &layout = adaptor->mod->getDataLayout();
  if (auto opt = alloca->getAllocationSize(layout); opt) {
    const auto size = *opt;
    assert(!size.isScalable());
    auto size_val = size.getFixedValue();
    size_val = tpde::util::align_up(size_val, 16);
    if (size_val > 0) {
      assert(size < 0x8000'0000);
      ASM(SUB64ri, FE_SP, size_val);
    }

    res_ref.alloc_reg();
  } else {
    const auto elem_size = layout.getTypeAllocSize(alloca->getAllocatedType());
    ScratchReg scratch{this};

    ValuePartRef res = std::move(size_ref).into_temporary();

    if (elem_size == 0) {
      ASM(XOR32rr, res.cur_reg(), res.cur_reg());
    } else if ((elem_size & (elem_size - 1)) == 0) {
      // elSize is power of two
      if (elem_size != 1) {
        const auto shift = __builtin_ctzll(elem_size);
        ASM(SHL64ri, res.cur_reg(), shift);
      }
    } else {
      if (elem_size <= 0x7FFF'FFFF) [[likely]] {
        ASM(IMUL64rri, res.cur_reg(), res.cur_reg(), elem_size);
      } else {
        auto tmp = scratch.alloc_gp();
        ASM(MOV64ri, tmp, elem_size);
        ASM(IMUL64rr, res.cur_reg(), tmp);
      }
    }

    ASM(SUB64rr, FE_SP, res.cur_reg());
    res_ref.set_value(std::move(res));
    res_ref.lock();
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
  return true;
}

bool LLVMCompilerX64::compile_br(const llvm::Instruction *inst,
                                 const ValInfo &,
                                 u64) noexcept {
  const auto *br = llvm::cast<llvm::BranchInst>(inst);
  if (br->isUnconditional()) {
    auto spilled = this->spill_before_branch();

    generate_branch_to_block(
        Jump::jmp, adaptor->block_lookup_idx(br->getSuccessor(0)), false, true);

    release_spilled_regs(spilled);
    return true;
  }

  const auto true_block = adaptor->block_lookup_idx(br->getSuccessor(0));
  const auto false_block = adaptor->block_lookup_idx(br->getSuccessor(1));

  {
    auto [_, cond_ref] = this->val_ref_single(br->getCondition());
    const auto cond_reg = cond_ref.load_to_reg();
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

bool LLVMCompilerX64::compile_inline_asm(const llvm::CallBase *call) noexcept {
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
      if (code != "{memory}" && code != "{dirflag}" && code != "{fpsr}" &&
          code != "{flags}") {
        return false;
      }
    }
  }

  return true;
}

bool LLVMCompilerX64::compile_call_inner(
    const llvm::CallBase *call,
    std::variant<SymRef, ValuePartRef> &target,
    bool var_arg) noexcept {
  ValueRef res{this}; // must outlive results.
  tpde::util::SmallVector<CallArg, 16> args;
  tpde::util::SmallVector<ValuePart, 4> results;

  const auto num_args = call->arg_size();
  args.reserve(num_args);

  for (u32 i = 0; i < num_args; ++i) {
    auto *op = call->getArgOperand(i);
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
    }
    assert(!call->paramHasAttr(i, llvm::Attribute::AttrKind::InAlloca));
    assert(!call->paramHasAttr(i, llvm::Attribute::AttrKind::Preallocated));

    args.push_back(CallArg{op, flag, byval_align, byval_size});
  }

  if (!call->getType()->isVoidTy()) {
    res = this->result_ref(call);
    const auto res_part_count = res.assignment()->part_count;
    for (u32 part_idx = 0; part_idx < res_part_count; ++part_idx) {
      results.push_back(res.part(part_idx));
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

bool LLVMCompilerX64::compile_icmp(const llvm::Instruction *inst,
                                   const ValInfo &,
                                   u64) noexcept {
  const auto *cmp = llvm::cast<llvm::ICmpInst>(inst);
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
  default: TPDE_UNREACHABLE("invalid icmp predicate");
  }

  const llvm::BranchInst *fuse_br = nullptr;
  const llvm::Instruction *fuse_ext = nullptr;
  if (!cmp->user_empty() && *cmp->user_begin() == cmp->getNextNode() &&
      (analyzer.liveness_info((u32)val_idx(cmp)).ref_count <= 2)) {
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

  auto lhs = this->val_ref(cmp->getOperand(0));
  auto rhs = this->val_ref(cmp->getOperand(1));
  ScratchReg res_scratch{this};

  if (int_width == 128) {
    // for 128 bit compares, we need to swap the operands sometimes
    if ((jump == Jump::ja) || (jump == Jump::jbe) || (jump == Jump::jle) ||
        (jump == Jump::jg)) {
      std::swap(lhs, rhs);
      jump = swap_jump(jump);
    }

    auto rhs_lo = rhs.part(0);
    auto rhs_hi = rhs.part(1);
    auto rhs_reg_lo = rhs_lo.load_to_reg();
    auto rhs_reg_hi = rhs_hi.load_to_reg();

    // Compare the ints using carried subtraction
    if ((jump == Jump::je) || (jump == Jump::jne)) {
      // for eq,neq do something a bit quicker
      ScratchReg scratch{this};
      lhs.part(0).reload_into_specific_fixed(this, res_scratch.alloc_gp());
      lhs.part(1).reload_into_specific_fixed(this, scratch.alloc_gp());

      ASM(XOR64rr, res_scratch.cur_reg(), rhs_reg_lo);
      ASM(XOR64rr, scratch.cur_reg(), rhs_reg_hi);
      ASM(OR64rr, res_scratch.cur_reg(), scratch.cur_reg());
    } else {
      auto lhs_lo = lhs.part(0);
      auto lhs_reg_lo = lhs_lo.load_to_reg();
      auto lhs_high_tmp =
          lhs.part(1).reload_into_specific_fixed(this, res_scratch.alloc_gp());

      ASM(CMP64rr, lhs_reg_lo, rhs_reg_lo);
      ASM(SBB64rr, lhs_high_tmp, rhs_reg_hi);
    }
  } else {
    GenericValuePart lhs_op = lhs.part(0);
    GenericValuePart rhs_op = rhs.part(0);

    if (lhs_op.is_imm() && !rhs_op.is_imm()) {
      std::swap(lhs_op, rhs_op);
      jump = swap_jump(jump);
    }
    u64 rhs_imm;

    if (int_width != 32 && int_width != 64) {
      unsigned ext_bits = tpde::util::align_up(int_width, 32);
      lhs_op = ext_int(std::move(lhs_op), is_signed, int_width, ext_bits);
      if (!rhs_op.is_imm()) {
        rhs_op = ext_int(std::move(rhs_op), is_signed, int_width, ext_bits);
      } else if (is_signed) {
        rhs_imm = tpde::util::sext(rhs_op.imm64(), int_width);
        rhs_op = ValuePartRef{this, &rhs_imm, 8, CompilerConfig::GP_BANK};
      }
    }

    AsmReg lhs_reg;
    if (fuse_br) {
      lhs_reg = gval_as_reg(lhs_op);
    } else {
      lhs_reg = gval_as_reg_reuse(lhs_op, res_scratch);
    }
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

    lhs_op.reset();
    rhs_op.reset();
  }

  if (fuse_br) {
    auto true_block = adaptor->block_lookup_idx(fuse_br->getSuccessor(0));
    auto false_block = adaptor->block_lookup_idx(fuse_br->getSuccessor(1));
    generate_conditional_branch(jump, true_block, false_block);
    this->adaptor->inst_set_fused(fuse_br, true);
  } else if (fuse_ext) {
    auto [_, res_ref] = result_ref_single(fuse_ext);
    if (llvm::isa<llvm::ZExtInst>(fuse_ext)) {
      generate_raw_set(jump, res_scratch.alloc_gp());
    } else {
      generate_raw_mask(jump, res_scratch.alloc_gp());
    }
    set_value(res_ref, res_scratch);
    this->adaptor->inst_set_fused(fuse_ext, true);
  } else {
    auto [_, res_ref] = result_ref_single(cmp);
    generate_raw_set(jump, res_scratch.alloc_gp());
    set_value(res_ref, res_scratch);
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
  default: TPDE_UNREACHABLE("invalid icmp_zero predicate");
  }
  ASM(MOVZXr32r8, reg, reg);
}

void LLVMCompilerX64::resolved_gep_to_base_reg(ResolvedGEP &gep) noexcept {
  GenericValuePart operand = resolved_gep_to_addr(gep);
  AsmReg res_reg = gval_as_reg(operand);

  if (auto *op_reg = std::get_if<ScratchReg>(&operand.state)) {
    gep.base = std::move(*op_reg);
  } else {
    ScratchReg result{this};
    AsmReg copy_reg = result.alloc_gp();
    ASM(MOV64rr, copy_reg, res_reg);
    gep.base = std::move(result);
  }

  gep.index = std::nullopt;
  gep.displacement = 0;
  gep.idx_size_bits = 0;
  gep.scale = 0;
}

LLVMCompilerX64::GenericValuePart
    LLVMCompilerX64::create_addr_for_alloca(u32 ref_idx) noexcept {
  const auto &info = this->variable_refs[ref_idx];
  assert(info.alloca);
  return GenericValuePart::Expr{AsmReg::BP, info.alloca_frame_off};
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
      auto const_ref =
          ValuePartRef{this, &case_value, 8, CompilerConfig::GP_BANK};
      const_ref.reload_into_specific_fixed(tmp);
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
      ValuePartRef const_ref{this, &low_bound, 8, CompilerConfig::GP_BANK};
      ASM(SUB64rr, cmp_reg, const_ref.load_to_reg());
    }
  }

  auto tmp = scratch.alloc_gp();
  Label jump_table = assembler.label_create();
  ASM(LEA64rm, tmp, FE_MEM(FE_IP, 0, FE_NOREG, -1));
  // we reuse the jump offset stuff since the patch procedure is the same
  assembler.add_unresolved_entry(
      jump_table,
      assembler.text_cur_off() - 4,
      Assembler::UnresolvedEntryKind::JMP_OR_MEM_DISP);
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
                                         std::span<ValuePart> results,
                                         SymRef sym) noexcept {
  tpde::util::SmallVector<CallArg, 8> arg_vec{};
  for (auto arg : args) {
    arg_vec.push_back(CallArg{arg});
  }

  generate_call(sym, arg_vec, results, tpde::x64::CallingConv::SYSV_CC, false);
}

bool LLVMCompilerX64::handle_intrin(const llvm::IntrinsicInst *inst) noexcept {
  const auto intrin_id = inst->getIntrinsicID();
  switch (intrin_id) {
  case llvm::Intrinsic::vastart: {
    auto [_, list_ref] = this->val_ref_single(inst->getOperand(0));
    ScratchReg scratch1{this};
    auto list_reg = list_ref.load_to_reg();
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
    auto [dst_vr, dst_ref] = this->val_ref_single(inst->getOperand(0));
    auto [src_vr, src_ref] = this->val_ref_single(inst->getOperand(1));

    ScratchReg scratch{this};
    const auto src_reg = src_ref.load_to_reg();
    const auto dst_reg = dst_ref.load_to_reg();

    const auto tmp_reg = scratch.alloc(CompilerConfig::FP_BANK);
    ASM(SSE_MOVDQUrm, tmp_reg, FE_MEM(src_reg, 0, FE_NOREG, 0));
    ASM(SSE_MOVDQUmr, FE_MEM(dst_reg, 0, FE_NOREG, 0), tmp_reg);

    ASM(SSE_MOVQrm, tmp_reg, FE_MEM(src_reg, 0, FE_NOREG, 16));
    ASM(SSE_MOVQmr, FE_MEM(dst_reg, 0, FE_NOREG, 16), tmp_reg);
    return true;
  }
  case llvm::Intrinsic::stacksave: {
    ValuePartRef res{this, CompilerConfig::GP_BANK};
    ASM(MOV64rr, res.alloc_reg(), FE_SP);
    this->result_ref(inst).part(0).set_value(std::move(res));
    return true;
  }
  case llvm::Intrinsic::stackrestore: {
    auto [val_vr, val_ref] = this->val_ref_single(inst->getOperand(0));
    auto val_reg = val_ref.load_to_reg();
    ASM(MOV64rr, FE_SP, val_reg);
    return true;
  }
  case llvm::Intrinsic::x86_sse42_crc32_64_64: {
    auto [rhs_vr, rhs_ref] = this->val_ref_single(inst->getOperand(1));
    auto res = this->val_ref(inst->getOperand(0)).part(0).into_temporary();
    ASM(CRC32_64rr, res.cur_reg(), rhs_ref.load_to_reg());
    this->result_ref(inst).part(0).set_value(std::move(res));
    return true;
  }
  case llvm::Intrinsic::returnaddress: {
    ValuePartRef res{this, CompilerConfig::GP_BANK};
    res.alloc_reg();
    auto op = llvm::cast<llvm::ConstantInt>(inst->getOperand(0));
    if (op->isZeroValue()) {
      ASM(MOV64rm, res.cur_reg(), FE_MEM(FE_BP, 0, FE_NOREG, 8));
    } else {
      ASM(XOR32rr, res.cur_reg(), res.cur_reg());
    }
    this->result_ref(inst).part(0).set_value(std::move(res));
    return true;
  }
  case llvm::Intrinsic::frameaddress: {
    ValuePartRef res{this, CompilerConfig::GP_BANK};
    res.alloc_reg();
    auto op = llvm::cast<llvm::ConstantInt>(inst->getOperand(0));
    if (op->isZeroValue()) {
      ASM(MOV64rr, res.cur_reg(), FE_BP);
    } else {
      ASM(XOR32rr, res.cur_reg(), res.cur_reg());
    }
    this->result_ref(inst).part(0).set_value(std::move(res));
    return true;
  }
  case llvm::Intrinsic::trap: ASM(UD2); return true;
  case llvm::Intrinsic::debugtrap: ASM(INT3); return true;
  case llvm::Intrinsic::x86_sse2_pause: ASM(PAUSE); return true;
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
  using EncodeFnTy = bool (LLVMCompilerX64::*)(GenericValuePart &&,
                                               GenericValuePart &&,
                                               GenericValuePart &&,
                                               GenericValuePart &&,
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
  default: TPDE_UNREACHABLE("invalid operation");
  }

  return (this->*encode_fn)(std::move(lhs_lo),
                            std::move(lhs_hi),
                            std::move(rhs_lo),
                            std::move(rhs_hi),
                            res_lo,
                            res_hi,
                            res_of);
}

std::unique_ptr<LLVMCompiler> create_compiler() noexcept {
  auto adaptor = std::make_unique<LLVMAdaptor>();
  return std::make_unique<LLVMCompilerX64>(std::move(adaptor));
}

} // namespace tpde_llvm::x64
