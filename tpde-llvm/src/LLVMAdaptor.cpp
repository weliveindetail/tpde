// SPDX-License-Identifier: LicenseRef-Proprietary

#include "LLVMAdaptor.hpp"

#include <format>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TimeProfiler.h>
#include <llvm/Support/raw_ostream.h>
#include <utility>

#include "base.hpp"
#include "tpde/base.hpp"
#include "tpde/util/misc.hpp"

namespace tpde_llvm {

namespace {

// Returns pair of replaced value and first inserted instruction.
std::pair<llvm::Value *, llvm::Instruction *>
    fixup_constant(llvm::Constant *cst, llvm::Instruction *ins_before) {
  if (auto *cexpr = llvm::dyn_cast<llvm::ConstantExpr>(cst); cexpr) {
    // clang creates these and we don't want to handle them so
    // we split them up into their own values
    llvm::Instruction *expr_inst = cexpr->getAsInstruction();
    expr_inst->insertBefore(ins_before);
    return {expr_inst, expr_inst};
  }

  if (auto *agg = llvm::dyn_cast<llvm::ConstantAggregate>(cst)) {
    // TODO: implement support for constant vectors
    assert(!llvm::isa<llvm::ConstantVector>(cst));

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

} // end anonymous namespace

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
      auto nullC = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0);
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
        if (cst->getType()->isVectorTy()) {
          TPDE_LOG_ERR("vector constants are unsupported");
          func_unsupported = true;
        }
        auto [repl, ins_begin] = fixup_constant(cst, ins_before);
        if (repl) {
          phi.setIncomingValueForBlock(inst->getParent(), repl);
          if (!restart_from) {
            restart_from = ins_begin;
          }
        } else if (!value_lookup.contains(cst)) {
          // we insert constants inbetween block so that instructions
          // in a block are consecutive
          block_constants.push_back(cst);
        }
      }
    }
  }

  // Check operands for constants; PHI nodes are handled by predecessors.
  if (!llvm::isa<llvm::PHINode>(inst)) {
    for (auto it : llvm::enumerate(inst->operands())) {
      auto *cst = llvm::dyn_cast<llvm::Constant>(it.value());
      if (!cst || llvm::isa<llvm::GlobalValue>(cst)) {
        continue;
      }

      if (cst->getType()->isVectorTy()) {
        TPDE_LOG_ERR("vector constants are unsupported");
        func_unsupported = true;
      }
      if (auto [repl, ins_begin] = fixup_constant(cst, inst); repl) {
        inst->setOperand(it.index(), repl);
        if (!restart_from) {
          restart_from = ins_begin;
        }
      } else if (!value_lookup.contains(cst)) {
        // we insert constants inbetween block so that instructions in a
        // block are consecutive
        block_constants.push_back(cst);
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
  value_lookup.insert_or_assign(inst, val_idx);
#endif
  auto [ty, complex_part_idx] = val_basic_type_uncached(inst, false);
  values.push_back(ValInfo{.val = static_cast<llvm::Value *>(inst),
                           .type = ty,
                           .fused = fused,
                           .argument = false,
                           .complex_part_tys_idx = complex_part_idx});
  return nullptr;
}

bool LLVMAdaptor::switch_func(const IRFuncRef function) noexcept {
  cur_func = function;
  func_unsupported = false;

  TPDE_LOG_DBG("Compiling func: {}",
               static_cast<std::string_view>(function->getName()));

  const bool profile_time = llvm::timeTraceProfilerEnabled();
  llvm::TimeTraceProfilerEntry *time_entry;
  if (profile_time) {
    time_entry = llvm::timeTraceProfilerBegin("TPDE_Prepass", "");
  }


  // assign local ids
  value_lookup.clear();
  block_lookup.clear();
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
                               .complex_part_tys_idx = complex_part_idx});
    };
    for (llvm::Function &fn : mod.functions()) {
      add_global(&fn);
    }
    for (llvm::GlobalVariable &gv : mod.globals()) {
      add_global(&gv);
    }
    for (llvm::GlobalAlias &ga : mod.aliases()) {
      add_global(&ga);
    }
    global_idx_end = values.size();
    global_complex_part_types_end_idx = complex_part_types.size();
  } else {
    values.resize(global_idx_end);
    complex_part_types.resize(global_complex_part_types_end_idx);
  }

  // reserve a constant size and optimize for smaller functions
  value_lookup.reserve(512);
  block_lookup.reserve(128);

  const size_t arg_count = function->arg_size();
  for (size_t i = 0; i < arg_count; ++i) {
    llvm::Argument *arg = function->getArg(i);
    value_lookup.insert_or_assign(arg, values.size());
    func_arg_indices.push_back(values.size());
    const auto [ty, complex_part_idx] = val_basic_type_uncached(arg, false);
    values.push_back(ValInfo{.val = static_cast<llvm::Value *>(arg),
                             .type = ty,
                             .fused = false,
                             .argument = true,
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

    block_lookup[&block] = block_idx;
    block_embedded_idx(&block) = block_idx;

    // blocks are also used as operands for branches so they need an
    // IRValueRef, too
    value_lookup.insert_or_assign(&block, values.size());
    values.push_back(ValInfo{.val = static_cast<llvm::Value *>(&block),
                             .type = LLVMBasicValType::invalid,
                             .fused = false,
                             .argument = false});

    for (auto *C : block_constants) {
      if (value_lookup.find(C) == value_lookup.end()) {
        value_lookup.insert_or_assign(C, values.size());
        const auto [ty, complex_part_idx] = val_basic_type_uncached(C, true);
        values.push_back(ValInfo{.val = static_cast<llvm::Value *>(C),
                                 .type = ty,
                                 .fused = false,
                                 .argument = false,
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

  if (profile_time) {
    llvm::timeTraceProfilerEnd(time_entry);
  }

  return !func_unsupported;
}

void LLVMAdaptor::reset() noexcept {
  values.clear();
  global_lookup.clear();
  value_lookup.clear();
  block_lookup.clear();
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

[[nodiscard]] LLVMBasicValType
    llvm_ty_to_basic_ty_simple(const llvm::Type *type,
                               const llvm::Type::TypeID id) noexcept {
  switch (id) {
  case llvm::Type::FloatTyID: return LLVMBasicValType::f32;
  case llvm::Type::DoubleTyID: return LLVMBasicValType::f64;
  case llvm::Type::FP128TyID: return LLVMBasicValType::v128;
  case llvm::Type::VoidTyID:
    return LLVMBasicValType::none;
    // case llvm::Type::X86_MMXTyID: return LLVMBasicValType::v64;

  case llvm::Type::IntegerTyID: {
    auto bit_width = type->getIntegerBitWidth();
    // round up to the nearest size we support
    if (bit_width <= 8) {
      return LLVMBasicValType::i8;
    }
    if (bit_width <= 16) {
      return LLVMBasicValType::i16;
    }
    if (bit_width <= 32) {
      return LLVMBasicValType::i32;
    }
    if (bit_width <= 64) {
      return LLVMBasicValType::i64;
    }
    if (bit_width == 128) {
      return LLVMBasicValType::i128;
    }
    return LLVMBasicValType::invalid;
  }
  case llvm::Type::PointerTyID: return LLVMBasicValType::ptr;
  case llvm::Type::FixedVectorTyID: {
    const auto bit_width = type->getPrimitiveSizeInBits().getFixedValue();
    switch (bit_width) {
    case 32: return LLVMBasicValType::v32;
    case 64: return LLVMBasicValType::v64;
    case 128: return LLVMBasicValType::v128;
    // Supporting vectors needs more thought in general.
    // case 256: return LLVMBasicValType::v256;
    // case 512: return LLVMBasicValType::v512;
    default: return LLVMBasicValType::invalid;
    }
  }
  default: return LLVMBasicValType::invalid;
  }
}

} // end anonymous namespace

std::pair<unsigned, unsigned>
    LLVMAdaptor::complex_types_append(llvm::Type *type) noexcept {
  const auto type_id = type->getTypeID();
  if (auto ty = llvm_ty_to_basic_ty_simple(type, type_id);
      ty != LLVMBasicValType::invalid) {
    unsigned size = basic_ty_part_size(ty);
    unsigned align = basic_ty_part_align(ty);
    unsigned part_count = basic_ty_part_count(ty);
    assert(part_count > 0);
    // TODO: support types with different part types/sizes?
    for (unsigned i = 0; i < part_count; i++) {
      complex_part_types.emplace_back(ty, size, i == part_count - 1);
    }
    return std::make_pair(part_count * size, align);
  }

  size_t start = complex_part_types.size();
  switch (type_id) {
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
  auto type_id = type->getTypeID();
  if (auto ty = llvm_ty_to_basic_ty_simple(type, type_id);
      ty != LLVMBasicValType::invalid) {
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
    LLVMAdaptor::complex_part_for_index(IRValueRef val_idx, unsigned index) {
  assert(values[val_idx].type == LLVMBasicValType::complex);
  const auto ty_idx = values[val_idx].complex_part_tys_idx;
  const LLVMComplexPart *part_descs = &complex_part_types[ty_idx + 1];
  unsigned part_count = part_descs[-1].num_parts;

  tpde::util::SmallVector<unsigned, 16> indices;
  unsigned first_part = -1u;
  for (unsigned i = 0; i < part_count; i++) {
    indices.resize(indices.size() + part_descs[i].part.nest_inc);
    // TODO: support more than one index operand. Comparing this index
    // vector with the operands should be sufficient.
    if (indices[0] == index && first_part == -1u) {
      first_part = i;
    }

    indices.resize(indices.size() - part_descs[i].part.nest_dec);
    if (part_descs[i].part.ends_value && !indices.empty()) {
      indices.back()++;
    }

    if (indices.size() == 0 || indices[0] > index) {
      return std::make_pair(first_part, i);
    }
  }

  assert(0 && "out-of-range part index?");
  return std::make_pair(0, 0);
}

} // end namespace tpde_llvm
