// SPDX-License-Identifier: LicenseRef-Proprietary

#include "LLVMAdaptor.hpp"

#include <format>

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/TimeProfiler.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>

#include "base.hpp"
#include "tpde/base.hpp"
#include "tpde/util/misc.hpp"

namespace tpde_llvm {

std::pair<llvm::Value *, llvm::Instruction *>
    LLVMAdaptor::fixup_constant(llvm::Constant *cst,
                                llvm::Instruction *ins_before) {
  if (auto *cexpr = llvm::dyn_cast<llvm::ConstantExpr>(cst)) [[unlikely]] {
    // clang creates these and we don't want to handle them so
    // we split them up into their own values
    llvm::Instruction *expr_inst = cexpr->getAsInstruction();
    expr_inst->insertBefore(ins_before);
    return {expr_inst, expr_inst};
  }

  if (auto *cv = llvm::dyn_cast<llvm::ConstantVector>(cst)) [[unlikely]] {
    // TODO: optimize so that all supported constants are in a top-level
    // ConstantDataVector. E.g., <poison, 0> could be replaced with zero.
    llvm::Instruction *ins_begin = nullptr;
    llvm::Type *el_ty = cv->getType()->getScalarType();
    llvm::Constant *el_zero = llvm::Constant::getNullValue(el_ty);

    llvm::SmallVector<llvm::Constant *> base;
    llvm::SmallVector<llvm::Value *> repls;
    for (auto it : llvm::enumerate(cv->operands())) {
      auto *cst = llvm::cast<llvm::Constant>(it.value());
      if (llvm::isa<llvm::UndefValue, llvm::PoisonValue>(cst)) {
        // replace undef/poison with zero
        base.push_back(el_zero);
      } else if (llvm::isa<llvm::ConstantData>(cst)) {
        // other ConstantData (null pointer, int, fp) is fine
        base.push_back(cst);
      } else {
        assert((llvm::isa<llvm::GlobalValue, llvm::ConstantExpr>(cst)) &&
               "unexpected constant type in vector");
        base.push_back(el_zero);
        repls.resize(cv->getNumOperands());
        if (auto [repl, inst] = fixup_constant(cst, ins_before); repl) {
          repls[it.index()] = repl;
          if (!ins_begin) {
            ins_begin = inst;
          }
        } else {
          repls[it.index()] = cst;
        }
      }
    }

    llvm::Value *repl = llvm::ConstantVector::get(base);
    // NB: this is likely a ConstantDataSequential, but could also be a
    // ConstantAggregateZero or still a ConstantVector for weird types.
    if (repls.empty()) {
      return {repl, nullptr};
    }

    llvm::Type *i32 = llvm::Type::getInt32Ty(*context);
    for (auto it : llvm::enumerate(repls)) {
      auto *el = it.value() ? it.value() : cv->getOperand(it.index());
      auto *idx_val = llvm::ConstantInt::get(i32, it.index());
      repl = llvm::InsertElementInst::Create(repl, el, idx_val, "", ins_before);
      if (!ins_begin) {
        ins_begin = llvm::cast<llvm::Instruction>(repl);
      }
    }
    return {repl, ins_begin};
  }

  if (auto *agg = llvm::dyn_cast<llvm::ConstantAggregate>(cst)) [[unlikely]] {
    llvm::Instruction *ins_begin = nullptr;
    llvm::SmallVector<llvm::Value *> repls;
    for (auto it : llvm::enumerate(agg->operands())) {
      auto *cst = llvm::cast<llvm::Constant>(it.value());
      if (auto [repl, inst] = fixup_constant(cst, ins_before); repl) {
        repls.resize(agg->getNumOperands());
        repls[it.index()] = repl;
        if (!ins_begin) {
          ins_begin = inst;
        }
      }
    }
    if (!repls.empty()) {
      // TODO: optimize so that all supported constants are in the
      // top-level constant?
      llvm::Value *repl = llvm::PoisonValue::get(cst->getType());
      for (auto it : llvm::enumerate(repls)) {
        unsigned idx = it.index();
        auto *el = it.value() ? it.value() : agg->getOperand(idx);
        repl = llvm::InsertValueInst::Create(repl, el, {idx}, "", ins_before);
      }
      return {repl, ins_begin};
    }
  }

  return {nullptr, nullptr};
}

llvm::Instruction *LLVMAdaptor::handle_inst_in_block(llvm::BasicBlock *block,
                                                     llvm::Instruction *inst) {
  llvm::Instruction *restart_from = nullptr;

  // I also don't want to handle non-constant values in GEPs in the GEP
  // handler below it's easier to just split them here
  if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(inst); gep) {
    // TODO: we probably want to optimize the case where we have a
    // getelementptr [10 x i32], ptr %3, i64 0, i64 %13 since that is
    // just one scaled add
    u8 split_indices[10];
    u8 split_indices_size = 0;
    {
      auto idx_it = gep->idx_begin(), idx_end_it = gep->idx_end();
      if (idx_it != idx_end_it) {
        auto idx = 0;
        ++idx_it; // we don't care if the first idx is non-const
        ++idx;
        for (; idx_it != idx_end_it; ++idx_it, ++idx) {
          const auto *idx_val = idx_it->get();
          if (llvm::dyn_cast<llvm::ConstantInt>(idx_val) == nullptr) {
            assert(split_indices_size < 9);
            split_indices[split_indices_size++] = idx;
          }
        }
      }
    }

    if (split_indices_size != 0) {
      auto idx_vec = tpde::util::SmallVector<llvm::Value *, 8>{};

      auto begin = gep->idx_begin();
      auto *ty = gep->getSourceElementType();
      auto *ptr_val = gep->getPointerOperand();
      auto start_idx = 0;
      auto nullC = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
      auto inbounds = gep->isInBounds();
      for (auto split_idx = 0; split_idx < split_indices_size; ++split_idx) {
        idx_vec.clear();
        auto len = static_cast<size_t>(split_indices[split_idx] - start_idx);
        assert(len > 0);
        idx_vec.resize(len + 1);
        for (auto i = 0u; i < len; ++i) {
          idx_vec[i] = (begin + start_idx + i)->get();
        }
        idx_vec[len] = nullC;

        llvm::GetElementPtrInst *newGEP;
        if (inbounds) {
          newGEP = llvm::GetElementPtrInst::CreateInBounds(
              ty, ptr_val, {idx_vec.data(), len + 1}, "", inst);
        } else {
          newGEP = llvm::GetElementPtrInst::Create(
              ty, ptr_val, {idx_vec.data(), len + 1}, "", inst);
        }

        ty = newGEP->getResultElementType();
        ptr_val = newGEP;
        start_idx = split_indices[split_idx];

        if (!restart_from) {
          restart_from = newGEP;
        }
      }

      // last doesn't need the zero constant
      {
        idx_vec.clear();
        auto len = static_cast<size_t>(gep->getNumIndices() - start_idx);
        assert(len > 0);
        idx_vec.resize(len);
        for (auto i = 0u; i < len; ++i) {
          idx_vec[i] = (begin + start_idx + i)->get();
        }

        llvm::GetElementPtrInst *new_gep;
        if (inbounds) {
          new_gep = llvm::GetElementPtrInst::CreateInBounds(
              ty, ptr_val, {idx_vec.data(), len}, "", inst);
        } else {
          new_gep = llvm::GetElementPtrInst::Create(
              ty, ptr_val, {idx_vec.data(), len}, "", inst);
        }

        inst->replaceAllUsesWith(new_gep);
        inst->eraseFromParent();

        if (!restart_from) {
          restart_from = new_gep;
        }
      }
      return restart_from;
    }
  }

  if (inst->isTerminator()) {
    // TODO: remove this hack, see compile_invoke.
    if (llvm::isa<llvm::InvokeInst>(inst)) {
      func_has_dynamic_alloca = true;
    }

    auto *ins_before = inst;
    if (block->begin().getNodePtr() != ins_before) {
      auto *prev_inst = ins_before->getPrevNonDebugInstruction();
      if (prev_inst && llvm::isa<llvm::CmpInst>(prev_inst)) {
        // make sure fusing can still happen
        ins_before = prev_inst;
      }
    }

    for (llvm::BasicBlock *succ : llvm::successors(inst->getParent())) {
      for (llvm::PHINode &phi : succ->phis()) {
        auto *val = phi.getIncomingValueForBlock(inst->getParent());
        auto *cst = llvm::dyn_cast<llvm::Constant>(val);
        if (!cst || llvm::isa<llvm::GlobalValue>(cst)) {
          continue;
        }
        auto [repl, ins_begin] = fixup_constant(cst, ins_before);
        if (repl) {
          phi.setIncomingValueForBlock(inst->getParent(), repl);
          if (!ins_begin) {
            block_constants.push_back(llvm::cast<llvm::Constant>(repl));
          } else if (!restart_from) {
            restart_from = ins_begin;
          }
        } else if (!value_lookup.contains(cst)) {
          // we insert constants inbetween block so that instructions
          // in a block are consecutive
          block_constants.push_back(cst);
        }
      }
    }

    // We might have inserted before the cmp, which would get compiled twice
    // now. Therefore, remove the assigned value number.
    if (restart_from && ins_before != inst) {
      auto ins_before_idx = inst_lookup_idx(ins_before);
      assert(values.size() == ins_before_idx + 1);
      values.resize(ins_before_idx);
#ifndef NDEBUG
      value_lookup.erase(ins_before);
#endif
    }
  }

  // Check operands for constants; PHI nodes are handled by predecessors.
  if (!llvm::isa<llvm::PHINode>(inst)) {
    for (llvm::Use &use : inst->operands()) {
      auto *cst = llvm::dyn_cast<llvm::Constant>(use.get());
      if (!cst || llvm::isa<llvm::GlobalValue>(cst)) {
        continue;
      }

      if (auto [repl, ins_begin] = fixup_constant(cst, inst); repl)
          [[unlikely]] {
        use = repl;
        if (!ins_begin) {
          block_constants.push_back(llvm::cast<llvm::Constant>(repl));
        } else if (!restart_from) {
          restart_from = ins_begin;
        }
      } else if (!value_lookup.contains(use.get())) {
        // we insert constants inbetween block so that instructions in a
        // block are consecutive
        block_constants.push_back(llvm::cast<llvm::Constant>(use.get()));
      }
    }
  }

  auto fused = false;
  if (const auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(inst)) {
    if (is_static_alloca(alloca)) {
      initial_stack_slot_indices.push_back(values.size());
      // fuse static alloca's in the initial block so we dont try
      // to dynamically allocate them
      fused = true;
    } else {
      func_has_dynamic_alloca = true;
    }
  }

  if (restart_from) {
    return restart_from;
  }

  auto val_idx = values.size();
  val_idx_for_inst(inst) = val_idx;

#ifndef NDEBUG
  assert(!value_lookup.contains(inst));
  value_lookup.insert_or_assign(inst, val_idx);
#endif
  auto [ty, complex_part_idx] = val_basic_type_uncached(inst, false);
  values.push_back(ValInfo{.val = static_cast<llvm::Value *>(inst),
                           .type = ty,
                           .fused = fused,
                           .argument = false,
                           .skip_liveness = fused,
                           .complex_part_tys_idx = complex_part_idx});
  return nullptr;
}

bool LLVMAdaptor::switch_func(const IRFuncRef function) noexcept {
  llvm::TimeTraceScope time_scope("TPDE_Prepass", function->getName());

  cur_func = function;
  func_unsupported = false;

  TPDE_LOG_DBG("Compiling func: {}",
               static_cast<std::string_view>(function->getName()));

  // assign local ids
  value_lookup.clear();
#ifndef NDEBUG
  block_lookup.clear();
#endif
  blocks.clear();
  block_succ_indices.clear();
  block_succ_ranges.clear();
  func_arg_indices.clear();
  initial_stack_slot_indices.clear();
  func_has_dynamic_alloca = false;

  // we keep globals around for all function compilation
  // and assign their value indices at the start of the compilation
  // TODO(ts): move this to start_compile?
  if (!globals_init) {
    globals_init = true;
    global_lookup.reserve(512);
    auto add_global = [&](llvm::GlobalValue *gv) {
      assert(global_lookup.find(gv) == global_lookup.end());
      global_lookup.insert_or_assign(gv, values.size());
      const auto [ty, complex_part_idx] = val_basic_type_uncached(gv, true);
      values.push_back(ValInfo{.val = gv,
                               .type = ty,
                               .fused = false,
                               .argument = false,
                               .skip_liveness = true,
                               .complex_part_tys_idx = complex_part_idx});

      if (gv->isThreadLocal()) [[unlikely]] {
        // Rewrite all accesses to thread-local variables to go through the
        // intrinsic llvm.threadlocal.address; other accesses are unsupported.
        for (llvm::Use &use : llvm::make_early_inc_range(gv->uses())) {
          auto *intrin = llvm::dyn_cast<llvm::IntrinsicInst>(use.getUser());
          if (intrin && intrin->getIntrinsicID() == llvm::Intrinsic::threadlocal_address) {
            continue;
          }

          auto *instr = llvm::dyn_cast<llvm::Instruction>(use.getUser());
          if (!instr) [[unlikely]] {
            std::string user;
            llvm::raw_string_ostream(user) << *use.getUser();
            TPDE_LOG_ERR("thread-local global with unsupported use: {}", user);
            func_unsupported = true;
            break;
          }

          llvm::IRBuilder<> irb(instr);
          use.set(irb.CreateThreadLocalAddress(gv));
        }
      }
    };
    for (llvm::GlobalVariable &gv : mod->globals()) {
      add_global(&gv);
    }
    for (llvm::GlobalAlias &ga : mod->aliases()) {
      add_global(&ga);
    }
    // Do functions last, handling of global variables/aliases might introduce
    // another intrinsic declaration for llvm.threadlocal.address.
    for (llvm::Function &fn : mod->functions()) {
      add_global(&fn);
    }
    global_idx_end = values.size();
    global_complex_part_types_end_idx = complex_part_types.size();
  } else {
    values.resize(global_idx_end);
    complex_part_types.resize(global_complex_part_types_end_idx);
  }

  // reserve a constant size and optimize for smaller functions
  value_lookup.reserve(512);

  // NB: this iterates over all basic blocks.
  const size_t block_count = function->size();
  block_idx_end = global_idx_end + block_count;
  values.resize(block_idx_end);

  const size_t arg_count = function->arg_size();
  arg_idx_end = block_idx_end + arg_count;
  for (size_t i = 0; i < arg_count; ++i) {
    llvm::Argument *arg = function->getArg(i);
    // value_lookup.insert_or_assign(arg, values.size());
    func_arg_indices.push_back(values.size());
    const auto [ty, complex_part_idx] = val_basic_type_uncached(arg, false);
    values.push_back(ValInfo{.val = static_cast<llvm::Value *>(arg),
                             .type = ty,
                             .fused = false,
                             .argument = true,
                             .skip_liveness = false,
                             .complex_part_tys_idx = complex_part_idx});
  }

  for (llvm::BasicBlock &block : *function) {
    const u32 idx_start = values.size();
    u32 phi_end = idx_start;

    for (auto it = block.begin(), end = block.end(); it != end;) {
      auto *restart_from = handle_inst_in_block(&block, &*it);
      if (restart_from) {
        it = restart_from->getIterator();
      } else {
        if (llvm::isa<llvm::PHINode>(&*it)) {
          phi_end = static_cast<u32>(values.size());
        }
        ++it;
      }
    }

    u32 idx_end = values.size();

    const auto block_idx = blocks.size();
    if (!blocks.empty()) {
      blocks.back().aux.sibling = block_idx;
    }

    blocks.push_back(BlockInfo{
        .block = &block,
        .aux = BlockAux{.idx_start = idx_start,
                        .idx_end = idx_end,
                        .idx_phi_end = phi_end,
                        .sibling = INVALID_BLOCK_REF}
    });

#ifndef NDEBUG
    block_lookup[&block] = block_idx;
#endif
    block_embedded_idx(&block) = block_idx;

    // blocks are also used as operands for branches so they need an
    // IRValueRef, too
    values[global_idx_end + block_idx] = ValInfo{
        .val = static_cast<llvm::Value *>(&block),
        .type = LLVMBasicValType::invalid,
        .fused = false,
        .argument = false,
        .skip_liveness = true,
    };

    for (auto *C : block_constants) {
      if (value_lookup.find(C) == value_lookup.end()) {
        value_lookup.insert_or_assign(C, values.size());
        const auto [ty, complex_part_idx] = val_basic_type_uncached(C, true);
        values.push_back(ValInfo{.val = static_cast<llvm::Value *>(C),
                                 .type = ty,
                                 .fused = false,
                                 .argument = false,
                                 .skip_liveness = true,
                                 .complex_part_tys_idx = complex_part_idx});
      }
    }
    block_constants.clear();
  }

  for (const auto &info : blocks) {
    const u32 start_idx = block_succ_indices.size();
    for (auto *succ : llvm::successors(info.block)) {
      // blockSuccs.push_back(blockLookup[succ]);
      block_succ_indices.push_back(block_embedded_idx(succ));
    }
    block_succ_ranges.push_back(
        std::make_pair(start_idx, block_succ_indices.size()));
  }

  return !func_unsupported;
}

void LLVMAdaptor::switch_module(llvm::Module &mod) noexcept {
  if (this->mod) {
    reset();
  }
  this->context = &mod.getContext();
  this->mod = &mod;
}

void LLVMAdaptor::reset() noexcept {
  context = nullptr;
  mod = nullptr;
  values.clear();
  global_lookup.clear();
  value_lookup.clear();
#ifndef NDEBUG
  block_lookup.clear();
#endif
  complex_part_types.clear();
  func_arg_indices.clear();
  initial_stack_slot_indices.clear();
  cur_func = nullptr;
  globals_init = false;
  global_idx_end = 0;
  global_complex_part_types_end_idx = 0;
  block_constants.clear();
  blocks.clear();
  block_succ_indices.clear();
  block_succ_ranges.clear();
}

namespace {

/// Returns (num, element-type), with num > 0 and a valid type, or (0, invalid).
[[nodiscard]] std::pair<unsigned long, LLVMBasicValType>
    llvm_ty_to_basic_ty_simple(const llvm::Type *type) noexcept {
  switch (type->getTypeID()) {
  case llvm::Type::FloatTyID: return {1, LLVMBasicValType::f32};
  case llvm::Type::DoubleTyID: return {1, LLVMBasicValType::f64};
  case llvm::Type::FP128TyID: return {1, LLVMBasicValType::v128};
  case llvm::Type::VoidTyID:
    return {1, LLVMBasicValType::none};
    // case llvm::Type::X86_MMXTyID: return {1, LLVMBasicValType::v64};

  case llvm::Type::IntegerTyID: {
    const u32 bit_width = type->getIntegerBitWidth();
    // round up to the nearest size we support
    if (bit_width <= 64) [[likely]] {
      constexpr unsigned base = static_cast<unsigned>(LLVMBasicValType::i8);
      static_assert(base + 1 == static_cast<unsigned>(LLVMBasicValType::i16));
      static_assert(base + 2 == static_cast<unsigned>(LLVMBasicValType::i32));
      static_assert(base + 3 == static_cast<unsigned>(LLVMBasicValType::i64));

      unsigned off = 31 - tpde::util::cnt_lz((bit_width - 1) >> 2 | 1);
      return {1, static_cast<LLVMBasicValType>(base + off)};
    } else if (bit_width == 128) {
      return {2, LLVMBasicValType::i128};
    }
    return {0, LLVMBasicValType::invalid};
  }
  case llvm::Type::PointerTyID: return {1, LLVMBasicValType::ptr};
  case llvm::Type::FixedVectorTyID: {
    auto *el_ty = llvm::cast<llvm::FixedVectorType>(type)->getElementType();
    auto num_elts = llvm::cast<llvm::FixedVectorType>(type)->getNumElements();
    if (num_elts == 1) {
      // Single-element vectors tend to get scalarized. On x86, however, if the
      // element type is a small integer, it gets assigned to GP regs; on
      // AArch64, it stays in a vector register.
      // TODO: handle this case.
      return {0, LLVMBasicValType::invalid};
    }

    // LLVM vectors have two different representations, the in-memory/bitcast
    // representation and the in-register representation. For types that are not
    // directly legal, these are not equivalent.
    //
    // The in-memory type is always dense, i.e. <N x iM> is like an i(N*M),
    // and only unspecified when N*M is not a multiple of 8.
    //
    // The in-register type, which is also used for passing parameters, is
    // legalized as follows (see TargetLoweringBase::getTypeConversion):
    // - Target-specific legalization rules are applied.
    //   - E.g., AArch64: widen v1i16 => v4i16 instead of scalarizing
    //     (see AArch64TargetLowering::getPreferredVectorAction)
    // - If single element, scalarize.
    // - Widen number of elements to next power of two.
    // - For integer types: increase width until legal type is found.
    //   - E.g., AArch64: v2i16 => v2i32 (ISA supports 2s) (final result)
    //   - E.g., x86-64: v2i16 unchanged (ISA supports neither v2i32, v2i64)
    // - Widen number of elements in powers of two until legal type is found.
    //   - E.g., x86-64: v2i16 => v8i16 (final result)
    // - If type still not legal, split in half and repeat.
    //
    // This also handles illegal types. E.g., v3i99 would first get widened to
    // v4i99, then (as no i99 is legal anywhere) split into v2i99, which is
    // legalized recursively; this gets split into v1i99; this gets scalarized
    // into i99; this gets promoted into i128 (next power of two); this gets
    // expanded into two i64 (as i128 is not legal for any register class).
    //
    //
    // To avoid the difficulties of legalizing all kinds of vector types, we
    // only support types that are directly legal or can be legalized by
    // just widening (e.g., v2f32 => v4f32) on x86-64 *and* AArch64 for now.
    //
    // Therefore, we support:
    // - Integer elements: v8i8/v16i8, v4i16/v8i16, v2i32/v4i32, v2i64
    //   (no widened vectors: x86-64 would widen, but AArch64 would promote)
    // - Floating-point elements: v2f32, v4f32, v2f64
    //   (single-element vectors would be scalarized; v3f32 would need 12b load)
    switch (el_ty->getTypeID()) {
    case llvm::Type::IntegerTyID: {
      unsigned el_width = el_ty->getIntegerBitWidth();
      if (el_width < 8 || el_width > 64 || (el_width & (el_width - 1))) {
        return {0, LLVMBasicValType::invalid};
      }
      if (el_width * num_elts == 64) {
        return {1, LLVMBasicValType::v64};
      } else if (el_width * num_elts == 128) {
        return {1, LLVMBasicValType::v128};
      }
      return {0, LLVMBasicValType::invalid};
    }
    case llvm::Type::FloatTyID:
      if (num_elts == 2) {
        return {1, LLVMBasicValType::v64};
      } else if (num_elts == 4) {
        return {1, LLVMBasicValType::v128};
      }
      return {0, LLVMBasicValType::invalid};
    case llvm::Type::DoubleTyID:
      if (num_elts == 2) {
        return {1, LLVMBasicValType::v128};
      }
      return {0, LLVMBasicValType::invalid};
    default: return {0, LLVMBasicValType::invalid};
    }
  }
  default: return {0, LLVMBasicValType::invalid};
  }
}

} // end anonymous namespace

std::pair<unsigned, unsigned>
    LLVMAdaptor::complex_types_append(llvm::Type *type) noexcept {
  if (auto [num, ty] = llvm_ty_to_basic_ty_simple(type);
      ty != LLVMBasicValType::invalid) {
    unsigned size = basic_ty_part_size(ty);
    unsigned align = basic_ty_part_align(ty);
    assert(num > 0);
    // TODO: support types with different part types/sizes?
    for (unsigned i = 0; i < num; i++) {
      complex_part_types.emplace_back(ty, size, i == num - 1);
    }
    return std::make_pair(num * size, align);
  }

  size_t start = complex_part_types.size();
  switch (type->getTypeID()) {
  case llvm::Type::ArrayTyID: {
    auto [sz, algn] = complex_types_append(type->getArrayElementType());
    size_t len = complex_part_types.size() - start;

    unsigned nelem = type->getArrayNumElements();
    complex_part_types.resize(start + nelem * len);
    for (unsigned i = 1; i < nelem; i++) {
      std::memcpy(&complex_part_types[start + i * len],
                  &complex_part_types[start],
                  len * sizeof(LLVMComplexPart));
    }
    if (nelem > 0) {
      complex_part_types[start].part.nest_inc++;
      complex_part_types[start + nelem * len - 1].part.nest_dec++;
    }
    return std::make_pair(nelem * sz, algn);
  }
  case llvm::Type::StructTyID: {
    unsigned size = 0;
    unsigned align = 1;
    bool packed = llvm::cast<llvm::StructType>(type)->isPacked();
    for (auto *el : llvm::cast<llvm::StructType>(type)->elements()) {
      unsigned prev = complex_part_types.size() - 1;
      auto [el_size, el_align] = complex_types_append(el);
      assert(el_size % el_align == 0 && "size must be multiple of alignment");
      if (packed) {
        el_align = 1;
      }

      unsigned old_size = size;
      size = tpde::util::align_up(size, el_align);
      if (size != old_size) {
        complex_part_types[prev].part.pad_after += size - old_size;
      }
      size += el_size;
      align = std::max(align, el_align);
    }

    size_t end = complex_part_types.size() - 1;
    unsigned old_size = size;
    size = tpde::util::align_up(size, align);
    if (size > 0) {
      complex_part_types[start].part.nest_inc++;
      complex_part_types[end].part.nest_dec++;
      complex_part_types[end].part.pad_after += size - old_size;
    }
    return std::make_pair(size, align);
  }
  default: {
    std::string type_name;
    llvm::raw_string_ostream(type_name) << *type;
    TPDE_LOG_ERR("unsupported type: {}", type_name);
    func_unsupported = true;
    return std::make_pair(0, 1);
  }
  }
}

[[nodiscard]] std::pair<LLVMBasicValType, u32>
    LLVMAdaptor::val_basic_type_uncached(const llvm::Value *val,
                                         const bool const_or_global) noexcept {
  (void)const_or_global;
  // TODO: Cache this?
  auto *type = val->getType();
  if (auto [num, ty] = llvm_ty_to_basic_ty_simple(type); num > 0) [[likely]] {
    assert(num == 1 || ty == LLVMBasicValType::i128);
    return std::make_pair(ty, ~0u);
  }

  unsigned start = complex_part_types.size();
  complex_part_types.push_back(LLVMComplexPart{}); // length
  // TODO: store size/alignment?
  complex_types_append(type);
  unsigned len = complex_part_types.size() - (start + 1);
  complex_part_types[start].num_parts = len;

  return std::make_pair(LLVMBasicValType::complex, start);
}

std::pair<unsigned, unsigned>
    LLVMAdaptor::complex_part_for_index(IRValueRef val_idx,
                                        llvm::ArrayRef<unsigned> search) {
  assert(values[val_idx].type == LLVMBasicValType::complex);
  const auto ty_idx = values[val_idx].complex_part_tys_idx;
  const LLVMComplexPart *part_descs = &complex_part_types[ty_idx + 1];
  unsigned part_count = part_descs[-1].num_parts;

  assert(search.size() > 0);

  unsigned depth = 0;
  tpde::util::SmallVector<unsigned, 16> indices;
  unsigned first_part = -1u;
  for (unsigned i = 0; i < part_count; i++) {
    indices.resize(indices.size() + part_descs[i].part.nest_inc);
    while (first_part == -1u && indices[depth] == search[depth]) {
      if (depth + 1 < search.size()) {
        depth++;
      } else {
        first_part = i;
      }
    }

    indices.resize(indices.size() - part_descs[i].part.nest_dec);
    if (part_descs[i].part.ends_value && !indices.empty()) {
      indices.back()++;
    }

    if (first_part != -1u &&
        (indices.size() <= depth || indices[depth] > search[depth])) {
      return std::make_pair(first_part, i);
    }
  }

  assert(0 && "out-of-range part index?");
  return std::make_pair(0, 0);
}

} // end namespace tpde_llvm
