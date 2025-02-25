// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <format>
#include <ranges>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "base.hpp"
#include "tpde/base.hpp"
#include "tpde/util/SmallVector.hpp"
#include "tpde/util/misc.hpp"

namespace tpde_llvm {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

// very hacky
inline u32 &val_idx_for_inst(llvm::Instruction *inst) noexcept {
  return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(inst) +
                                  offsetof(llvm::Instruction, DebugMarker) - 4);
  // static_assert(sizeof(llvm::Instruction) == 64);
}

inline u32 val_idx_for_inst(const llvm::Instruction *inst) noexcept {
  return *reinterpret_cast<const u32 *>(
      reinterpret_cast<const u8 *>(inst) +
      offsetof(llvm::Instruction, DebugMarker) - 4);
  // static_assert(sizeof(llvm::Instruction) == 64);
}

inline u32 &block_embedded_idx(llvm::BasicBlock *block) noexcept {
  return *reinterpret_cast<u32 *>(
      reinterpret_cast<u8 *>(block) +
      offsetof(llvm::BasicBlock, IsNewDbgInfoFormat) + 4);
}

inline u32 block_embedded_idx(const llvm::BasicBlock *block) noexcept {
  return block_embedded_idx(const_cast<llvm::BasicBlock *>(block));
}

#pragma GCC diagnostic pop

// the basic types we handle, the actual compiler can figure out the parts
enum class LLVMBasicValType : u8 {
  invalid,
  none,
  i1,
  i8,
  i16,
  i32,
  i64,
  ptr,
  i128,
  f32,
  f64,
  v32,
  v64,
  v128,
  v256,
  v512,
  complex,
};

union LLVMComplexPart {
  struct {
    /// Type of the part.
    LLVMBasicValType type;
    /// In-memory size in bytes, e.g. 3 for i24.
    u8 size;
    /// Padding after the part for the in-memory layout.
    u8 pad_after : 7;
    /// Whether the part begins a new LLVM value. This is not the case for,
    /// e.g., the second part of i128.
    u8 ends_value : 1;
    /// Nesting depth increase before the part.
    u8 nest_inc : 4;
    /// Nesting depth decrease after the part.
    u8 nest_dec : 4;
  } part;

  /// Number of parts following.
  u32 num_parts;

  LLVMComplexPart() : part{.type = LLVMBasicValType::invalid} {}

  LLVMComplexPart(LLVMBasicValType type, u8 size, bool ends_value = true)
      : part{.type = type,
             .size = size,
             .pad_after = 0,
             .ends_value = ends_value,
             .nest_inc = 0,
             .nest_dec = 0} {}
};

static_assert(sizeof(LLVMComplexPart) == 4);

struct LLVMAdaptor {
  using IRValueRef = const llvm::Value *;
  using IRInstRef = const llvm::Instruction *;
  using IRBlockRef = u32;
  using IRFuncRef = llvm::Function *;

  static constexpr IRValueRef INVALID_VALUE_REF = nullptr;
  static constexpr IRBlockRef INVALID_BLOCK_REF = static_cast<IRBlockRef>(~0u);
  static constexpr IRFuncRef INVALID_FUNC_REF =
      nullptr; // NOLINT(*-misplaced-const)

  llvm::LLVMContext *context = nullptr;
  llvm::Module *mod = nullptr;

  struct ValInfo {
    LLVMBasicValType type;
    bool fused;
    u32 complex_part_tys_idx;
  };

  struct BlockAux {
    u32 aux1;
    u32 aux2;
    llvm::BasicBlock::iterator phi_end;
  };

  struct BlockInfo {
    llvm::BasicBlock *block;
    BlockAux aux;
  };

  /// Value info. Values are numbered in the following order:
  /// - 0..<global_idx_end: GlobalValue
  /// - global_idx_end..<arg_idx_end: Arguments
  /// - arg_idx_end..: Instructions
  tpde::util::SmallVector<ValInfo, 128> values;
  /// Map from global value to value index. Globals are the lowest values.
  /// Keep them separate so that we don't have to repeatedly insert them for
  /// every function.
  llvm::DenseMap<const llvm::GlobalValue *, u32> global_lookup;
#ifndef NDEBUG
  llvm::DenseMap<const llvm::Value *, u32> value_lookup;
  llvm::DenseMap<const llvm::BasicBlock *, u32> block_lookup;
#endif
  tpde::util::SmallVector<LLVMComplexPart, 32> complex_part_types;

  // helpers for faster lookup
  tpde::util::SmallVector<const llvm::AllocaInst *, 16>
      initial_stack_slot_indices;

  llvm::Function *cur_func = nullptr;
  bool func_unsupported = false;
  bool globals_init = false;
  bool func_has_dynamic_alloca = false;
  // Index boundaries into values.
  u32 global_idx_end = 0;
  u32 global_complex_part_types_end_idx = 0;

  tpde::util::SmallVector<BlockInfo, 128> blocks;
  tpde::util::SmallVector<u32, 256> block_succ_indices;
  tpde::util::SmallVector<std::pair<u32, u32>, 128> block_succ_ranges;

  LLVMAdaptor() = default;

  static constexpr bool TPDE_PROVIDES_HIGHEST_VAL_IDX = true;
  static constexpr bool TPDE_LIVENESS_VISIT_ARGS = true;

  [[nodiscard]] u32 func_count() const noexcept {
    return mod->getFunctionList().size();
  }

  [[nodiscard]] auto funcs() const noexcept {
    return *mod | std::views::filter([](llvm::Function &fn) {
      return !fn.isIntrinsic();
    }) | std::views::transform([](llvm::Function &fn) { return &fn; });
  }

  [[nodiscard]] auto funcs_to_compile() const noexcept { return funcs(); }

  [[nodiscard]] static std::string_view
      func_link_name(const IRFuncRef func) noexcept {
    return func->getName();
  }

  [[nodiscard]] static bool func_extern(const IRFuncRef func) noexcept {
    return func->isDeclarationForLinker();
  }

  [[nodiscard]] static bool func_only_local(const IRFuncRef func) noexcept {
    return func->hasLocalLinkage();
  }

  [[nodiscard]] static bool
      func_has_weak_linkage(const IRFuncRef func) noexcept {
    return func->isWeakForLinker();
  }

  [[nodiscard]] bool cur_needs_unwind_info() const noexcept {
    return cur_func->needsUnwindTableEntry();
  }

  [[nodiscard]] bool cur_is_vararg() const noexcept {
    return cur_func->isVarArg();
  }

  [[nodiscard]] u32 cur_highest_val_idx() const noexcept {
    return values.size();
  }

  [[nodiscard]] IRFuncRef cur_personality_func() const noexcept {
    if (!cur_func->hasPersonalityFn()) {
      return nullptr;
    }

    return llvm::cast<llvm::Function>(cur_func->getPersonalityFn());
  }

  [[nodiscard]] auto cur_args() const noexcept {
    return cur_func->args() |
           std::views::transform([](llvm::Argument &arg) { return &arg; });
  }

  [[nodiscard]] const auto &cur_static_allocas() const noexcept {
    return initial_stack_slot_indices;
  }

  [[nodiscard]] bool cur_has_dynamic_alloca() const noexcept {
    return func_has_dynamic_alloca;
  }

  [[nodiscard]] static IRBlockRef cur_entry_block() noexcept { return 0; }

  auto cur_blocks() const noexcept {
    return std::views::iota(size_t{0}, blocks.size());
  }

  [[nodiscard]] IRBlockRef
      block_lookup_idx(const llvm::BasicBlock *block) const noexcept {
    auto idx = block_embedded_idx(block);
#ifndef NDEBUG
    auto it = block_lookup.find(block);
    assert(it != block_lookup.end() && it->second == idx);
#endif
    return idx;
  }

  [[nodiscard]] auto block_succs(const IRBlockRef block) const noexcept {
    struct BlockRange {
      const IRBlockRef *block_start, *block_end;

      [[nodiscard]] const IRBlockRef *begin() const noexcept {
        return block_start;
      }

      [[nodiscard]] const IRBlockRef *end() const noexcept { return block_end; }
    };

    auto &[start, end] = block_succ_ranges[block];
    return BlockRange{block_succ_indices.data() + start,
                      block_succ_indices.data() + end};
  }

  [[nodiscard]] auto block_insts(const IRBlockRef block) const noexcept {
    const auto &aux = blocks[block].aux;
    return std::ranges::subrange(aux.phi_end, blocks[block].block->end()) |
           std::views::transform(
               [](llvm::Instruction &instr) { return &instr; });
  }

  [[nodiscard]] auto block_phis(const IRBlockRef block) const noexcept {
    const auto &aux = blocks[block].aux;
    return std::ranges::subrange(blocks[block].block->begin(), aux.phi_end) |
           std::views::transform(
               [](llvm::Instruction &instr) { return &instr; });
  }

  [[nodiscard]] u32 block_info(const IRBlockRef block) const noexcept {
    return blocks[block].aux.aux1;
  }

  void block_set_info(const IRBlockRef block, const u32 aux) noexcept {
    blocks[block].aux.aux1 = aux;
  }

  [[nodiscard]] u32 block_info2(const IRBlockRef block) const noexcept {
    return blocks[block].aux.aux2;
  }

  void block_set_info2(const IRBlockRef block, const u32 aux) noexcept {
    blocks[block].aux.aux2 = aux;
  }

  [[nodiscard]] std::string
      block_fmt_ref(const IRBlockRef block) const noexcept {
    std::string buf;
    llvm::raw_string_ostream os{buf};
    blocks[block].block->printAsOperand(os);
    return buf;
  }

  [[nodiscard]] std::string
      value_fmt_ref(const IRValueRef value) const noexcept {
    std::string buf;
    llvm::raw_string_ostream os(buf);
    value->printAsOperand(os, /*PrintType=*/true, mod);
    return buf;
  }

  [[nodiscard]] std::string inst_fmt_ref(const IRInstRef inst) const noexcept {
    std::string buf;
    llvm::raw_string_ostream(buf) << *inst;
    return buf;
  }


  [[nodiscard]] u32 val_local_idx(const IRValueRef value) const noexcept {
    return val_lookup_idx(value);
  }

  [[nodiscard]] auto inst_operands(const IRInstRef inst) const noexcept {
    return inst->operands() | std::views::transform([](const llvm::Use &use) {
             return use.get();
           });
  }

  [[nodiscard]] auto inst_results(const IRInstRef inst) const noexcept {
    bool is_void = inst->getType()->isVoidTy();
    return std::views::single(inst) | std::views::drop(is_void ? 1 : 0);
  }

  [[nodiscard]] bool
      val_ignore_in_liveness_analysis(const IRValueRef value) const noexcept {
    return !llvm::isa<llvm::Instruction, llvm::Argument>(value);
  }

  [[nodiscard]] auto val_as_phi(const IRValueRef value) const noexcept {
    struct PHIRef {
      const llvm::PHINode *phi;
      const LLVMAdaptor *self;

      [[nodiscard]] u32 incoming_count() const noexcept {
        return phi->getNumIncomingValues();
      }

      [[nodiscard]] IRValueRef
          incoming_val_for_slot(const u32 slot) const noexcept {
        return phi->getIncomingValue(slot);
      }

      [[nodiscard]] IRBlockRef
          incoming_block_for_slot(const u32 slot) const noexcept {
        return self->block_lookup_idx(phi->getIncomingBlock(slot));
      }

      [[nodiscard]] IRValueRef
          incoming_val_for_block(const IRBlockRef block) const noexcept {
        return phi->getIncomingValueForBlock(self->blocks[block].block);
      }
    };

    return PHIRef{
        .phi = llvm::cast<llvm::PHINode>(value),
        .self = this,
    };
  }

private:
  static bool is_static_alloca(const llvm::AllocaInst *alloca) noexcept {
    // Larger allocas need dynamic stack alignment. In future, we might
    // realign the stack at the beginning, but for now, treat them like
    // dynamic allocas.
    // TODO: properly support over-aligned static allocas.
    return alloca->isStaticAlloca() && alloca->getAlign().value() <= 16;
  }

public:
  [[nodiscard]] u32 val_alloca_size(const IRValueRef value) const noexcept {
    const auto *alloca = llvm::cast<llvm::AllocaInst>(value);
    assert(alloca->isStaticAlloca());
    const u64 size = *alloca->getAllocationSize(mod->getDataLayout());
    assert(size <= std::numeric_limits<u32>::max());
    return size;
  }

  [[nodiscard]] u32 val_alloca_align(const IRValueRef value) const noexcept {
    const auto *alloca = llvm::cast<llvm::AllocaInst>(value);
    assert(alloca->isStaticAlloca());
    const u64 align = alloca->getAlign().value();
    assert(align <= 16);
    return align;
  }

  bool cur_arg_is_byval(const u32 idx) const noexcept {
    return cur_func->hasParamAttribute(idx, llvm::Attribute::AttrKind::ByVal);
  }

  u32 cur_arg_byval_align(const u32 idx) const noexcept {
    return cur_func->getParamAlign(idx)->value();
  }

  u32 cur_arg_byval_size(const u32 idx) const noexcept {
    return mod->getDataLayout().getTypeAllocSize(
        cur_func->getParamByValType(idx));
  }

  bool cur_arg_is_sret(const u32 idx) const noexcept {
    return cur_func->hasParamAttribute(idx,
                                       llvm::Attribute::AttrKind::StructRet);
  }

  static void start_compile() noexcept {}

  static void end_compile() noexcept {}

private:
  /// Replace constant expressions with instructions. Returns pair of replaced
  /// value and first inserted instruction.
  std::pair<llvm::Value *, llvm::Instruction *>
      fixup_constant(llvm::Constant *cst, llvm::Instruction *ins_before);

  /// Handle instruction during switch_func.
  /// retval = restart from instruction, or nullptr to continue
  llvm::Instruction *handle_inst_in_block(llvm::BasicBlock *block,
                                          llvm::Instruction *inst);

public:
  bool switch_func(const IRFuncRef function) noexcept;

  void switch_module(llvm::Module &mod) noexcept;

  void reset() noexcept;

  struct ValueParts {
    LLVMBasicValType bvt;
    const LLVMComplexPart *complex;

    u32 count() const {
      if (bvt != LLVMBasicValType::complex) {
        return LLVMAdaptor::basic_ty_part_count(bvt);
      }
      return complex->num_parts;
    }

    LLVMBasicValType type(u32 n) const {
      return bvt != LLVMBasicValType::complex ? bvt : complex[n + 1].part.type;
    }

    u32 size_bytes(u32 n) const {
      return LLVMAdaptor::basic_ty_part_size(type(n));
    }

    u8 reg_bank(u32 n) const { return basic_ty_part_bank(type(n)); }
  };

  ValueParts val_parts(const IRValueRef value) {
    if (llvm::isa<llvm::Constant>(value)) {
      auto [ty, ty_idx] = val_basic_type_uncached(value, false);
      if (ty == LLVMBasicValType::complex) {
        return ValueParts{ty, &complex_part_types[ty_idx]};
      }
      return ValueParts{ty, nullptr};
    }
    assert((llvm::isa<llvm::Instruction, llvm::Argument>(value)) &&
           "val_parts called on non-instruction/argument");
    size_t idx;
    if (auto *inst = llvm::dyn_cast<llvm::Instruction>(value)) {
      idx = inst_lookup_idx(inst);
    } else {
      idx = arg_lookup_idx(llvm::cast<llvm::Argument>(value));
    }
    const ValInfo &info = values[idx];
    if (info.type == LLVMBasicValType::complex) {
      unsigned ty_idx = info.complex_part_tys_idx;
      return ValueParts{info.type, &complex_part_types[ty_idx]};
    }
    return ValueParts{info.type, nullptr};
  }

  // other public stuff for the compiler impl
  u32 val_part_count(const IRValueRef idx) noexcept {
    return val_parts(idx).count();
  }

  [[nodiscard]] bool inst_fused(const IRInstRef inst) const noexcept {
    return val_info(inst).fused;
  }

  void inst_set_fused(const IRInstRef value, const bool fused) noexcept {
    values[inst_lookup_idx(value)].fused = fused;
  }

  [[nodiscard]] u32 val_lookup_idx(const llvm::Value *val) const noexcept {
    if (auto *inst = llvm::dyn_cast<llvm::Instruction>(val); inst) {
      const auto idx = inst_lookup_idx(inst);
      return idx;
    }
    if (auto *gv = llvm::dyn_cast<llvm::GlobalValue>(val)) {
      if (auto it = global_lookup.find(gv); it != global_lookup.end()) {
        return it->second;
      }
    }
    if (auto *arg = llvm::dyn_cast<llvm::Argument>(val)) {
      return arg_lookup_idx(arg);
    }
    TPDE_FATAL("unhandled value type");
  }

  const ValInfo &val_info(const llvm::Instruction *inst) const noexcept {
    return values[inst_lookup_idx(inst)];
  }

  u32 arg_lookup_idx(const llvm::Argument *arg) const noexcept {
    return global_idx_end + arg->getArgNo();
  }

  [[nodiscard]] u32
      inst_lookup_idx(const llvm::Instruction *inst) const noexcept {
    const auto idx = val_idx_for_inst(inst);
#ifndef NDEBUG
    assert(value_lookup.find(inst) != value_lookup.end() &&
           value_lookup.find(inst)->second == idx);
#endif
    return idx;
  }

  // internal helpers
  static unsigned basic_ty_part_size(const LLVMBasicValType ty) noexcept {
    switch (ty) {
      using enum LLVMBasicValType;
    case i1:
    case i8: return 1;
    case i16: return 2;
    case i32: return 4;
    case i64:
    case ptr:
    case i128: return 8;
    case f32: return 4;
    case f64: return 8;
    case v32: return 4;
    case v64: return 8;
    case v128: return 16;
    case v256:
    case v512:
    case complex:
    case invalid:
    case none:
    default: TPDE_UNREACHABLE("invalid basic type");
    }
  }

  static unsigned basic_ty_part_bank(const LLVMBasicValType ty) noexcept {
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
    case complex:
    default: TPDE_UNREACHABLE("invalid basic type");
    }
  }

private:
  static unsigned basic_ty_part_align(const LLVMBasicValType ty) noexcept {
    switch (ty) {
      using enum LLVMBasicValType;
    case i1:
    case i8: return 1;
    case i16: return 2;
    case i32: return 4;
    case i64:
    case ptr: return 8;
    case i128: return 16;
    case f32: return 4;
    case f64: return 8;
    case v32: return 4;
    case v64: return 8;
    case v128: return 16;
    case v256:
    case v512:
    case complex:
    case invalid:
    case none:
    default: TPDE_UNREACHABLE("invalid basic type");
    }
  }

  static unsigned basic_ty_part_count(const LLVMBasicValType ty) noexcept {
    switch (ty) {
      using enum LLVMBasicValType;
    case i1:
    case i8:
    case i16:
    case i32:
    case i64:
    case ptr:
    case f32:
    case f64:
    case v32:
    case v64:
    case v128: return 1;
    case i128: return 2;
    case complex:
    case v256:
    case v512:
    case none:
    case invalid:
    default: TPDE_UNREACHABLE("invalid basic type");
    }
  }

  /// Append basic types of specified type to complex_part_types. Returns the
  /// allocation size in bytes and the alignment.
  std::pair<unsigned, unsigned> complex_types_append(llvm::Type *type) noexcept;

public:
  [[nodiscard]] std::pair<LLVMBasicValType, u32>
      val_basic_type_uncached(const llvm::Value *val,
                              const bool const_or_global) noexcept;

  /// Map insertvalue/extractvalue indices to parts. Returns (first part,
  /// last part (inclusive)).
  std::pair<unsigned, unsigned>
      complex_part_for_index(IRValueRef val_idx,
                             llvm::ArrayRef<unsigned> search);
};

} // namespace tpde_llvm
