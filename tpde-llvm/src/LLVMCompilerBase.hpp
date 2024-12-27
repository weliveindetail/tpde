// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/TimeProfiler.h>
#include <llvm/Support/raw_ostream.h>

#include "tpde/CompilerBase.hpp"
#include "tpde/base.hpp"
#include "tpde/util/misc.hpp"

#include "LLVMAdaptor.hpp"

namespace tpde_llvm {

template <typename Adaptor, typename Derived, typename Config>
struct LLVMCompilerBase : tpde::CompilerBase<LLVMAdaptor, Derived, Config> {
  // TODO
  using Base = tpde::CompilerBase<LLVMAdaptor, Derived, Config>;

  using IRValueRef = typename Base::IRValueRef;
  using IRBlockRef = typename Base::IRBlockRef;
  using IRFuncRef = typename Base::IRFuncRef;
  using ScratchReg = typename Base::ScratchReg;
  using ValuePartRef = typename Base::ValuePartRef;
  using AssignmentPartRef = typename Base::AssignmentPartRef;
  using GenericValuePart = typename Base::GenericValuePart;
  using ValLocalIdx = typename Base::ValLocalIdx;
  using InstRange = typename Base::InstRange;

  using Assembler = typename Base::Assembler;
  using SymRef = typename Assembler::SymRef;

  using AsmReg = typename Base::AsmReg;

  struct RelocInfo {
    enum RELOC_TYPE : uint8_t {
      RELOC_ABS,
      RELOC_PC32,
    };

    uint32_t off;
    int32_t addend;
    SymRef sym;
    RELOC_TYPE type = RELOC_ABS;
  };

  struct ResolvedGEP {
    ValuePartRef base;
    std::optional<ValuePartRef> index;
    u64 scale;
    i64 displacement;
  };

  struct VarRefInfo {
    IRValueRef val;
    bool alloca;
    bool local;
    u32 alloca_frame_off;
  };

  enum class IntBinaryOp {
    add,
    sub,
    mul,
    udiv,
    sdiv,
    urem,
    srem,
    land,
    lor,
    lxor,
    shl,
    shr,
    ashr,
  };

  enum class FloatBinaryOp {
    add,
    sub,
    mul,
    div,
    rem
  };

  enum class OverflowOp {
    uadd,
    sadd,
    usub,
    ssub,
    umul,
    smul
  };

  // <is_func, idx>
  tsl::hopscotch_map<const llvm::GlobalValue *, std::pair<bool, u32>>
      global_sym_lookup{};
  // TODO(ts): SmallVector?
  std::vector<SymRef> global_syms;

  tpde::util::SmallVector<VarRefInfo, 16> variable_refs{};
  tpde::util::SmallVector<std::pair<IRValueRef, SymRef>, 16> type_info_syms;

  SymRef sym_fmod = Assembler::INVALID_SYM_REF;
  SymRef sym_fmodf = Assembler::INVALID_SYM_REF;
  SymRef sym_floorf = Assembler::INVALID_SYM_REF;
  SymRef sym_floor = Assembler::INVALID_SYM_REF;
  SymRef sym_ceilf = Assembler::INVALID_SYM_REF;
  SymRef sym_ceil = Assembler::INVALID_SYM_REF;
  SymRef sym_roundf = Assembler::INVALID_SYM_REF;
  SymRef sym_round = Assembler::INVALID_SYM_REF;
  SymRef sym_memcpy = Assembler::INVALID_SYM_REF;
  SymRef sym_memset = Assembler::INVALID_SYM_REF;
  SymRef sym_memmove = Assembler::INVALID_SYM_REF;
  SymRef sym_resume = Assembler::INVALID_SYM_REF;
  SymRef sym_powisf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_powidf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_trunc = Assembler::INVALID_SYM_REF;
  SymRef sym_truncf = Assembler::INVALID_SYM_REF;
  SymRef sym_trunctfsf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_trunctfdf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_extendsftf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_extenddftf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_eqtf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_netf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_gttf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_getf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_lttf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_letf2 = Assembler::INVALID_SYM_REF;
  SymRef sym_unordtf2 = Assembler::INVALID_SYM_REF;

  llvm::TimeTraceProfilerEntry *time_entry;

  LLVMCompilerBase(LLVMAdaptor *adaptor, const bool generate_obj)
      : Base{adaptor, generate_obj} {
    static_assert(tpde::Compiler<Derived, Config>);
    static_assert(std::is_same_v<Adaptor, LLVMAdaptor>);
  }

  Derived *derived() noexcept { return static_cast<Derived *>(this); }

  const Derived *derived() const noexcept {
    return static_cast<Derived *>(this);
  }

  // TODO(ts): check if it helps to check this
  static bool cur_func_may_emit_calls() noexcept { return true; }

  static bool try_force_fixed_assignment(IRValueRef) noexcept { return false; }

  void analysis_start() noexcept;
  void analysis_end() noexcept;

  std::optional<ValuePartRef> val_ref_special(ValLocalIdx local_idx,
                                              u32 part) noexcept;

  void define_func_idx(IRFuncRef func, const u32 idx) noexcept;
  bool hook_post_func_sym_init() noexcept;
  bool global_init_to_data(const llvm::Value *reloc_base,
                           tpde::util::SmallVector<u8, 64> &data,
                           tpde::util::SmallVector<RelocInfo, 8> &relocs,
                           const llvm::DataLayout &layout,
                           const llvm::Constant *constant,
                           u32 off) noexcept;
  bool global_const_expr_to_data(const llvm::Value *reloc_base,
                                 tpde::util::SmallVector<u8, 64> &data,
                                 tpde::util::SmallVector<RelocInfo, 8> &relocs,
                                 const llvm::DataLayout &layout,
                                 llvm::Instruction *expr,
                                 u32 off) noexcept;

  IRValueRef llvm_val_idx(const llvm::Value *) const noexcept;
  IRValueRef llvm_val_idx(const llvm::Instruction *) const noexcept;

  SymRef get_or_create_sym_ref(SymRef &sym,
                               std::string_view name,
                               bool local = false,
                               bool weak = false) noexcept;
  SymRef global_sym(const llvm::GlobalValue *global) const noexcept;

  void setup_var_ref_assignments() noexcept;

  bool compile();

  bool compile_inst(IRValueRef, InstRange) noexcept;

  bool compile_ret(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_load_generic(IRValueRef,
                            llvm::LoadInst *,
                            GenericValuePart &&) noexcept;
  bool compile_load(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_store_generic(llvm::StoreInst *, GenericValuePart &&) noexcept;
  bool compile_store(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_int_binary_op(IRValueRef,
                             llvm::Instruction *,
                             IntBinaryOp op) noexcept;
  bool compile_float_binary_op(IRValueRef,
                               llvm::Instruction *,
                               FloatBinaryOp op) noexcept;
  bool compile_fneg(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_float_ext_trunc(IRValueRef, llvm::Instruction *) noexcept;
  bool
      compile_float_to_int(IRValueRef, llvm::Instruction *, bool sign) noexcept;
  bool
      compile_int_to_float(IRValueRef, llvm::Instruction *, bool sign) noexcept;
  bool compile_int_trunc(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_int_ext(IRValueRef, llvm::Instruction *, bool sign) noexcept;
  bool compile_ptr_to_int(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_int_to_ptr(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_bitcast(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_extract_value(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_insert_value(IRValueRef, llvm::Instruction *) noexcept;

  void extract_element(IRValueRef vec,
                       unsigned idx,
                       LLVMBasicValType ty,
                       ScratchReg &out) noexcept;
  void insert_element(IRValueRef vec,
                      unsigned idx,
                      LLVMBasicValType ty,
                      GenericValuePart el) noexcept;
  bool compile_extract_element(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_insert_element(IRValueRef, llvm::Instruction *) noexcept;

  bool compile_cmpxchg(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_phi(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_freeze(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_call(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_select(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_gep(IRValueRef, llvm::Instruction *, InstRange) noexcept;
  bool compile_fcmp(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_switch(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_invoke(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_landing_pad(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_resume(IRValueRef, llvm::Instruction *) noexcept;
  SymRef lookup_type_info_sym(IRValueRef value) noexcept;
  bool compile_intrin(IRValueRef,
                      llvm::Instruction *,
                      llvm::Function *) noexcept;
  bool compile_is_fpclass(IRValueRef, llvm::Instruction *) noexcept;
  bool compile_overflow_intrin(IRValueRef,
                               llvm::Instruction *,
                               OverflowOp) noexcept;
  bool compile_saturating_intrin(IRValueRef,
                                 llvm::Instruction *,
                                 OverflowOp) noexcept;

  bool compile_unreachable(IRValueRef, llvm::Instruction *) noexcept {
    return false;
  }

  bool compile_alloca(IRValueRef, llvm::Instruction *) noexcept {
    return false;
  }

  bool compile_br(IRValueRef, llvm::Instruction *) noexcept { return false; }

  bool compile_call_inner(IRValueRef,
                          llvm::CallInst *,
                          std::variant<SymRef, ValuePartRef> &,
                          bool) noexcept {
    return false;
  }

  bool compile_icmp(IRValueRef, llvm::Instruction *, InstRange) noexcept {
    return false;
  }

  void create_helper_call(std::span<IRValueRef> args,
                          std::span<ValuePartRef> results,
                          SymRef sym) {
    (void)args;
    (void)results;
    (void)sym;
    assert(0);
  }

  bool handle_intrin(IRValueRef,
                     llvm::Instruction *,
                     llvm::Function *) noexcept {
    return false;
  }
};

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::analysis_start() noexcept {
  if (llvm::timeTraceProfilerEnabled()) {
    time_entry = llvm::timeTraceProfilerBegin("TPDE_Analysis", "");
  }
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::analysis_end() noexcept {
  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
    time_entry = llvm::timeTraceProfilerBegin("TPDE_CodeGen", "");
  }
}

template <typename Adaptor, typename Derived, typename Config>
std::optional<typename LLVMCompilerBase<Adaptor, Derived, Config>::ValuePartRef>
    LLVMCompilerBase<Adaptor, Derived, Config>::val_ref_special(
        ValLocalIdx local_idx, u32 part) noexcept {
  auto val_idx = static_cast<LLVMAdaptor::IRValueRef>(local_idx);
  auto *val = this->adaptor->values[val_idx].val;
  auto *const_val = llvm::dyn_cast<llvm::Constant>(val);
  if (!const_val) {
    return std::nullopt;
  }

  auto ty = this->adaptor->values[val_idx].type;
  unsigned sub_part = part;

  if (const_val && ty == LLVMBasicValType::complex) {
    unsigned ty_idx = this->adaptor->values[val_idx].complex_part_tys_idx;
    LLVMComplexPart *part_descs =
        &this->adaptor->complex_part_types[ty_idx + 1];

    // Iterate over complex data type to find the struct/array indices that
    // belong to part.
    tpde::util::SmallVector<unsigned, 16> indices;
    for (unsigned i = 0; i < part; i++) {
      indices.resize(indices.size() + part_descs[i].part.nest_inc -
                     part_descs[i].part.nest_dec);
      if (part_descs[i].part.ends_value) {
        sub_part = 0;
        if (!indices.empty()) {
          indices.back()++;
        }
      } else {
        sub_part++;
      }
    }
    indices.resize(indices.size() + part_descs[part].part.nest_inc);

    for (unsigned idx : indices) {
      if (!const_val) {
        break;
      }
      if (auto *cda = llvm::dyn_cast<llvm::ConstantDataArray>(const_val)) {
        const_val = cda->getElementAsConstant(idx);
        break;
      }
      auto *agg = llvm::dyn_cast<llvm::ConstantAggregate>(const_val);
      if (!agg) {
        break;
      }
      const_val = llvm::dyn_cast<llvm::Constant>(agg->getOperand(idx));
    }
    if (!const_val) {
      return {};
    }

    ty = part_descs[part].part.type;
  }

  // At this point, ty is the basic type of the element and sub_part the part
  // inside the basic type.

  if (llvm::isa<llvm::GlobalValue>(const_val)) {
    assert(ty == LLVMBasicValType::ptr && sub_part == 0);
    u32 global_idx = this->adaptor->val_lookup_idx(const_val);
    auto local_idx =
        static_cast<ValLocalIdx>(this->adaptor->val_local_idx(global_idx));
    if (!this->val_assignment(local_idx)) {
      auto *assignment = this->allocate_assignment(1);
      assignment->initialize(global_idx,
                             Config::PLATFORM_POINTER_SIZE,
                             0,
                             Config::PLATFORM_POINTER_SIZE);
      this->assignments.value_ptrs[u32(local_idx)] = assignment;

      auto ap = AssignmentPartRef{assignment, 0};
      ap.reset();
      ap.set_bank(Config::GP_BANK);
      ap.set_variable_ref(true);
      ap.set_part_size(Config::PLATFORM_POINTER_SIZE);
    }
    return ValuePartRef{this, local_idx, 0};
  }

  if (llvm::isa<llvm::PoisonValue>(const_val) ||
      llvm::isa<llvm::UndefValue>(const_val) ||
      llvm::isa<llvm::ConstantPointerNull>(const_val) ||
      llvm::isa<llvm::ConstantAggregateZero>(const_val)) {
    u32 size = this->adaptor->val_part_size(val_idx, part);
    u32 bank = derived()->val_part_bank(val_idx, part);
    return ValuePartRef(0, bank, size);
  }

  if (auto *cdv = llvm::dyn_cast<llvm::ConstantDataVector>(const_val)) {
    assert(part == 0 && "multi-part vector constants not implemented");
    llvm::StringRef data = cdv->getRawDataValues();
    std::span<const u8> span{reinterpret_cast<const u8 *>(data.data()),
                             data.size()};
    return ValuePartRef(span, Config::FP_BANK);
  }

  if (llvm::isa<llvm::ConstantVector>(const_val)) {
    // TODO(ts): check how to handle this
    assert(0);
    exit(1);
  }

  if (const auto *const_int = llvm::dyn_cast<llvm::ConstantInt>(const_val);
      const_int != nullptr) {
    switch (ty) {
      using enum LLVMBasicValType;
    case i1:
    case i8:
      return ValuePartRef(
          static_cast<u8>(const_int->getZExtValue()), Config::GP_BANK, 1);
    case i16:
      return ValuePartRef(
          static_cast<u16>(const_int->getZExtValue()), Config::GP_BANK, 2);
    case i32:
      return ValuePartRef(
          static_cast<u32>(const_int->getZExtValue()), Config::GP_BANK, 4);
    case i64:
    case ptr:
      return ValuePartRef(const_int->getZExtValue(), Config::GP_BANK, 8);
    case i128:
      return ValuePartRef(
          const_int->getValue().extractBitsAsZExtValue(64, 64 * sub_part),
          Config::GP_BANK,
          8);
    default: assert(0); exit(1);
    }
  }

  if (const auto *const_fp = llvm::dyn_cast<llvm::ConstantFP>(const_val);
      const_fp != nullptr) {
    switch (ty) {
      using enum LLVMBasicValType;
    case f32:
    case v32:
      return ValuePartRef(
          static_cast<u32>(
              const_fp->getValue().bitcastToAPInt().getZExtValue()),
          Config::FP_BANK,
          4);
    case f64:
    case v64:
      return ValuePartRef(const_fp->getValue().bitcastToAPInt().getZExtValue(),
                          Config::FP_BANK,
                          8);
    case v128: {
      llvm::APInt data = const_fp->getValue().bitcastToAPInt();
      auto raw_data = reinterpret_cast<const u8 *>(data.getRawData());
      auto num_bytes = sizeof(uint64_t) * data.getNumWords();
      return ValuePartRef({raw_data, num_bytes}, Config::FP_BANK);
    }
      // TODO(ts): support the rest
    default: assert(0); exit(1);
    }
  }

  std::string const_str;
  llvm::raw_string_ostream(const_str) << *const_val;
  TPDE_LOG_ERR("Encountered unknown constant type: {}", const_str);
  assert(0);
  exit(1);
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::define_func_idx(
    IRFuncRef func, const u32 idx) noexcept {
  global_sym_lookup[func] = std::make_pair(true, idx);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::
    hook_post_func_sym_init() noexcept {
  if (llvm::timeTraceProfilerEnabled()) {
    time_entry = llvm::timeTraceProfilerBegin("TPDE_GlobalGen", "");
  }

  // create global symbols and their definitions
  const auto &llvm_mod = this->adaptor->mod;
  auto &data_layout = llvm_mod.getDataLayout();

  global_sym_lookup.reserve(2 * llvm_mod.global_size());

  // create the symbols first so that later relocations don't try to look up
  // non-existant symbols
  for (auto it = llvm_mod.global_begin(); it != llvm_mod.global_end(); ++it) {
    const llvm::GlobalVariable *gv = &*it;
    if (gv->hasAppendingLinkage()) [[unlikely]] {
      if (gv->getName() != "llvm.global_ctors" &&
          gv->getName() != "llvm.global_dtors" &&
          gv->getName() != "llvm.used" &&
          gv->getName() != "llvm.compiler.used") {
        TPDE_LOG_ERR("Unknown global with appending linkage: {}\n",
                     static_cast<std::string_view>(gv->getName()));
        assert(0);
        return false;
      }
      continue;
    }

    // TODO(ts): we ignore weak linkage here, should emit a weak symbol for
    // it in the data section and place an undef symbol in the symbol
    // lookup
    bool local = gv->hasLocalLinkage();
    bool weak = gv->isWeakForLinker();
    if (!gv->isDeclarationForLinker()) {
      auto sym = this->assembler.sym_predef_data(gv->getName(), local, weak);

      const auto idx = global_syms.size();
      global_syms.push_back(sym);
      global_sym_lookup.insert_or_assign(gv, std::make_pair(false, idx));
    } else {
      // TODO(ts): should we use getValueName here?
      auto sym = this->assembler.sym_add_undef(gv->getName(), local, weak);
      const auto idx = global_syms.size();
      global_syms.push_back(sym);
      global_sym_lookup.insert_or_assign(gv, std::make_pair(false, idx));
    }
  }

  for (auto it = llvm_mod.alias_begin(); it != llvm_mod.alias_end(); ++it) {
    const llvm::GlobalAlias *ga = &*it;
    bool local = ga->hasLocalLinkage();
    bool weak = ga->isWeakForLinker();
    const auto sym = this->assembler.sym_add_undef(ga->getName(), local, weak);
    const auto idx = global_syms.size();
    global_syms.push_back(sym);
    global_sym_lookup.insert_or_assign(ga, std::make_pair(false, idx));
  }

  if (!llvm_mod.ifunc_empty()) {
    TPDE_LOG_ERR("ifuncs are not supported");
    return false;
  }

  // since the adaptor exposes all functions in the module to TPDE,
  // all function symbols are already added

  // now we can initialize the global data
  tpde::util::SmallVector<u8, 64> data;
  tpde::util::SmallVector<RelocInfo, 8> relocs;
  for (auto it = llvm_mod.global_begin(); it != llvm_mod.global_end(); ++it) {
    auto *gv = &*it;
    if (gv->isDeclarationForLinker()) {
      continue;
    }

    auto *init = gv->getInitializer();
    if (gv->hasAppendingLinkage()) [[unlikely]] {
      // TODO: for non-aliases in llvm.used, set SHF_GNU_RETAIN to prevent
      // linker section GC from removing the entire section.
      if (gv->getName() == "llvm.used" ||
          gv->getName() == "llvm.compiler.used") {
        continue;
      }

      tpde::util::SmallVector<std::pair<SymRef, u32>, 16> functions;
      assert(gv->getName() == "llvm.global_ctors" ||
             gv->getName() == "llvm.global_dtors");
      if (llvm::isa<llvm::ConstantAggregateZero>(init)) {
        continue;
      }

      auto *array = llvm::dyn_cast<llvm::ConstantArray>(init);
      assert(array);

      u32 max = array->getNumOperands();
      // see
      // https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable
      for (auto i = 0u; i < max; ++i) {
        auto *entry = array->getOperand(i);
        auto *prio = llvm::dyn_cast<llvm::ConstantInt>(
            entry->getAggregateElement(static_cast<unsigned>(0)));
        assert(prio);
        auto *ptr =
            llvm::dyn_cast<llvm::GlobalValue>(entry->getAggregateElement(1));
        assert(ptr);
        // we should not need to care about the third element
        assert(global_sym_lookup.contains(ptr));
        functions.emplace_back(global_sym(ptr), prio->getZExtValue());
      }

      const auto is_ctor = (gv->getName() == "llvm.global_ctors");
      if (is_ctor) {
        std::sort(functions.begin(), functions.end(), [](auto &lhs, auto &rhs) {
          return lhs.second < rhs.second;
        });
      } else {
        std::sort(functions.begin(), functions.end(), [](auto &lhs, auto &rhs) {
          return lhs.second > rhs.second;
        });
      }

      // TODO(ts): this hardcodes the ELF assembler
      auto secref = this->assembler.get_structor_section(is_ctor);
      auto &sec = this->assembler.get_section(secref);
      u32 off = sec.data.size();
      sec.data.resize(sec.data.size() + functions.size() * sizeof(u64));

      for (auto i = 0u; i < functions.size(); ++i) {
        this->assembler.reloc_abs(
            secref, functions[i].first, off + i * sizeof(u64), 0);
      }
      continue;
    }

    // check if the data value is a zero aggregate and put into bss if that
    // is the case
    if (llvm::isa<llvm::ConstantAggregateZero>(init)) {
      auto sym = global_sym(gv);
      this->assembler.sym_def_predef_bss(
          sym,
          data_layout.getTypeAllocSize(init->getType()),
          gv->getAlign().valueOrOne().value());
      continue;
    }

    data.clear();
    relocs.clear();

    data.resize(data_layout.getTypeAllocSize(init->getType()));
    if (!global_init_to_data(gv, data, relocs, data_layout, init, 0)) {
      return false;
    }

    u32 off;
    auto read_only = gv->isConstant();
    auto sec = this->assembler.get_data_section(read_only, !relocs.empty());
    auto sym = global_sym(gv);
    this->assembler.sym_def_predef_data(
        sec, sym, data, gv->getAlign().valueOrOne().value(), &off);
    for (auto &[inner_off, addend, target, type] : relocs) {
      if (type == RelocInfo::RELOC_ABS) {
        this->assembler.reloc_abs(sec, target, off + inner_off, addend);
      } else {
        assert(type == RelocInfo::RELOC_PC32);
        this->assembler.reloc_pc32(sec, target, off + inner_off, addend);
      }
    }
  }

  if (llvm::timeTraceProfilerEnabled()) {
    llvm::timeTraceProfilerEnd(time_entry);
  }
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::global_init_to_data(
    const llvm::Value *reloc_base,
    tpde::util::SmallVector<u8, 64> &data,
    tpde::util::SmallVector<RelocInfo, 8> &relocs,
    const llvm::DataLayout &layout,
    const llvm::Constant *constant,
    u32 off) noexcept {
  const auto alloc_size = layout.getTypeAllocSize(constant->getType());
  assert(off + alloc_size <= data.size());
  // can't do this since for floats -0 is special
  // if (constant->isZeroValue()) {
  //    return true;
  //}

  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(constant); CI) {
    // TODO: endianess?
    llvm::StoreIntToMemory(CI->getValue(), data.data() + off, alloc_size);
    return true;
  }
  if (auto *CF = llvm::dyn_cast<llvm::ConstantFP>(constant); CF) {
    // TODO: endianess?
    llvm::StoreIntToMemory(
        CF->getValue().bitcastToAPInt(), data.data() + off, alloc_size);
    return true;
  }
  if (auto *CDS = llvm::dyn_cast<llvm::ConstantDataSequential>(constant); CDS) {
    auto d = CDS->getRawDataValues();
    assert(d.size() <= alloc_size);
    std::copy(d.bytes_begin(), d.bytes_end(), data.begin() + off);
    return true;
  }
  if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(constant); CA) {
    const auto num_elements = CA->getType()->getNumElements();
    const auto element_size =
        layout.getTypeAllocSize(CA->getType()->getElementType());
    bool success = true;
    for (auto i = 0u; i < num_elements; ++i) {
      success &= global_init_to_data(reloc_base,
                                     data,
                                     relocs,
                                     layout,
                                     CA->getAggregateElement(i),
                                     off + i * element_size);
    }
    return success;
  }
  if (auto *CA = llvm::dyn_cast<llvm::ConstantAggregate>(constant); CA) {
    const auto num_elements = CA->getType()->getStructNumElements();
    auto &ctx = constant->getContext();

    auto *ty = CA->getType();
    auto c0 = llvm::ConstantInt::get(ctx, llvm::APInt(32, 0, false));

    for (auto i = 0u; i < num_elements; ++i) {
      auto idx = llvm::ConstantInt::get(ctx, llvm::APInt(32, (u64)i, false));
      auto agg_off = layout.getIndexedOffsetInType(ty, {c0, idx});
      global_init_to_data(reloc_base,
                          data,
                          relocs,
                          layout,
                          CA->getAggregateElement(i),
                          off + agg_off);
    }
    return true;
  }
  if (auto *ud = llvm::dyn_cast<llvm::UndefValue>(constant); ud) {
    // just leave it at 0
    return true;
  }
  if (auto *zero = llvm::dyn_cast<llvm::ConstantAggregateZero>(constant);
      zero) {
    // leave at 0
    return true;
  }
  if (auto *null = llvm::dyn_cast<llvm::ConstantPointerNull>(constant); null) {
    // leave at 0
    return true;
  }
  if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(constant); GV) {
    assert(alloc_size == 8);
    const auto sym = global_sym(GV);
    assert(sym != Assembler::INVALID_SYM_REF);
    relocs.push_back({off, 0, sym});
    return true;
  }
  if (auto *GA = llvm::dyn_cast<llvm::GlobalAlias>(constant); GA) {
    assert(alloc_size == 8);
    const auto sym = global_sym(GA);
    assert(sym != Assembler::INVALID_SYM_REF);
    relocs.push_back({off, 0, sym});
    return true;
  }
  if (auto *FN = llvm::dyn_cast<llvm::Function>(constant); FN) {
    // TODO: we create more work for the linker than we need so we should
    // fix this sometime
    assert(!FN->isIntrinsic());
    assert(alloc_size == 8);
    const auto sym = global_sym(FN);
    assert(sym != Assembler::INVALID_SYM_REF);
    relocs.push_back({off, 0, sym});
    return true;
  }
  if (auto *CE = llvm::dyn_cast<llvm::ConstantExpr>(constant); CE) {
    auto *inst = CE->getAsInstruction();
    const auto res =
        global_const_expr_to_data(reloc_base, data, relocs, layout, inst, off);
    inst->deleteValue();
    return res;
  }

  TPDE_LOG_ERR("Encountered unknown constant in global initializer");
  constant->print(llvm::errs(), true);
  llvm::errs() << "\n";

  return false;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::global_const_expr_to_data(
    const llvm::Value *reloc_base,
    tpde::util::SmallVector<u8, 64> &data,
    tpde::util::SmallVector<RelocInfo, 8> &relocs,
    const llvm::DataLayout &layout,
    llvm::Instruction *expr,
    u32 off) noexcept {
  // idk about this design, currently just hardcoding stuff i see
  // in theory i think this needs a new data buffer so we can recursively call
  // parseConstIntoByteArray
  switch (expr->getOpcode()) {
  case llvm::Instruction::IntToPtr: {
    auto *op = expr->getOperand(0);
    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(op); CI) {
      auto alloc_size = layout.getTypeAllocSize(expr->getType());
      // TODO: endianess?
      llvm::StoreIntToMemory(CI->getValue(), data.data() + off, alloc_size);
      return true;
    } else {
      TPDE_LOG_ERR("Operand to IntToPtr is not a constant int");
      return false;
    }
  }
  case llvm::Instruction::GetElementPtr: {
    auto *gep = llvm::cast<llvm::GetElementPtrInst>(expr);
    auto *ptr = gep->getPointerOperand();
    SymRef ptr_sym;
    if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(ptr); GV) {
      ptr_sym = global_sym(GV);
    } else {
      return false;
    }

    auto indices = tpde::util::SmallVector<llvm::Value *, 8>{};
    for (auto &idx : gep->indices()) {
      indices.push_back(idx.get());
    }

    const auto ty_off = layout.getIndexedOffsetInType(
        gep->getSourceElementType(),
        llvm::ArrayRef{indices.data(), indices.size()});
    relocs.push_back({off, static_cast<int32_t>(ty_off), ptr_sym});

    return true;
  }
  case llvm::Instruction::Trunc: {
    // recognize a truncation pattern where we need to emit PC32 relocations
    // i32 trunc (i64 sub (i64 ptrtoint (ptr <someglobal> to i64), i64
    // ptrtoint (ptr <relocBase> to i64)))
    if (expr->getType()->isIntegerTy(32)) {
      if (auto *sub = llvm::dyn_cast<llvm::ConstantExpr>(expr->getOperand(0));
          sub && sub->getOpcode() == llvm::Instruction::Sub &&
          sub->getType()->isIntegerTy(64)) {
        if (auto *lhs_ptr_to_int =
                llvm::dyn_cast<llvm::ConstantExpr>(sub->getOperand(0)),
            *rhs_ptr_to_int =
                llvm::dyn_cast<llvm::ConstantExpr>(sub->getOperand(1));
            lhs_ptr_to_int && rhs_ptr_to_int &&
            lhs_ptr_to_int->getOpcode() == llvm::Instruction::PtrToInt &&
            rhs_ptr_to_int->getOpcode() == llvm::Instruction::PtrToInt) {
          if (rhs_ptr_to_int->getOperand(0) == reloc_base &&
              llvm::isa<llvm::GlobalVariable>(lhs_ptr_to_int->getOperand(0))) {
            auto ptr_sym = global_sym(llvm::dyn_cast<llvm::GlobalValue>(
                lhs_ptr_to_int->getOperand(0)));

            relocs.push_back({off,
                              static_cast<int32_t>(off),
                              ptr_sym,
                              RelocInfo::RELOC_PC32});
            return true;
          }
        }
      }
    }
    return false;
  }
  default: {
    TPDE_LOG_ERR("Unknown constant expression in global initializer");
    llvm::errs() << *expr << "\n";
    return false;
  }
  }

  return false;
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::IRValueRef
    LLVMCompilerBase<Adaptor, Derived, Config>::llvm_val_idx(
        const llvm::Value *val) const noexcept {
  return this->adaptor->val_lookup_idx(val);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::IRValueRef
    LLVMCompilerBase<Adaptor, Derived, Config>::llvm_val_idx(
        const llvm::Instruction *inst) const noexcept {
  return this->adaptor->inst_lookup_idx(inst);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::get_or_create_sym_ref(
        SymRef &sym, std::string_view name, bool local, bool weak) noexcept {
  if (sym != Assembler::INVALID_SYM_REF) [[likely]] {
    return sym;
  }

  sym = this->assembler.sym_add_undef(name, local, weak);
  return sym;
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::global_sym(
        const llvm::GlobalValue *global) const noexcept {
  assert(global != nullptr);
  auto it = global_sym_lookup.find(global);
  if (it == global_sym_lookup.end()) {
    assert(0);
    return Assembler::INVALID_SYM_REF;
  }
  return it->second.first ? this->func_syms[it->second.second]
                          : global_syms[it->second.second];
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::
    setup_var_ref_assignments() noexcept {
  using AssignmentPartRef = typename Base::AssignmentPartRef;
  bool needs_globals = variable_refs.empty();

  variable_refs.resize(this->adaptor->initial_stack_slot_indices.size() +
                       this->adaptor->global_idx_end);

  // Allocate regs for globals
  if (needs_globals) {
    for (u32 v = 0; v < this->adaptor->global_idx_end; ++v) {
      variable_refs[v].val = v;
      variable_refs[v].alloca = false;
      variable_refs[v].local =
          llvm::cast<llvm::GlobalValue>(this->adaptor->values[v].val)
              ->hasLocalLinkage();
      // assignments are initialized lazily in val_ref_special.
    }
  }

  // Allocate registers for TPDE's stack slots
  u32 cur_idx = this->adaptor->global_idx_end;
  for (auto v : this->adaptor->cur_static_allocas()) {
    variable_refs[cur_idx].val = v;
    variable_refs[cur_idx].alloca = true;
    // static allocas don't need to be compiled later
    this->adaptor->val_set_fused(v, true);

    auto size = this->adaptor->val_alloca_size(v);
    auto align = this->adaptor->val_alloca_align(v);
    assert(align <= 16 && "over-aligned alloca not supported");
    size = tpde::util::align_up(size, align);
    const auto frame_off = this->allocate_stack_slot(size);

    variable_refs[cur_idx].alloca_frame_off = frame_off;

    auto *assignment = this->allocate_assignment(1, true);
    assignment->initialize(cur_idx++,
                           Config::PLATFORM_POINTER_SIZE,
                           0,
                           Config::PLATFORM_POINTER_SIZE);
    this->assignments.value_ptrs[this->adaptor->val_local_idx(v)] = assignment;

    auto ap = AssignmentPartRef{assignment, 0};
    ap.reset();
    ap.set_bank(Config::GP_BANK);
    ap.set_variable_ref(true);
    ap.set_part_size(Config::PLATFORM_POINTER_SIZE);
  }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile() {
  if (!Base::compile()) {
    return false;
  }

  // copy alias symbol definitions
  for (auto it = this->adaptor->mod.alias_begin();
       it != this->adaptor->mod.alias_end();
       ++it) {
    llvm::GlobalAlias *ga = &*it;
    auto *alias_target = llvm::dyn_cast<llvm::GlobalValue>(ga->getAliasee());
    if (alias_target == nullptr) {
      assert(0);
      continue;
    }
    auto dst_sym = global_sym(ga);
    auto from_sym = global_sym(alias_target);

    this->assembler.sym_copy(
        dst_sym, from_sym, ga->hasLocalLinkage(), ga->isWeakForLinker());
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_inst(
    IRValueRef val_idx, InstRange remaining) noexcept {
  TPDE_LOG_TRACE(
      "Compiling inst {} ({})", val_idx, this->adaptor->value_fmt_ref(val_idx));

  auto *i =
      llvm::dyn_cast<llvm::Instruction>(this->adaptor->values[val_idx].val);
  const auto opcode = i->getOpcode();
  // TODO(ts): loads are next
  switch (opcode) {
    // clang-format off
  case llvm::Instruction::Ret: return compile_ret(val_idx, i);
  case llvm::Instruction::Load: return compile_load(val_idx, i);
  case llvm::Instruction::Store: return compile_store(val_idx, i);
  case llvm::Instruction::Add: return compile_int_binary_op(val_idx, i, IntBinaryOp::add);
  case llvm::Instruction::Sub: return compile_int_binary_op(val_idx, i, IntBinaryOp::sub);
  case llvm::Instruction::Mul: return compile_int_binary_op(val_idx, i, IntBinaryOp::mul);
  case llvm::Instruction::UDiv: return compile_int_binary_op(val_idx, i, IntBinaryOp::udiv);
  case llvm::Instruction::SDiv: return compile_int_binary_op(val_idx, i, IntBinaryOp::sdiv);
  case llvm::Instruction::URem: return compile_int_binary_op(val_idx, i, IntBinaryOp::urem);
  case llvm::Instruction::SRem: return compile_int_binary_op(val_idx, i, IntBinaryOp::srem);
  case llvm::Instruction::And: return compile_int_binary_op(val_idx, i, IntBinaryOp::land);
  case llvm::Instruction::Or: return compile_int_binary_op(val_idx, i, IntBinaryOp::lor);
  case llvm::Instruction::Xor: return compile_int_binary_op(val_idx, i, IntBinaryOp::lxor);
  case llvm::Instruction::Shl: return compile_int_binary_op(val_idx, i, IntBinaryOp::shl);
  case llvm::Instruction::LShr: return compile_int_binary_op(val_idx, i, IntBinaryOp::shr);
  case llvm::Instruction::AShr: return compile_int_binary_op(val_idx, i, IntBinaryOp::ashr);
  case llvm::Instruction::FAdd: return compile_float_binary_op(val_idx, i, FloatBinaryOp::add);
  case llvm::Instruction::FSub: return compile_float_binary_op(val_idx, i, FloatBinaryOp::sub);
  case llvm::Instruction::FMul: return compile_float_binary_op(val_idx, i, FloatBinaryOp::mul);
  case llvm::Instruction::FDiv: return compile_float_binary_op(val_idx, i, FloatBinaryOp::div);
  case llvm::Instruction::FRem: return compile_float_binary_op(val_idx, i, FloatBinaryOp::rem);
  case llvm::Instruction::FNeg: return compile_fneg(val_idx, i);
  case llvm::Instruction::FPExt:
  case llvm::Instruction::FPTrunc: return compile_float_ext_trunc(val_idx, i);
  case llvm::Instruction::FPToSI: return compile_float_to_int(val_idx, i, true);
  case llvm::Instruction::FPToUI: return compile_float_to_int(val_idx, i, false);
  case llvm::Instruction::SIToFP: return compile_int_to_float(val_idx, i, true);
  case llvm::Instruction::UIToFP: return compile_int_to_float(val_idx, i, false);
  case llvm::Instruction::Trunc: return compile_int_trunc(val_idx, i);
  case llvm::Instruction::SExt: return compile_int_ext(val_idx, i, true);
  case llvm::Instruction::ZExt: return compile_int_ext(val_idx, i, false);
  case llvm::Instruction::PtrToInt: return compile_ptr_to_int(val_idx, i);
  case llvm::Instruction::IntToPtr: return compile_int_to_ptr(val_idx, i);
  case llvm::Instruction::BitCast: return compile_bitcast(val_idx, i);
  case llvm::Instruction::ExtractValue: return compile_extract_value(val_idx, i);
  case llvm::Instruction::InsertValue: return compile_insert_value(val_idx, i);
  case llvm::Instruction::ExtractElement: return compile_extract_element(val_idx, i);
  case llvm::Instruction::InsertElement: return compile_insert_element(val_idx, i);
  case llvm::Instruction::AtomicCmpXchg: return compile_cmpxchg(val_idx, i);
  case llvm::Instruction::PHI: return compile_phi(val_idx, i);
  case llvm::Instruction::Freeze: return compile_freeze(val_idx, i);
  case llvm::Instruction::Unreachable: return derived()->compile_unreachable(val_idx, i);
  case llvm::Instruction::Alloca: return derived()->compile_alloca(val_idx, i);
  case llvm::Instruction::Br: return derived()->compile_br(val_idx, i);
  case llvm::Instruction::Call: return compile_call(val_idx, i);
  case llvm::Instruction::Select: return compile_select(val_idx, i);
  case llvm::Instruction::GetElementPtr: return compile_gep(val_idx, i, remaining);
  case llvm::Instruction::ICmp: return derived()->compile_icmp(val_idx, i, remaining);
  case llvm::Instruction::FCmp: return compile_fcmp(val_idx, i);
  case llvm::Instruction::Switch: return compile_switch(val_idx, i);
  case llvm::Instruction::Invoke: return compile_invoke(val_idx, i);
  case llvm::Instruction::LandingPad: return compile_landing_pad(val_idx, i);
  case llvm::Instruction::Resume: return compile_resume(val_idx, i);
    // clang-format on

  default: {
    TPDE_LOG_ERR("Encountered unknown instruction opcode {}: {}",
                 opcode,
                 i->getOpcodeName());
    return false;
  }
  }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ret(
    IRValueRef, llvm::Instruction *ret) noexcept {
  assert(llvm::isa<llvm::ReturnInst>(ret));

  if (ret->getNumOperands() != 0) {
    derived()->move_val_to_ret_regs(ret->getOperand(0));
  }

  derived()->gen_func_epilog();
  this->release_regs_after_return();
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_load_generic(
    const IRValueRef load_idx,
    llvm::LoadInst *load,
    GenericValuePart &&ptr_op) noexcept {
  auto res = this->result_ref_lazy(load_idx, 0);
  // TODO(ts): if the ref-count is <= 1, then skip emitting the load as LLVM
  // does that, too. at least on ARM

  if (load->isAtomic()) {
    u32 width = 64;
    if (load->getType()->isIntegerTy()) {
      width = load->getType()->getIntegerBitWidth();
      if (width != 8 && width != 16 && width != 32 && width != 64) {
        TPDE_LOG_ERR("atomic loads not of i8/i16/i32/i64/ptr not supported");
        return false;
      }
    } else if (!load->getType()->isPointerTy()) {
      TPDE_LOG_ERR("atomic loads not of i8/i16/i32/i64/ptr not supported");
      return false;
    }
    u32 needed_align = 1;
    switch (width) {
    case 16: needed_align = 2; break;
    case 32: needed_align = 4; break;
    case 64: needed_align = 8; break;
    }
    if (load->getAlign().value() < needed_align) {
      TPDE_LOG_ERR(
          "atomic load of width {} has alignment {} which is too small",
          width,
          load->getAlign().value());
      return false;
    }

    const auto order = load->getOrdering();
    using EncodeFnTy = bool (Derived::*)(GenericValuePart, ScratchReg &);
    EncodeFnTy encode_fn = nullptr;
    if (order == llvm::AtomicOrdering::Monotonic) {
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_load_u8_mono; break;
      case 16: encode_fn = &Derived::encode_atomic_load_u16_mono; break;
      case 32: encode_fn = &Derived::encode_atomic_load_u32_mono; break;
      case 64: encode_fn = &Derived::encode_atomic_load_u64_mono; break;
      default: __builtin_unreachable();
      }
    } else if (order == llvm::AtomicOrdering::Acquire) {
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_load_u8_acq; break;
      case 16: encode_fn = &Derived::encode_atomic_load_u16_acq; break;
      case 32: encode_fn = &Derived::encode_atomic_load_u32_acq; break;
      case 64: encode_fn = &Derived::encode_atomic_load_u64_acq; break;
      default: __builtin_unreachable();
      }
    } else {
      assert(order == llvm::AtomicOrdering::SequentiallyConsistent);
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_load_u8_seqcst; break;
      case 16: encode_fn = &Derived::encode_atomic_load_u16_seqcst; break;
      case 32: encode_fn = &Derived::encode_atomic_load_u32_seqcst; break;
      case 64: encode_fn = &Derived::encode_atomic_load_u64_seqcst; break;
      default: __builtin_unreachable();
      }
    }

    ScratchReg res_scratch{derived()};
    if (!(derived()->*encode_fn)(std::move(ptr_op), res_scratch)) {
      return false;
    }

    this->set_value(res, res_scratch);
    return true;
  }

  ScratchReg res_scratch{this};
  switch (this->adaptor->values[load_idx].type) {
    using enum LLVMBasicValType;
  case i1:
  case i8:
  case i16:
  case i32:
  case i64: {
    assert(load->getType()->isIntegerTy());
    const auto bit_width = load->getType()->getIntegerBitWidth();
    switch (bit_width) {
    case 1:
    case 8: derived()->encode_loadi8(std::move(ptr_op), res_scratch); break;
    case 16: derived()->encode_loadi16(std::move(ptr_op), res_scratch); break;
    case 24: derived()->encode_loadi24(std::move(ptr_op), res_scratch); break;
    case 32: derived()->encode_loadi32(std::move(ptr_op), res_scratch); break;
    case 40: derived()->encode_loadi40(std::move(ptr_op), res_scratch); break;
    case 48: derived()->encode_loadi48(std::move(ptr_op), res_scratch); break;
    case 56: derived()->encode_loadi56(std::move(ptr_op), res_scratch); break;
    case 64: derived()->encode_loadi64(std::move(ptr_op), res_scratch); break;
    default: assert(0); return false;
    }
    break;
  }
  case ptr: {
    derived()->encode_loadi64(std::move(ptr_op), res_scratch);
    break;
  }
  case i128: {
    ScratchReg res_scratch_high{derived()};
    res.inc_ref_count();
    auto res_high = this->result_ref_lazy(load_idx, 1);

    derived()->encode_loadi128(
        std::move(ptr_op), res_scratch, res_scratch_high);
    this->set_value(res, res_scratch);
    this->set_value(res_high, res_scratch_high);
    return true;
  }
  case v32:
  case f32: {
    derived()->encode_loadf32(std::move(ptr_op), res_scratch);
    break;
  }
  case v64:
  case f64: {
    derived()->encode_loadf64(std::move(ptr_op), res_scratch);
    break;
  }
  case v128: {
    derived()->encode_loadv128(std::move(ptr_op), res_scratch);
    break;
  }
  case complex: {
    res.reset_without_refcount();

    auto ty_idx = this->adaptor->values[load_idx].complex_part_tys_idx;
    const LLVMComplexPart *part_descs =
        &this->adaptor->complex_part_types[ty_idx + 1];
    unsigned part_count = part_descs[-1].num_parts;

    // TODO: fuse expr; not easy, because we lose the GVP
    AsmReg ptr_reg = this->gval_as_reg(ptr_op);

    unsigned off = 0;
    for (unsigned i = 0; i < part_count; i++) {
      auto part_ref = this->result_ref_lazy(load_idx, i);
      auto part_addr =
          typename GenericValuePart::Expr{ptr_reg, static_cast<tpde::i32>(off)};
      auto part_ty = part_descs[i].part.type;
      switch (part_ty) {
      case i1:
      case i8:
        derived()->encode_loadi8(std::move(part_addr), res_scratch);
        break;
      case i16:
        derived()->encode_loadi16(std::move(part_addr), res_scratch);
        break;
      case i32:
        derived()->encode_loadi32(std::move(part_addr), res_scratch);
        break;
      case i64:
      case ptr:
        derived()->encode_loadi64(std::move(part_addr), res_scratch);
        break;
      case i128:
        derived()->encode_loadi64(std::move(part_addr), res_scratch);
        break;
      case v32:
      case f32:
        derived()->encode_loadf32(std::move(part_addr), res_scratch);
        break;
      case v64:
      case f64:
        derived()->encode_loadf64(std::move(part_addr), res_scratch);
        break;
      case v128:
        derived()->encode_loadv128(std::move(part_addr), res_scratch);
        break;
      default: assert(0); return false;
      }

      off += part_descs[i].part.size + part_descs[i].part.pad_after;

      this->set_value(part_ref, res_scratch);
      if (i != part_count - 1) {
        part_ref.reset_without_refcount();
      }
    }
    return true;
  }
  default: assert(0); return false;
  }

  this->set_value(res, res_scratch);

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_load(
    const IRValueRef load_idx, llvm::Instruction *inst) noexcept {
  auto *load = llvm::cast<llvm::LoadInst>(inst);

  auto ptr_ref = this->val_ref(llvm_val_idx(load->getPointerOperand()), 0);
  if (!ptr_ref.is_const && ptr_ref.assignment().variable_ref()) {
    const auto ref_idx = ptr_ref.state.v.assignment->var_ref_custom_idx;
    if (this->variable_refs[ref_idx].alloca) {
      GenericValuePart addr = derived()->create_addr_for_alloca(ref_idx);
      return compile_load_generic(load_idx, load, std::move(addr));
    }
  }

  return compile_load_generic(load_idx, load, std::move(ptr_ref));
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_store_generic(
    llvm::StoreInst *store, GenericValuePart &&ptr_op) noexcept {
  if (store->isAtomic()) {
    // TODO: atomic stores
    return false;
  }

  const auto *op_val = store->getValueOperand();
  const auto op_idx = llvm_val_idx(op_val);
  auto op_ref = this->val_ref(op_idx, 0);

  switch (this->adaptor->values[op_idx].type) {
    using enum LLVMBasicValType;
  case i1:
  case i8:
  case i16:
  case i32:
  case i64: {
    assert(op_val->getType()->isIntegerTy());
    const auto bit_width = op_val->getType()->getIntegerBitWidth();
    switch (bit_width) {
    case 1:
    case 8:
      derived()->encode_storei8(std::move(ptr_op), std::move(op_ref));
      break;
    case 16:
      derived()->encode_storei16(std::move(ptr_op), std::move(op_ref));
      break;
    case 24:
      derived()->encode_storei24(std::move(ptr_op), std::move(op_ref));
      break;
    case 32:
      derived()->encode_storei32(std::move(ptr_op), std::move(op_ref));
      break;
    case 40:
      derived()->encode_storei40(std::move(ptr_op), std::move(op_ref));
      break;
    case 48:
      derived()->encode_storei48(std::move(ptr_op), std::move(op_ref));
      break;
    case 56:
      derived()->encode_storei56(std::move(ptr_op), std::move(op_ref));
      break;
    case 64:
      derived()->encode_storei64(std::move(ptr_op), std::move(op_ref));
      break;
    default: assert(0); return false;
    }
    break;
  }
  case ptr: {
    derived()->encode_storei64(std::move(ptr_op), std::move(op_ref));
    break;
  }
  case i128: {
    op_ref.inc_ref_count();
    auto op_ref_high = this->val_ref(op_idx, 1);

    derived()->encode_storei128(
        std::move(ptr_op), std::move(op_ref), std::move(op_ref_high));
    break;
  }
  case v32:
  case f32: {
    derived()->encode_storef32(std::move(ptr_op), std::move(op_ref));
    break;
  }
  case v64:
  case f64: {
    derived()->encode_storef64(std::move(ptr_op), std::move(op_ref));
    break;
  }
  case v128: {
    derived()->encode_storev128(std::move(ptr_op), std::move(op_ref));
    break;
  }
  case complex: {
    op_ref.reset_without_refcount();

    const auto ty_idx = this->adaptor->values[op_idx].complex_part_tys_idx;
    const LLVMComplexPart *part_descs =
        &this->adaptor->complex_part_types[ty_idx + 1];
    unsigned part_count = part_descs[-1].num_parts;

    // TODO: fuse expr; not easy, because we lose the GVP
    AsmReg ptr_reg = this->gval_as_reg(ptr_op);

    unsigned off = 0;
    for (unsigned i = 0; i < part_count; i++) {
      auto part_ref = this->val_ref(op_idx, i);
      auto part_addr =
          typename GenericValuePart::Expr{ptr_reg, static_cast<tpde::i32>(off)};
      auto part_ty = part_descs[i].part.type;
      switch (part_ty) {
      case i1:
      case i8: derived()->encode_storei8(std::move(part_addr), part_ref); break;
      case i16:
        derived()->encode_storei16(std::move(part_addr), part_ref);
        break;
      case i32:
        derived()->encode_storei32(std::move(part_addr), part_ref);
        break;
      case i64:
      case ptr:
        derived()->encode_storei64(std::move(part_addr), part_ref);
        break;
      case i128:
        derived()->encode_storei64(std::move(part_addr), part_ref);
        break;
      case v32:
      case f32:
        derived()->encode_storef32(std::move(part_addr), part_ref);
        break;
      case v64:
      case f64:
        derived()->encode_storef64(std::move(part_addr), part_ref);
        break;
      case v128:
        derived()->encode_storev128(std::move(part_addr), part_ref);
        break;
      default: assert(0); return false;
      }

      off += part_descs[i].part.size + part_descs[i].part.pad_after;
      if (i != part_count - 1) {
        part_ref.reset_without_refcount();
      }
    }
    return true;
  }
  default: assert(0); return false;
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_store(
    const IRValueRef, llvm::Instruction *inst) noexcept {
  auto *store = llvm::cast<llvm::StoreInst>(inst);
  if (store->isAtomic()) {
    // TODO: atomic stores
    return false;
  }

  auto ptr_ref = this->val_ref(llvm_val_idx(store->getPointerOperand()), 0);
  if (!ptr_ref.is_const && ptr_ref.assignment().variable_ref()) {
    const auto ref_idx = ptr_ref.state.v.assignment->var_ref_custom_idx;
    if (this->variable_refs[ref_idx].alloca) {
      GenericValuePart addr = derived()->create_addr_for_alloca(ref_idx);
      return compile_store_generic(store, std::move(addr));
    }
  }

  return compile_store_generic(store, std::move(ptr_ref));
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_binary_op(
    IRValueRef inst_idx,
    llvm::Instruction *inst,
    const IntBinaryOp op) noexcept {
  using EncodeImm = typename Derived::EncodeImm;

  auto *inst_ty = inst->getType();

  if (inst_ty->isVectorTy()) {
    return false;
  }

  assert(inst_ty->isIntegerTy());

  const auto int_width = inst_ty->getIntegerBitWidth();
  if (int_width == 128) {
    auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

    // TODO(ts): better salvaging
    lhs.inc_ref_count();
    rhs.inc_ref_count();

    auto lhs_high = this->val_ref(llvm_val_idx(inst->getOperand(0)), 1);
    auto rhs_high = this->val_ref(llvm_val_idx(inst->getOperand(1)), 1);

    if ((op == IntBinaryOp::add || op == IntBinaryOp::mul ||
         op == IntBinaryOp::land || op == IntBinaryOp::lor ||
         op == IntBinaryOp::lxor) &&
        lhs.is_const && lhs_high.is_const && !rhs.is_const &&
        !rhs_high.is_const) {
      // TODO(ts): this is a hack since the encoder can currently not do
      // commutable operations so we reorder immediates manually here
      std::swap(lhs, rhs);
      std::swap(lhs_high, rhs_high);
    }

    auto res_low = this->result_ref_lazy(inst_idx, 0);
    res_low.inc_ref_count();
    auto res_high = this->result_ref_lazy(inst_idx, 1);

    ScratchReg scratch_low{derived()}, scratch_high{derived()};


    std::array<bool (Derived::*)(GenericValuePart,
                                 GenericValuePart,
                                 GenericValuePart,
                                 GenericValuePart,
                                 ScratchReg &,
                                 ScratchReg &),
               10>
        encode_ptrs = {
            {
             &Derived::encode_addi128,
             &Derived::encode_subi128,
             &Derived::encode_muli128,
#if 0
                 &Derived::encode_udivi128,
                 &Derived::encode_sdivi128,
                 &Derived::encode_uremi128,
                 &Derived::encode_sremi128,
#else
             nullptr, nullptr,
             nullptr, nullptr,
#endif
             &Derived::encode_landi128,
             &Derived::encode_lori128,
             &Derived::encode_lxori128,
             }
    };

    if (op == IntBinaryOp::udiv || op == IntBinaryOp::sdiv ||
        op == IntBinaryOp::urem || op == IntBinaryOp::srem) {
      // TODO(ts): the autoencoder can currently not generate calls to
      // globals which LLVM generates for 128 bit division so we need to
      // fix that or do the calls ourselves (which is probably easier rn)
      llvm::errs() << "Division/Remainder for 128bit integers currently "
                      "unimplemented\n";
      return false;
    }

    switch (op) {
    case IntBinaryOp::shl:
    case IntBinaryOp::shr:
    case IntBinaryOp::ashr:
      rhs_high.reset();
      if (rhs.is_const) {
        u32 amt = rhs.state.c.const_u64 & 0b111'1111;
        u32 iamt = 64 - amt;
        if (amt < 64) {
          if (op == IntBinaryOp::shl) {
            derived()->encode_shli128_lt64(std::move(lhs),
                                           std::move(lhs_high),
                                           EncodeImm{amt & 0x3f},
                                           EncodeImm{iamt & 0x3f},
                                           scratch_low,
                                           scratch_high);
          } else if (op == IntBinaryOp::shr) {
            derived()->encode_shri128_lt64(std::move(lhs),
                                           std::move(lhs_high),
                                           EncodeImm{amt & 0x3f},
                                           EncodeImm{iamt & 0x3f},
                                           scratch_low,
                                           scratch_high);
          } else {
            assert(op == IntBinaryOp::ashr);
            derived()->encode_ashri128_lt64(std::move(lhs),
                                            std::move(lhs_high),
                                            EncodeImm{amt & 0x3f},
                                            EncodeImm{iamt & 0x3f},
                                            scratch_low,
                                            scratch_high);
          }
        } else {
          amt -= 64;
          if (op == IntBinaryOp::shl) {
            derived()->encode_shli128_ge64(std::move(lhs),
                                           EncodeImm{amt & 0x3f},
                                           scratch_low,
                                           scratch_high);
          } else if (op == IntBinaryOp::shr) {
            derived()->encode_shri128_ge64(std::move(lhs_high),
                                           EncodeImm{amt & 0x3f},
                                           scratch_low,
                                           scratch_high);
          } else {
            assert(op == IntBinaryOp::ashr);
            derived()->encode_ashri128_ge64(std::move(lhs_high),
                                            EncodeImm{amt & 0x3f},
                                            scratch_low,
                                            scratch_high);
          }
        }
        break;
      }
      if (op == IntBinaryOp::shl) {
        derived()->encode_shli128(std::move(lhs),
                                  std::move(lhs_high),
                                  std::move(rhs),
                                  scratch_low,
                                  scratch_high);
      } else if (op == IntBinaryOp::shr) {
        derived()->encode_shri128(std::move(lhs),
                                  std::move(lhs_high),
                                  std::move(rhs),
                                  scratch_low,
                                  scratch_high);
      } else {
        assert(op == IntBinaryOp::ashr);
        derived()->encode_ashri128(std::move(lhs),
                                   std::move(lhs_high),
                                   std::move(rhs),
                                   scratch_low,
                                   scratch_high);
      }
      break;
    default:
      (derived()->*(encode_ptrs[static_cast<u32>(op)]))(std::move(lhs),
                                                        std::move(lhs_high),
                                                        std::move(rhs),
                                                        std::move(rhs_high),
                                                        scratch_low,
                                                        scratch_high);
      break;
    }

    this->set_value(res_low, scratch_low);
    this->set_value(res_high, scratch_high);

    return true;
  }
  assert(int_width <= 64);

  // encode functions for 32/64 bit operations
  using EncodeFnTy =
      bool (Derived::*)(GenericValuePart, GenericValuePart, ScratchReg &);
  std::array<std::array<EncodeFnTy, 2>, 13> encode_ptrs{
      {
       {&Derived::encode_addi32, &Derived::encode_addi64},
       {&Derived::encode_subi32, &Derived::encode_subi64},
       {&Derived::encode_muli32, &Derived::encode_muli64},
       {&Derived::encode_udivi32, &Derived::encode_udivi64},
       {&Derived::encode_sdivi32, &Derived::encode_sdivi64},
       {&Derived::encode_uremi32, &Derived::encode_uremi64},
       {&Derived::encode_sremi32, &Derived::encode_sremi64},
       {&Derived::encode_landi32, &Derived::encode_landi64},
       {&Derived::encode_lori32, &Derived::encode_lori64},
       {&Derived::encode_lxori32, &Derived::encode_lxori64},
       {&Derived::encode_shli32, &Derived::encode_shli64},
       {&Derived::encode_shri32, &Derived::encode_shri64},
       {&Derived::encode_ashri32, &Derived::encode_ashri64},
       }
  };

  auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
  auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);

  if ((op == IntBinaryOp::add || op == IntBinaryOp::mul ||
       op == IntBinaryOp::land || op == IntBinaryOp::lor ||
       op == IntBinaryOp::lxor) &&
      lhs.is_const && !rhs.is_const) {
    // TODO(ts): this is a hack since the encoder can currently not do
    // commutable operations so we reorder immediates manually here
    std::swap(lhs, rhs);
  }

  // TODO(ts): optimize div/rem by constant to a shift?
  const auto lhs_const = lhs.is_const;
  const auto rhs_const = rhs.is_const;
  GenericValuePart lhs_op = std::move(lhs);
  GenericValuePart rhs_op = std::move(rhs);

  unsigned ext_width = tpde::util::align_up(int_width, 32);
  if (ext_width != int_width &&
      (op == IntBinaryOp::udiv || op == IntBinaryOp::sdiv ||
       op == IntBinaryOp::urem || op == IntBinaryOp::srem ||
       op == IntBinaryOp::shl || op == IntBinaryOp::shr ||
       op == IntBinaryOp::ashr)) {
    if (op == IntBinaryOp::sdiv || op == IntBinaryOp::srem ||
        op == IntBinaryOp::ashr) {
      // need to sign-extend lhs
      // TODO(ts): if lhs is constant, sign-extend as constant
      lhs_op =
          derived()->ext_int(std::move(lhs_op), true, int_width, ext_width);
    } else if ((op == IntBinaryOp::udiv || op == IntBinaryOp::urem ||
                op == IntBinaryOp::shr) &&
               !lhs_const) {
      // need to zero-extend lhs (if it is not an immediate)
      lhs_op =
          derived()->ext_int(std::move(lhs_op), false, int_width, ext_width);
    }

    // rhs doesn't need extension for shift, if it is larger than the bit
    // width, the result is poison.
    if (op == IntBinaryOp::sdiv || op == IntBinaryOp::srem) {
      // need to sign-extend rhs
      // TODO(ts): if rhs is constant, sign-extend as constant
      rhs_op =
          derived()->ext_int(std::move(rhs_op), true, int_width, ext_width);
    } else if ((op == IntBinaryOp::udiv || op == IntBinaryOp::urem) &&
               !rhs_const) {
      // need to zero-extend rhs (if it is not an immediate since then
      // this is guaranteed by LLVM)
      rhs_op =
          derived()->ext_int(std::move(rhs_op), false, int_width, ext_width);
    }
  }

  auto res = this->result_ref_lazy(inst_idx, 0);

  auto res_scratch = ScratchReg{derived()};

  (derived()->*(encode_ptrs[static_cast<u32>(op)][ext_width / 32 - 1]))(
      std::move(lhs_op), std::move(rhs_op), res_scratch);

  this->set_value(res, res_scratch);

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_binary_op(
    IRValueRef inst_idx, llvm::Instruction *inst, FloatBinaryOp op) noexcept {
  auto *inst_ty = inst->getType();
  auto *scalar_ty = inst_ty->getScalarType();

  const bool is_double = scalar_ty->isDoubleTy();
  if (!scalar_ty->isFloatTy() && !scalar_ty->isDoubleTy()) {
    return false;
  }

  if (op == FloatBinaryOp::rem) {
    if (inst_ty->isVectorTy()) {
      return false;
    }
    // TODO(ts): encodegen cannot encode calls atm
    derived()->create_frem_calls(llvm_val_idx(inst->getOperand(0)),
                                 llvm_val_idx(inst->getOperand(1)),
                                 this->result_ref_lazy(inst_idx, 0),
                                 is_double);
    return true;
  }

  using EncodeFnTy =
      bool (Derived::*)(GenericValuePart, GenericValuePart, ScratchReg &);
  EncodeFnTy encode_fn = nullptr;

  switch (this->adaptor->values[inst_idx].type) {
    using enum LLVMBasicValType;
  case f32:
    assert(!is_double);
    switch (op) {
    case FloatBinaryOp::add: encode_fn = &Derived::encode_addf32; break;
    case FloatBinaryOp::sub: encode_fn = &Derived::encode_subf32; break;
    case FloatBinaryOp::mul: encode_fn = &Derived::encode_mulf32; break;
    case FloatBinaryOp::div: encode_fn = &Derived::encode_divf32; break;
    default: TPDE_UNREACHABLE("invalid FloatBinaryOp");
    }
    break;
  case f64:
    assert(is_double);
    switch (op) {
    case FloatBinaryOp::add: encode_fn = &Derived::encode_addf64; break;
    case FloatBinaryOp::sub: encode_fn = &Derived::encode_subf64; break;
    case FloatBinaryOp::mul: encode_fn = &Derived::encode_mulf64; break;
    case FloatBinaryOp::div: encode_fn = &Derived::encode_divf64; break;
    default: TPDE_UNREACHABLE("invalid FloatBinaryOp");
    }
    break;
  case v64:
    assert(!is_double);
    switch (op) {
    case FloatBinaryOp::add: encode_fn = &Derived::encode_addv2f32; break;
    case FloatBinaryOp::sub: encode_fn = &Derived::encode_subv2f32; break;
    case FloatBinaryOp::mul: encode_fn = &Derived::encode_mulv2f32; break;
    case FloatBinaryOp::div: encode_fn = &Derived::encode_divv2f32; break;
    default: TPDE_UNREACHABLE("invalid FloatBinaryOp");
    }
    break;
  case v128:
    if (!is_double) {
      switch (op) {
      case FloatBinaryOp::add: encode_fn = &Derived::encode_addv4f32; break;
      case FloatBinaryOp::sub: encode_fn = &Derived::encode_subv4f32; break;
      case FloatBinaryOp::mul: encode_fn = &Derived::encode_mulv4f32; break;
      case FloatBinaryOp::div: encode_fn = &Derived::encode_divv4f32; break;
      default: TPDE_UNREACHABLE("invalid FloatBinaryOp");
      }
    } else {
      switch (op) {
      case FloatBinaryOp::add: encode_fn = &Derived::encode_addv2f64; break;
      case FloatBinaryOp::sub: encode_fn = &Derived::encode_subv2f64; break;
      case FloatBinaryOp::mul: encode_fn = &Derived::encode_mulv2f64; break;
      case FloatBinaryOp::div: encode_fn = &Derived::encode_divv2f64; break;
      default: TPDE_UNREACHABLE("invalid FloatBinaryOp");
      }
    }
    break;
  default: TPDE_UNREACHABLE("invalid basic type for float binary op");
  }

  auto res = this->result_ref_lazy(inst_idx, 0);
  auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
  auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
  ScratchReg res_scratch{derived()};
  if (!(derived()->*encode_fn)(std::move(lhs), std::move(rhs), res_scratch)) {
    return false;
  }
  this->set_value(res, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_fneg(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *scalar_ty = inst->getType()->getScalarType();
  const bool is_double = scalar_ty->isDoubleTy();
  if (!scalar_ty->isFloatTy() && !scalar_ty->isDoubleTy()) {
    return false;
  }

  auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
  auto res_ref = this->result_ref_lazy(inst_idx, 0);
  auto res_scratch = ScratchReg{derived()};
  switch (this->adaptor->values[inst_idx].type) {
    using enum LLVMBasicValType;
  case f32: derived()->encode_fnegf32(std::move(src_ref), res_scratch); break;
  case f64: derived()->encode_fnegf64(std::move(src_ref), res_scratch); break;
  case v64:
    assert(!is_double);
    derived()->encode_fnegv2f32(std::move(src_ref), res_scratch);
    break;
  case v128:
    if (!is_double) {
      derived()->encode_fnegv4f32(std::move(src_ref), res_scratch);
    } else {
      derived()->encode_fnegv2f64(std::move(src_ref), res_scratch);
    }
    break;
  default: TPDE_UNREACHABLE("invalid basic type for fneg");
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_ext_trunc(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *src_val = inst->getOperand(0);
  auto *src_ty = src_val->getType();
  auto *dst_ty = inst->getType();

  auto res_ref = this->result_ref_lazy(inst_idx, 0);

  ScratchReg res_scratch{derived()};
  SymRef sym = Assembler::INVALID_SYM_REF;
  if (src_ty->isDoubleTy() && dst_ty->isFloatTy()) {
    auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);
    derived()->encode_f64tof32(std::move(src_ref), res_scratch);
  } else if (src_ty->isFP128Ty() && dst_ty->isFloatTy()) {
    sym = get_or_create_sym_ref(sym_trunctfsf2, "__trunctfsf2");
  } else if (src_ty->isFP128Ty() && dst_ty->isDoubleTy()) {
    sym = get_or_create_sym_ref(sym_trunctfdf2, "__trunctfdf2");
  } else if (src_ty->isFloatTy() && dst_ty->isDoubleTy()) {
    auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);
    derived()->encode_f32tof64(std::move(src_ref), res_scratch);
  } else if (src_ty->isFloatTy() && dst_ty->isFP128Ty()) {
    sym = get_or_create_sym_ref(sym_extendsftf2, "__extendsftf2");
  } else if (src_ty->isDoubleTy() && dst_ty->isFP128Ty()) {
    sym = get_or_create_sym_ref(sym_extenddftf2, "__extenddftf2");
  }

  if (res_scratch.cur_reg.valid()) {
    this->set_value(res_ref, res_scratch);
  } else if (sym != Assembler::INVALID_SYM_REF) {
    IRValueRef src_ref = llvm_val_idx(src_val);
    derived()->create_helper_call({&src_ref, 1}, {&res_ref, 1}, sym);
  } else {
    return false;
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_to_int(
    IRValueRef inst_idx, llvm::Instruction *inst, const bool sign) noexcept {
  auto *src_val = inst->getOperand(0);
  auto *src_ty = src_val->getType();

  const auto bit_width = inst->getType()->getIntegerBitWidth();
  if (bit_width > 64 || !(src_ty->isFloatTy() || src_ty->isDoubleTy())) {
    return false;
  }

  const auto src_double = src_ty->isDoubleTy();

  auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);
  auto res_ref = this->result_ref_lazy(inst_idx, 0);
  auto res_scratch = ScratchReg{derived()};
  if (sign) {
    if (src_double) {
      if (bit_width > 32) {
        derived()->encode_f64toi64(std::move(src_ref), res_scratch);
      } else {
        derived()->encode_f64toi32(std::move(src_ref), res_scratch);
      }
    } else {
      if (bit_width > 32) {
        derived()->encode_f32toi64(std::move(src_ref), res_scratch);
      } else {
        derived()->encode_f32toi32(std::move(src_ref), res_scratch);
      }
    }
  } else {
    if (src_double) {
      if (bit_width > 32) {
        derived()->encode_f64tou64(std::move(src_ref), res_scratch);
      } else {
        derived()->encode_f64tou32(std::move(src_ref), res_scratch);
      }
    } else {
      if (bit_width > 32) {
        derived()->encode_f32tou64(std::move(src_ref), res_scratch);
      } else {
        derived()->encode_f32tou32(std::move(src_ref), res_scratch);
      }
    }
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_to_float(
    IRValueRef inst_idx, llvm::Instruction *inst, const bool sign) noexcept {
  auto *src_val = inst->getOperand(0);
  auto *dst_ty = inst->getType();
  auto bit_width = src_val->getType()->getIntegerBitWidth();

  if (bit_width > 64 || !(dst_ty->isFloatTy() || dst_ty->isDoubleTy())) {
    return false;
  }

  const auto dst_double = dst_ty->isDoubleTy();

  GenericValuePart src_op = this->val_ref(llvm_val_idx(src_val), 0);
  auto res_ref = this->result_ref_lazy(inst_idx, 0);
  auto res_scratch = ScratchReg{derived()};

  if (bit_width != 32 && bit_width != 64) {
    unsigned ext = tpde::util::align_up(bit_width, 32);
    src_op = derived()->ext_int(std::move(src_op), sign, bit_width, ext);
  }

  if (sign) {
    if (bit_width > 32) {
      if (dst_double) {
        derived()->encode_i64tof64(std::move(src_op), res_scratch);
      } else {
        derived()->encode_i64tof32(std::move(src_op), res_scratch);
      }
    } else {
      if (dst_double) {
        derived()->encode_i32tof64(std::move(src_op), res_scratch);
      } else {
        derived()->encode_i32tof32(std::move(src_op), res_scratch);
      }
    }
  } else {
    if (bit_width > 32) {
      if (dst_double) {
        derived()->encode_u64tof64(std::move(src_op), res_scratch);
      } else {
        derived()->encode_u64tof32(std::move(src_op), res_scratch);
      }
    } else {
      if (dst_double) {
        derived()->encode_u32tof64(std::move(src_op), res_scratch);
      } else {
        derived()->encode_u32tof32(std::move(src_op), res_scratch);
      }
    }
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_trunc(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  // this is a no-op since every operation that depends on it will
  // zero/sign-extend the value anyways
  auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);

  AsmReg orig;
  auto res_ref = this->result_ref_salvage_with_original(
      inst_idx, 0, std::move(src_ref), orig);
  if (orig != res_ref.cur_reg()) {
    derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
  }
  this->set_value(res_ref, res_ref.cur_reg());

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_ext(
    IRValueRef inst_idx, llvm::Instruction *inst, bool sign) noexcept {
  auto *src_val = inst->getOperand(0);

  unsigned src_width = src_val->getType()->getIntegerBitWidth();
  unsigned dst_width = inst->getType()->getIntegerBitWidth();
  assert(dst_width >= src_width);

  auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);

  ScratchReg res_scratch{derived()};
  if (src_width < 64) {
    unsigned ext_width = dst_width <= 64 ? dst_width : 64;
    res_scratch =
        derived()->ext_int(std::move(src_ref), sign, src_width, ext_width);
  } else {
    if (src_width != 64) {
      return false;
    }
    if (src_ref.can_salvage()) {
      if (!src_ref.assignment().register_valid()) {
        src_ref.alloc_reg(true);
      }
      res_scratch.alloc_specific(src_ref.salvage());
    } else {
      ScratchReg tmp{derived()};
      auto src = this->val_as_reg(src_ref, tmp);

      derived()->mov(res_scratch.alloc_gp(), src, 8);
    }
  }

  auto res_ref = this->result_ref_lazy(inst_idx, 0);

  if (dst_width == 128) {
    if (src_width > 64) {
      return false;
    }
    res_ref.inc_ref_count();
    auto res_ref_high = this->result_ref_lazy(inst_idx, 1);
    ScratchReg scratch_high{derived()};

    if (sign) {
      derived()->encode_fill_with_sign64(res_scratch.cur_reg, scratch_high);
    } else {
      std::array<u8, 64> data{};
      derived()->materialize_constant(
          data, Config::GP_BANK, 8, scratch_high.alloc_gp());
    }

    this->set_value(res_ref_high, scratch_high);
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ptr_to_int(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  // this is a no-op since every operation that depends on it will
  // zero/sign-extend the value anyways
  auto src_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);

  AsmReg orig;
  auto res_ref = this->result_ref_salvage_with_original(
      inst_idx, 0, std::move(src_ref), orig);
  if (orig != res_ref.cur_reg()) {
    derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
  }
  this->set_value(res_ref, res_ref.cur_reg());

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_to_ptr(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  // zero-extend the value
  auto *src_val = inst->getOperand(0);
  const auto bit_width = src_val->getType()->getIntegerBitWidth();

  auto src_ref = this->val_ref(llvm_val_idx(src_val), 0);
  if (bit_width == 64) {
    // no-op
    AsmReg orig;
    auto res_ref = this->result_ref_salvage_with_original(
        inst_idx, 0, std::move(src_ref), orig);
    if (orig != res_ref.cur_reg()) {
      derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  } else if (bit_width < 64) {
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    auto res = derived()->ext_int(std::move(src_ref), false, bit_width, 64);
    this->set_value(res_ref, res);
    return true;
  }

  return false;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_bitcast(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  // at most this should be fine to implement as a copy operation
  // as the values cannot be aggregates
  const auto src_idx = llvm_val_idx(inst->getOperand(0));

  // TODO(ts): support 128bit values
  if (derived()->val_part_count(src_idx) != 1 ||
      derived()->val_part_count(inst_idx) != 1) {
    return false;
  }

  auto src_ref = this->val_ref(src_idx, 0);

  ScratchReg orig_scratch{derived()};
  AsmReg orig;
  ValuePartRef res_ref;
  if (derived()->val_part_bank(src_idx, 0) ==
      derived()->val_part_bank(inst_idx, 0)) {
    res_ref = this->result_ref_salvage_with_original(
        inst_idx, 0, std::move(src_ref), orig);
  } else {
    res_ref = this->result_ref_eager(inst_idx, 0);
    orig = this->val_as_reg(src_ref, orig_scratch);
  }

  if (orig != res_ref.cur_reg()) {
    derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
  }
  this->set_value(res_ref, res_ref.cur_reg());

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_extract_value(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *extract = llvm::cast<llvm::ExtractValueInst>(inst);
  auto src_idx = llvm_val_idx(inst->getOperand(0));

  auto [first_part, last_part] =
      this->adaptor->complex_part_for_index(src_idx, extract->getIndices());

  for (unsigned i = first_part; i <= last_part; i++) {
    auto part_ref = this->val_ref(src_idx, i);
    if (i != last_part) {
      part_ref.inc_ref_count();
    }

    AsmReg orig;
    auto res_ref =
        this->result_ref_salvage_with_original(inst_idx,
                                               i - first_part,
                                               std::move(part_ref),
                                               orig,
                                               i != last_part ? 2 : 1);
    if (orig != res_ref.cur_reg()) {
      derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());
    if (i != last_part) {
      res_ref.inc_ref_count();
    }
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_insert_value(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *insert = llvm::cast<llvm::InsertValueInst>(inst);
  auto agg_idx = llvm_val_idx(insert->getAggregateOperand());
  auto ins_idx = llvm_val_idx(insert->getInsertedValueOperand());

  unsigned part_count = this->adaptor->val_part_count(agg_idx);
  auto [first_part, last_part] =
      this->adaptor->complex_part_for_index(agg_idx, insert->getIndices());

  for (unsigned i = 0; i < part_count; i++) {
    ValuePartRef val_ref;
    bool inc_ref_count;
    if (i >= first_part && i <= last_part) {
      val_ref = this->val_ref(ins_idx, i - first_part);
      inc_ref_count = i != last_part;
    } else {
      val_ref = this->val_ref(agg_idx, i);
      inc_ref_count = i != part_count - 1 &&
                      (last_part != part_count - 1 || i != first_part - 1);
    }
    if (inc_ref_count) {
      val_ref.inc_ref_count();
    }
    AsmReg orig;
    auto res_ref = this->result_ref_salvage_with_original(
        inst_idx, i, std::move(val_ref), orig, inc_ref_count ? 2 : 1);
    if (orig != res_ref.cur_reg()) {
      derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());
    if (i != part_count - 1) {
      res_ref.inc_ref_count();
    }
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::extract_element(
    IRValueRef vec,
    unsigned idx,
    LLVMBasicValType ty,
    ScratchReg &out_reg) noexcept {
  assert(derived()->val_part_count(vec) == 1);

  ValuePartRef vec_ref = this->val_ref(vec, 0);
  vec_ref.spill();
  vec_ref.unlock();

  GenericValuePart addr = derived()->val_spill_slot(vec_ref);
  auto &expr = std::get<typename GenericValuePart::Expr>(addr.state);
  expr.disp += idx * this->adaptor->basic_ty_part_size(ty);

  switch (ty) {
    using enum LLVMBasicValType;
  case i8: derived()->encode_loadi8(std::move(addr), out_reg); break;
  case i16: derived()->encode_loadi16(std::move(addr), out_reg); break;
  case i32: derived()->encode_loadi32(std::move(addr), out_reg); break;
  case i64:
  case ptr: derived()->encode_loadi64(std::move(addr), out_reg); break;
  case f32: derived()->encode_loadf32(std::move(addr), out_reg); break;
  case f64: derived()->encode_loadf64(std::move(addr), out_reg); break;
  default: TPDE_UNREACHABLE("unexpected vector element type");
  }

  vec_ref.reset_without_refcount();
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::insert_element(
    IRValueRef vec,
    unsigned idx,
    LLVMBasicValType ty,
    GenericValuePart el) noexcept {
  assert(derived()->val_part_count(vec) == 1);

  ValuePartRef vec_ref = this->val_ref(vec, 0);
  vec_ref.spill();
  vec_ref.unlock();
  if (vec_ref.assignment().register_valid()) {
    vec_ref.assignment().set_register_valid(false);
    this->register_file.unmark_used(AsmReg{vec_ref.assignment().full_reg_id()});
  }

  GenericValuePart addr = derived()->val_spill_slot(vec_ref);
  auto &expr = std::get<typename GenericValuePart::Expr>(addr.state);
  expr.disp += idx * this->adaptor->basic_ty_part_size(ty);

  switch (ty) {
    using enum LLVMBasicValType;
  case i8: derived()->encode_storei8(std::move(addr), std::move(el)); break;
  case i16: derived()->encode_storei16(std::move(addr), std::move(el)); break;
  case i32: derived()->encode_storei32(std::move(addr), std::move(el)); break;
  case i64:
  case ptr: derived()->encode_storei64(std::move(addr), std::move(el)); break;
  case f32: derived()->encode_storef32(std::move(addr), std::move(el)); break;
  case f64: derived()->encode_storef64(std::move(addr), std::move(el)); break;
  default: TPDE_UNREACHABLE("unexpected vector element type");
  }

  vec_ref.reset_without_refcount();
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_extract_element(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  llvm::Value *src = inst->getOperand(0);
  llvm::Value *index = inst->getOperand(1);

  auto *vec_ty = llvm::cast<llvm::FixedVectorType>(src->getType());
  unsigned nelem = vec_ty->getNumElements();
  assert((nelem & (nelem - 1)) == 0 && "vector nelem must be power of two");
  assert(index->getType()->getIntegerBitWidth() >= 8);

  ValuePartRef result = this->result_ref_lazy(inst_idx, 0);
  LLVMBasicValType bvt = this->adaptor->values[inst_idx].type;

  ScratchReg scratch_res{this};
  if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(index)) {
    unsigned cidx = ci->getZExtValue();
    derived()->extract_element(llvm_val_idx(src), cidx, bvt, scratch_res);

    (void)this->val_ref(llvm_val_idx(src), 0); // ref-counting
    this->set_value(result, scratch_res);
    return true;
  }

  // TODO: deduplicate with code above somehow?
  // First, copy value into the spill slot.
  ValuePartRef vec_ref = this->val_ref(llvm_val_idx(src), 0);
  vec_ref.spill();
  vec_ref.unlock();

  // Second, create address. Mask index, out-of-bounds access are just poison.
  ScratchReg idx_scratch{this};
  GenericValuePart addr = derived()->val_spill_slot(vec_ref);
  auto &expr = std::get<typename GenericValuePart::Expr>(addr.state);
  derived()->encode_landi64(this->val_ref(llvm_val_idx(index), 0),
                            typename Derived::EncodeImm{u64{nelem - 1}},
                            idx_scratch);
  assert(expr.scale == 0);
  expr.scale = derived()->val_part_size(inst_idx, 0);
  expr.index = std::move(idx_scratch);

  // Third, do the load.
  switch (bvt) {
    using enum LLVMBasicValType;
  case i8: derived()->encode_loadi8(std::move(addr), scratch_res); break;
  case i16: derived()->encode_loadi16(std::move(addr), scratch_res); break;
  case i32: derived()->encode_loadi32(std::move(addr), scratch_res); break;
  case i64:
  case ptr: derived()->encode_loadi64(std::move(addr), scratch_res); break;
  case f32: derived()->encode_loadf32(std::move(addr), scratch_res); break;
  case f64: derived()->encode_loadf64(std::move(addr), scratch_res); break;
  default: TPDE_UNREACHABLE("unexpected vector element type");
  }

  this->set_value(result, scratch_res);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_insert_element(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  llvm::Value *index = inst->getOperand(2);

  auto *vec_ty = llvm::cast<llvm::FixedVectorType>(inst->getType());
  unsigned nelem = vec_ty->getNumElements();
  assert((nelem & (nelem - 1)) == 0 && "vector nelem must be power of two");
  assert(index->getType()->getIntegerBitWidth() >= 8);

  auto ins_idx = llvm_val_idx(inst->getOperand(1));
  ValuePartRef val = this->val_ref(ins_idx, 0);

  ValuePartRef result = this->result_ref_lazy(inst_idx, 0);
  LLVMBasicValType bvt = this->adaptor->values[ins_idx].type; // insert type

  // We do the dynamic insert in the spill slot of result.
  // TODO: reuse spill slot of vec_ref if possible.

  // First, copy value into the spill slot. We must also do this for constant
  // indices, because the value reference must always be initialized.
  {
    ValuePartRef vec_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    ScratchReg tmp{this};
    AsmReg orig_reg = this->val_as_reg(vec_ref, tmp);
    if (vec_ref.can_salvage()) {
      tmp.alloc_specific(vec_ref.salvage());
      this->set_value(result, tmp);
    } else {
      // TODO: don't spill when target insert_element doesn't need it?
      unsigned frame_off = result.assignment().frame_off();
      unsigned part_size = result.assignment().part_size();
      derived()->spill_reg(orig_reg, frame_off, part_size);
    }
  }

  if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(index)) {
    unsigned cidx = ci->getZExtValue();
    derived()->insert_element(inst_idx, cidx, bvt, std::move(val));
    // No need for ref counting: all operands and results were ValuePartRefs.
    return true;
  }

  result.spill();
  result.unlock();
  if (result.assignment().register_valid()) {
    result.assignment().set_register_valid(false);
    this->register_file.unmark_used(AsmReg{result.assignment().full_reg_id()});
  }

  // Second, create address. Mask index, out-of-bounds access are just poison.
  ScratchReg idx_scratch{this};
  GenericValuePart addr = derived()->val_spill_slot(result);
  auto &expr = std::get<typename GenericValuePart::Expr>(addr.state);
  derived()->encode_landi64(this->val_ref(llvm_val_idx(index), 0),
                            typename Derived::EncodeImm{u64{nelem - 1}},
                            idx_scratch);
  assert(expr.scale == 0);
  expr.scale = val.part_size();
  expr.index = std::move(idx_scratch);

  // Third, do the store.
  switch (bvt) {
    using enum LLVMBasicValType;
  case i8: derived()->encode_storei8(std::move(addr), std::move(val)); break;
  case i16: derived()->encode_storei16(std::move(addr), std::move(val)); break;
  case i32: derived()->encode_storei32(std::move(addr), std::move(val)); break;
  case i64:
  case ptr: derived()->encode_storei64(std::move(addr), std::move(val)); break;
  case f32: derived()->encode_storef32(std::move(addr), std::move(val)); break;
  case f64: derived()->encode_storef64(std::move(addr), std::move(val)); break;
  default: TPDE_UNREACHABLE("unexpected vector element type");
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_cmpxchg(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *cmpxchg = llvm::cast<llvm::AtomicCmpXchgInst>(inst);

  const auto succ_order = cmpxchg->getSuccessOrdering();
  const auto fail_order = cmpxchg->getFailureOrdering();

  // ptr, cmp, new_val, old_val, success
  bool (Derived::*encode_ptr)(GenericValuePart,
                              GenericValuePart,
                              GenericValuePart,
                              ScratchReg &,
                              ScratchReg &) = nullptr;

  if (succ_order == llvm::AtomicOrdering::Monotonic) {
    assert(fail_order == llvm::AtomicOrdering::Monotonic);
    encode_ptr = &Derived::encode_cmpxchg_u64_monotonic_monotonic;
  } else if (succ_order == llvm::AtomicOrdering::Acquire) {
    if (fail_order == llvm::AtomicOrdering::Acquire) {
      encode_ptr = &Derived::encode_cmpxchg_u64_acquire_acquire;
    } else {
      assert(fail_order == llvm::AtomicOrdering::Monotonic);
      encode_ptr = &Derived::encode_cmpxchg_u64_acquire_monotonic;
    }
  } else if (succ_order == llvm::AtomicOrdering::Release) {
    if (fail_order == llvm::AtomicOrdering::Acquire) {
      encode_ptr = &Derived::encode_cmpxchg_u64_release_acquire;
    } else {
      assert(fail_order == llvm::AtomicOrdering::Monotonic);
      encode_ptr = &Derived::encode_cmpxchg_u64_release_monotonic;
    }
  } else if (succ_order == llvm::AtomicOrdering::AcquireRelease) {
    if (fail_order == llvm::AtomicOrdering::Acquire) {
      encode_ptr = &Derived::encode_cmpxchg_u64_acqrel_acquire;
    } else {
      assert(fail_order == llvm::AtomicOrdering::Monotonic);
      encode_ptr = &Derived::encode_cmpxchg_u64_acqrel_monotonic;
    }
  } else if (succ_order == llvm::AtomicOrdering::SequentiallyConsistent) {
    if (fail_order == llvm::AtomicOrdering::SequentiallyConsistent) {
      encode_ptr = &Derived::encode_cmpxchg_u64_seqcst_seqcst;
    } else if (fail_order == llvm::AtomicOrdering::Acquire) {
      encode_ptr = &Derived::encode_cmpxchg_u64_seqcst_acquire;
    } else {
      assert(fail_order == llvm::AtomicOrdering::Monotonic);
      encode_ptr = &Derived::encode_cmpxchg_u64_seqcst_monotonic;
    }
  }

  auto *ptr_val = cmpxchg->getPointerOperand();
  assert(ptr_val->getType()->isPointerTy());
  auto ptr_ref = this->val_ref(llvm_val_idx(ptr_val), 0);

  auto *cmp_val = cmpxchg->getCompareOperand();
  auto *new_val = cmpxchg->getNewValOperand();

  assert(new_val->getType()->isIntegerTy(64) ||
         new_val->getType()->isPointerTy());

  auto cmp_ref = this->val_ref(llvm_val_idx(cmp_val), 0);
  auto new_ref = this->val_ref(llvm_val_idx(new_val), 0);

  auto res_ref = this->result_ref_lazy(inst_idx, 0);
  res_ref.inc_ref_count();
  auto res_ref_high = this->result_ref_lazy(inst_idx, 1);

  ScratchReg orig_scratch{derived()};
  ScratchReg succ_scratch{derived()};

  assert(encode_ptr != nullptr);
  if (!(derived()->*encode_ptr)(std::move(ptr_ref),
                                std::move(cmp_ref),
                                std::move(new_ref),
                                orig_scratch,
                                succ_scratch)) {
    return false;
  }

  this->set_value(res_ref, orig_scratch);
  this->set_value(res_ref, succ_scratch);

  // clang-format off
    // TODO(ts): fusing with subsequent extractvalues + br's
    // e.g. clang generates
    // %4 = cmpxchg ptr %0, i64 %3, i64 1 seq_cst seq_cst, align 8
    // %5 = extractvalue { i64, i1 } %4, 1
    // %6 = extractvalue { i64, i1 } %4, 0
    // br i1 %5, label %7, label %2, !llvm.loop !3
  // clang-format on

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_phi(
    IRValueRef inst_idx, llvm::Instruction *) noexcept {
  // need to just ref-count the value
  this->val_ref(inst_idx, 0);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_freeze(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  // essentially a no-op
  auto *src_val = inst->getOperand(0);
  const auto src_idx = llvm_val_idx(src_val);

  const auto part_count = derived()->val_part_count(src_idx);

  for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
    const auto last_part = (part_idx == part_count - 1);
    auto src_ref = this->val_ref(src_idx, part_idx);
    if (!last_part) {
      src_ref.inc_ref_count();
    }
    AsmReg orig;
    auto res_ref = this->result_ref_salvage_with_original(
        inst_idx, part_idx, std::move(src_ref), orig, last_part ? 1 : 2);
    if (orig != res_ref.cur_reg()) {
      derived()->mov(res_ref.cur_reg(), orig, res_ref.part_size());
    }
    this->set_value(res_ref, res_ref.cur_reg());

    if (!last_part) {
      res_ref.reset_without_refcount();
    }
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_call(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *call = llvm::cast<llvm::CallBase>(inst);

  std::variant<SymRef, ValuePartRef> call_target;
  auto var_arg = false;

  if (auto *fn = call->getCalledFunction(); fn) {
    if (fn->isIntrinsic()) {
      return compile_intrin(inst_idx, inst, fn);
    }

    // this is a direct call
    call_target = global_sym(fn);
    var_arg = fn->getFunctionType()->isVarArg();
  } else if (call->isInlineAsm()) {
    // TODO: handle inline assembly
    return false;
  } else {
    // either indirect call or call with mismatch of arguments
    var_arg = call->getFunctionType()->isVarArg();
    auto *op = call->getCalledOperand();
    if (auto *fn = llvm::dyn_cast<llvm::Function>(op); fn) {
      call_target = global_sym(fn);
    } else if (auto *ga = llvm::dyn_cast<llvm::GlobalAlias>(op); fn) {
      // aliases also show up here
      // TODO(ts): do we need to check if the alias target is a function
      // and not a variable?
      call_target = global_sym(ga);
    } else {
      // indirect call
      auto target_ref = this->val_ref(llvm_val_idx(op), 0);
      call_target = std::move(target_ref);
    }
  }

  return derived()->compile_call_inner(inst_idx, call, call_target, var_arg);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_select(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto cond = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
  auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
  auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(2)), 0);

  ScratchReg res_scratch{derived()};
  auto res_ref = this->result_ref_lazy(inst_idx, 0);

  switch (this->adaptor->values[inst_idx].type) {
    using enum LLVMBasicValType;
  case i1:
  case i8:
  case i16:
  case i32:
    derived()->encode_select_i32(
        std::move(cond), std::move(lhs), std::move(rhs), res_scratch);
    break;
  case i64:
  case ptr:
    derived()->encode_select_i64(
        std::move(cond), std::move(lhs), std::move(rhs), res_scratch);
    break;
  case f32:
  case v32:
    derived()->encode_select_f32(
        std::move(cond), std::move(lhs), std::move(rhs), res_scratch);
    break;
  case f64:
  case v64:
    derived()->encode_select_f64(
        std::move(cond), std::move(lhs), std::move(rhs), res_scratch);
    break;
  case v128:
    derived()->encode_select_v2u64(
        std::move(cond), std::move(lhs), std::move(rhs), res_scratch);
    break;
  case complex:
    // Handle case of complex with two i64 as i128, this is extremely hacky...
    // TODO(ts): support full complex types using branches
    if (derived()->val_part_count(inst_idx) != 2 ||
        derived()->val_part_bank(inst_idx, 0) != 0 ||
        derived()->val_part_bank(inst_idx, 1) != 0) {
      return false;
    }
    [[fallthrough]];
  case i128: {
    lhs.inc_ref_count();
    rhs.inc_ref_count();
    auto lhs_high = this->val_ref(llvm_val_idx(inst->getOperand(1)), 1);
    auto rhs_high = this->val_ref(llvm_val_idx(inst->getOperand(2)), 1);

    ScratchReg res_scratch_high{derived()};
    auto res_ref_high = this->result_ref_lazy(inst_idx, 1);

    derived()->encode_select_i128(std::move(cond),
                                  std::move(lhs),
                                  std::move(lhs_high),
                                  std::move(rhs),
                                  std::move(rhs_high),
                                  res_scratch,
                                  res_scratch_high);
    this->set_value(res_ref_high, res_scratch_high);
    res_ref_high.reset_without_refcount();
    break;
  }
  default: TPDE_UNREACHABLE("invalid select basic type"); break;
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_gep(
    IRValueRef inst_idx,
    llvm::Instruction *inst,
    InstRange remaining) noexcept {
  auto *gep = llvm::cast<llvm::GetElementPtrInst>(inst);

  auto ptr = gep->getPointerOperand();
  tpde::util::SmallVector<llvm::Value *, 8> indices;
  std::optional<IRValueRef> variable_off = {};

  if (gep->hasIndices()) {
    auto idx_begin = gep->idx_begin(), idx_end = gep->idx_end();
    if (llvm::dyn_cast<llvm::Constant>(idx_begin->get()) == nullptr) {
      variable_off = llvm_val_idx(idx_begin->get());
      indices.push_back(llvm::ConstantInt::get(
          llvm::IntegerType::getInt64Ty(this->adaptor->context), 0));
      ++idx_begin;
    }

    for (auto it = idx_begin; it != idx_end; ++it) {
      assert(llvm::dyn_cast<llvm::Constant>(it->get()) != nullptr);
      indices.push_back(it->get());
    }
  }

  struct GEPTypeRange {
    llvm::Type *ty;
    uint32_t start;
    uint32_t end;
  };

  tpde::util::SmallVector<GEPTypeRange, 8> types;
  types.push_back({.ty = gep->getSourceElementType(),
                   .start = 0,
                   .end = (u32)indices.size()});

  // fuse geps
  // TODO: use llvm statistic or analyzer liveness stat?
  auto final_idx = inst_idx;
  while (gep->hasOneUser() && remaining.from != remaining.to) {
    auto next_inst_idx = *remaining.from;
    auto *next_val = this->adaptor->values[next_inst_idx].val;
    auto *next_gep = llvm::dyn_cast<llvm::GetElementPtrInst>(next_val);
    if (!next_gep || next_gep->getPointerOperand() != gep ||
        gep->getResultElementType() != next_gep->getResultElementType() ||
        !next_gep->hasAllConstantIndices()) {
      break;
    }

    if (next_gep->hasIndices()) {
      // TODO: we should be able to fuse as long as this is a constant int
      auto *idx = llvm::cast<llvm::ConstantInt>(next_gep->idx_begin()->get());
      if (!idx->isZero()) {
        break;
      }

      u32 start = indices.size();
      indices.append(next_gep->idx_begin(), next_gep->idx_end());
      types.push_back({.ty = next_gep->getSourceElementType(),
                       .start = start,
                       .end = (u32)indices.size()});
    }

    this->adaptor->val_set_fused(next_inst_idx, true);
    final_idx = next_inst_idx; // we set the result for nextInst
    gep = next_gep;
    ++remaining.from;
  }

  auto resolved = ResolvedGEP{};
  resolved.base = this->val_ref(llvm_val_idx(ptr), 0);

  auto &data_layout = this->adaptor->mod.getDataLayout();

  if (variable_off) {
    resolved.index = this->val_ref(*variable_off, 0);
    const auto base_ty_size = data_layout.getTypeAllocSize(types[0].ty);
    resolved.scale = base_ty_size;
  }

  i64 disp = 0;
  for (auto &[ty, start, end] : types) {
    if (start == end) {
      continue;
    }

    assert(start < indices.size());
    assert(end <= indices.size());
    disp += data_layout.getIndexedOffsetInType(
        ty,
        llvm::ArrayRef<llvm::Value *>{indices.data() + start,
                                      indices.data() + end});
  }
  resolved.displacement = disp;

  GenericValuePart addr = derived()->resolved_gep_to_addr(resolved);

  if (gep->hasOneUser() && remaining.from != remaining.to) {
    auto next_inst_idx = *remaining.from;
    auto *next_val = this->adaptor->values[next_inst_idx].val;
    if (auto *store = llvm::dyn_cast<llvm::StoreInst>(next_val);
        store && store->getPointerOperand() == gep) {
      this->adaptor->val_set_fused(next_inst_idx, true);
      return compile_store_generic(store, std::move(addr));
    }
    if (auto *load = llvm::dyn_cast<llvm::LoadInst>(next_val);
        load && load->getPointerOperand() == gep) {
      this->adaptor->val_set_fused(next_inst_idx, true);
      return compile_load_generic(next_inst_idx, load, std::move(addr));
    }
  }

  auto res_ref = this->result_ref_lazy(final_idx, 0);

  AsmReg res_reg = derived()->gval_as_reg(addr);
  if (auto *op_reg = std::get_if<ScratchReg>(&addr.state)) {
    ScratchReg res_scratch = std::move(*op_reg);
    this->set_value(res_ref, res_scratch);
  } else {
    ScratchReg res_scratch{derived()};
    derived()->mov(res_scratch.alloc_gp(), res_reg, 8);
    this->set_value(res_ref, res_scratch);
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_fcmp(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *cmp = llvm::cast<llvm::FCmpInst>(inst);
  auto *cmp_ty = cmp->getOperand(0)->getType();
  const auto pred = cmp->getPredicate();

  if (pred == llvm::CmpInst::FCMP_FALSE || pred == llvm::CmpInst::FCMP_TRUE) {
    auto res_ref = this->result_ref_eager(inst_idx, 0);
    auto const_ref = ValuePartRef{
        (pred == llvm::CmpInst::FCMP_FALSE ? 0u : 1u), Config::GP_BANK, 1};
    derived()->materialize_constant(const_ref, res_ref.cur_reg());
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }

  if (cmp_ty->isFP128Ty()) {
    SymRef sym = Assembler::INVALID_SYM_REF;
    llvm::CmpInst::Predicate cmp_pred = llvm::CmpInst::ICMP_EQ;
    switch (pred) {
    case llvm::CmpInst::FCMP_OEQ:
      sym = get_or_create_sym_ref(sym_eqtf2, "__eqtf2");
      cmp_pred = llvm::CmpInst::ICMP_EQ;
      break;
    case llvm::CmpInst::FCMP_UNE:
      sym = get_or_create_sym_ref(sym_netf2, "__netf2");
      cmp_pred = llvm::CmpInst::ICMP_NE;
      break;
    case llvm::CmpInst::FCMP_OGT:
      sym = get_or_create_sym_ref(sym_gttf2, "__gttf2");
      cmp_pred = llvm::CmpInst::ICMP_SGT;
      break;
    case llvm::CmpInst::FCMP_ULE:
      sym = get_or_create_sym_ref(sym_gttf2, "__gttf2");
      cmp_pred = llvm::CmpInst::ICMP_SLE;
      break;
    case llvm::CmpInst::FCMP_OGE:
      sym = get_or_create_sym_ref(sym_getf2, "__getf2");
      cmp_pred = llvm::CmpInst::ICMP_SGE;
      break;
    case llvm::CmpInst::FCMP_ULT:
      sym = get_or_create_sym_ref(sym_getf2, "__getf2");
      cmp_pred = llvm::CmpInst::ICMP_SLT;
      break;
    case llvm::CmpInst::FCMP_OLT:
      sym = get_or_create_sym_ref(sym_lttf2, "__lttf2");
      cmp_pred = llvm::CmpInst::ICMP_SLT;
      break;
    case llvm::CmpInst::FCMP_UGE:
      sym = get_or_create_sym_ref(sym_lttf2, "__lttf2");
      cmp_pred = llvm::CmpInst::ICMP_SGE;
      break;
    case llvm::CmpInst::FCMP_OLE:
      sym = get_or_create_sym_ref(sym_letf2, "__letf2");
      cmp_pred = llvm::CmpInst::ICMP_SLE;
      break;
    case llvm::CmpInst::FCMP_UGT:
      sym = get_or_create_sym_ref(sym_letf2, "__letf2");
      cmp_pred = llvm::CmpInst::ICMP_SGT;
      break;
    case llvm::CmpInst::FCMP_ORD:
      sym = get_or_create_sym_ref(sym_unordtf2, "__unordtf2");
      cmp_pred = llvm::CmpInst::ICMP_EQ;
      break;
    case llvm::CmpInst::FCMP_UNO:
      sym = get_or_create_sym_ref(sym_unordtf2, "__unordtf2");
      cmp_pred = llvm::CmpInst::ICMP_NE;
      break;
    case llvm::CmpInst::FCMP_ONE:
    case llvm::CmpInst::FCMP_UEQ:
      // TODO: implement fp128 fcmp one/ueq
      // ONE __unordtf2 == 0 && __eqtf2 != 0
      // UEQ __unordtf2 != 0 || __eqtf2 == 0
      return false;
    default: assert(0 && "unexpected fcmp predicate");
    }

    IRValueRef lhs = llvm_val_idx(cmp->getOperand(0));
    IRValueRef rhs = llvm_val_idx(cmp->getOperand(1));
    std::array<IRValueRef, 2> args{lhs, rhs};

    ValuePartRef res_ref = this->result_ref_lazy(inst_idx, 0);
    derived()->create_helper_call(args, {&res_ref, 1}, sym);

    ValuePartRef res_ref2 = this->val_ref(inst_idx, 0);
    res_ref2.inc_ref_count();
    auto dst_reg = res_ref2.cur_reg();
    // Stupid hack to do the actual comparison.
    // TODO: do a proper comparison here.
    derived()->compile_i32_cmp_zero(dst_reg, cmp_pred);
    this->set_value(res_ref2, dst_reg);

    return true;
  }

  if (!cmp_ty->isFloatTy() && !cmp_ty->isDoubleTy()) {
    return false;
  }

  using EncodeFnTy =
      bool (Derived::*)(GenericValuePart, GenericValuePart, ScratchReg &);
  EncodeFnTy fn = nullptr;

  if (cmp_ty->isFloatTy()) {
    switch (pred) {
      using enum llvm::CmpInst::Predicate;
    case FCMP_OEQ: fn = &Derived::encode_fcmp_oeq_float; break;
    case FCMP_OGT: fn = &Derived::encode_fcmp_ogt_float; break;
    case FCMP_OGE: fn = &Derived::encode_fcmp_oge_float; break;
    case FCMP_OLT: fn = &Derived::encode_fcmp_olt_float; break;
    case FCMP_OLE: fn = &Derived::encode_fcmp_ole_float; break;
    case FCMP_ONE: fn = &Derived::encode_fcmp_one_float; break;
    case FCMP_ORD: fn = &Derived::encode_fcmp_ord_float; break;
    case FCMP_UEQ: fn = &Derived::encode_fcmp_ueq_float; break;
    case FCMP_UGT: fn = &Derived::encode_fcmp_ugt_float; break;
    case FCMP_UGE: fn = &Derived::encode_fcmp_uge_float; break;
    case FCMP_ULT: fn = &Derived::encode_fcmp_ult_float; break;
    case FCMP_ULE: fn = &Derived::encode_fcmp_ule_float; break;
    case FCMP_UNE: fn = &Derived::encode_fcmp_une_float; break;
    case FCMP_UNO: fn = &Derived::encode_fcmp_uno_float; break;
    default: TPDE_UNREACHABLE("invalid fcmp predicate");
    }
  } else {
    switch (pred) {
      using enum llvm::CmpInst::Predicate;
    case FCMP_OEQ: fn = &Derived::encode_fcmp_oeq_double; break;
    case FCMP_OGT: fn = &Derived::encode_fcmp_ogt_double; break;
    case FCMP_OGE: fn = &Derived::encode_fcmp_oge_double; break;
    case FCMP_OLT: fn = &Derived::encode_fcmp_olt_double; break;
    case FCMP_OLE: fn = &Derived::encode_fcmp_ole_double; break;
    case FCMP_ONE: fn = &Derived::encode_fcmp_one_double; break;
    case FCMP_ORD: fn = &Derived::encode_fcmp_ord_double; break;
    case FCMP_UEQ: fn = &Derived::encode_fcmp_ueq_double; break;
    case FCMP_UGT: fn = &Derived::encode_fcmp_ugt_double; break;
    case FCMP_UGE: fn = &Derived::encode_fcmp_uge_double; break;
    case FCMP_ULT: fn = &Derived::encode_fcmp_ult_double; break;
    case FCMP_ULE: fn = &Derived::encode_fcmp_ule_double; break;
    case FCMP_UNE: fn = &Derived::encode_fcmp_une_double; break;
    case FCMP_UNO: fn = &Derived::encode_fcmp_uno_double; break;
    default: TPDE_UNREACHABLE("invalid fcmp predicate");
    }
  }

  GenericValuePart lhs_op = this->val_ref(llvm_val_idx(cmp->getOperand(0)), 0);
  GenericValuePart rhs_op = this->val_ref(llvm_val_idx(cmp->getOperand(1)), 0);
  ScratchReg res_scratch{derived()};
  auto res_ref = this->result_ref_lazy(inst_idx, 0);

  if (!(derived()->*fn)(std::move(lhs_op), std::move(rhs_op), res_scratch)) {
    return false;
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_switch(
    IRValueRef, llvm::Instruction *inst) noexcept {
  using EncodeImm = typename Derived::EncodeImm;
  auto *switch_inst = llvm::cast<llvm::SwitchInst>(inst);

  ScratchReg scratch{this};
  AsmReg cmp_reg;
  bool width_is_32 = false;
  {
    auto arg_ref = this->val_ref(llvm_val_idx(switch_inst->getCondition()), 0);
    auto *arg_ty = switch_inst->getCondition()->getType();
    assert(arg_ty->isIntegerTy() || arg_ty->isPointerTy());
    u32 width = 64;
    if (arg_ty->isIntegerTy()) {
      width = arg_ty->getIntegerBitWidth();
      assert(width <= 64);
    }

    if (width < 32) {
      width_is_32 = true;
      if (width == 8) {
        derived()->encode_zext_8_to_32(std::move(arg_ref), scratch);
      } else if (width == 16) {
        derived()->encode_zext_16_to_32(std::move(arg_ref), scratch);
      } else {
        u32 mask = (1ull << width) - 1;
        derived()->encode_landi32(std::move(arg_ref), EncodeImm{mask}, scratch);
      }
      cmp_reg = scratch.cur_reg;
    } else if (width == 32) {
      width_is_32 = true;
      cmp_reg = this->val_as_reg(arg_ref, scratch);
      // make sure we can overwrite the register when we generate a jump
      // table
      if (arg_ref.can_salvage()) {
        scratch.alloc_specific(arg_ref.salvage());
      } else if (!arg_ref.is_const) {
        arg_ref.unlock();
        arg_ref.reload_into_specific_fixed(this, scratch.alloc_gp());
        cmp_reg = scratch.cur_reg;
      }
    } else if (width < 64) {
      u64 mask = (1ull << width) - 1;
      derived()->encode_landi64(std::move(arg_ref), EncodeImm{mask}, scratch);
      cmp_reg = scratch.cur_reg;
    } else {
      cmp_reg = this->val_as_reg(arg_ref, scratch);
      // make sure we can overwrite the register when we generate a jump
      // table
      if (arg_ref.can_salvage()) {
        scratch.alloc_specific(arg_ref.salvage());
      } else if (!arg_ref.is_const) {
        arg_ref.unlock();
        arg_ref.reload_into_specific_fixed(this, scratch.alloc_gp());
        cmp_reg = scratch.cur_reg;
      }
    }
  }

  const auto spilled = this->spill_before_branch();

  // get all cases, their target block and sort them in ascending order
  tpde::util::SmallVector<std::pair<u64, IRBlockRef>, 64> cases;
  assert(switch_inst->getNumCases() <= 200000);
  cases.reserve(switch_inst->getNumCases());
  for (auto case_val : switch_inst->cases()) {
    cases.push_back(std::make_pair(
        case_val.getCaseValue()->getZExtValue(),
        this->adaptor->block_lookup[case_val.getCaseSuccessor()]));
  }
  std::sort(cases.begin(), cases.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.first < rhs.first;
  });

  // because some blocks might have PHI-values we need to first jump to a
  // label which then fixes the registers and then jumps to the block
  // TODO(ts): check which blocks need PHIs and otherwise jump directly to
  // them? probably better for branch predictor

  tpde::util::SmallVector<typename Assembler::Label, 64> case_labels;
  for (auto i = 0u; i < cases.size(); ++i) {
    case_labels.push_back(this->assembler.label_create());
  }

  const auto default_label = this->assembler.label_create();

  const auto build_range = [&,
                            this](size_t begin, size_t end, const auto &self) {
    assert(begin <= end);
    const auto num_cases = end - begin;
    if (num_cases <= 4) {
      // if there are four or less cases we just compare the values
      // against each of them
      for (auto i = 0u; i < num_cases; ++i) {
        derived()->switch_emit_cmpeq(case_labels[begin + i],
                                     cmp_reg,
                                     cases[begin + i].first,
                                     width_is_32);
      }

      derived()->generate_raw_jump(Derived::Jump::jmp, default_label);
      return;
    }

    // check if the density of the values is high enough to warrant building
    // a jump table
    auto range = cases[end - 1].first - cases[begin].first;
    // we will get wrong results if range is -1 so skip the jump table if
    // that is the case
    if (range != 0xFFFF'FFFF'FFFF'FFFF && (range / num_cases) < 8) {
      // for gcc, it seems that if there are less than 8 values per
      // case it will build a jump table so we do that, too

      // the actual range is one greater than the result we get from
      // subtracting so adjust for that
      range += 1;

      tpde::util::SmallVector<typename Assembler::Label, 32> label_vec;
      std::span<typename Assembler::Label> labels;
      if (range == num_cases) {
        labels = std::span{case_labels.begin() + begin, num_cases};
      } else {
        label_vec.resize(range, default_label);
        for (auto i = 0u; i < num_cases; ++i) {
          label_vec[cases[begin + i].first - cases[begin].first] =
              case_labels[begin + i];
        }
        labels = std::span{label_vec.begin(), range};
      }

      // Give target the option to emit a jump table.
      if (derived()->switch_emit_jump_table(default_label,
                                            labels,
                                            cmp_reg,
                                            cases[begin].first,
                                            cases[end - 1].first,
                                            width_is_32)) {
        return;
      }
    }

    // do a binary search step
    const auto half_len = num_cases / 2;
    const auto half_value = cases[begin + half_len].first;
    const auto gt_label = this->assembler.label_create();

    // this will cmp against the input value, jump to the case if it is
    // equal or to gt_label if the value is greater. Otherwise it will
    // fall-through
    derived()->switch_emit_binary_step(case_labels[begin + half_len],
                                       gt_label,
                                       cmp_reg,
                                       half_value,
                                       width_is_32);
    // search the lower half
    self(begin, begin + half_len, self);

    // and the upper half
    this->assembler.label_place(gt_label);
    self(begin + half_len + 1, end, self);
  };

  build_range(0, case_labels.size(), build_range);

  // write out the labels
  this->assembler.label_place(default_label);
  // TODO(ts): factor into arch-code?
  derived()->generate_branch_to_block(
      Derived::Jump::jmp,
      this->adaptor->block_lookup[switch_inst->getDefaultDest()],
      false,
      false);

  for (auto i = 0u; i < cases.size(); ++i) {
    this->assembler.label_place(case_labels[i]);
    derived()->generate_branch_to_block(
        Derived::Jump::jmp, cases[i].second, false, false);
  }

  this->release_spilled_regs(spilled);

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_invoke(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  auto *invoke = llvm::cast<llvm::InvokeInst>(inst);

  // we need to spill here since the call might branch off
  // TODO: this will also spill the call arguments even if the call kills them
  // however, spillBeforeCall already does this anyways so probably something
  // for later
  auto spilled = this->spill_before_branch();

  const auto off_before_call = this->assembler.text_cur_off();
  // compile the call
  // TODO: in the case of an exception we need to invalidate the result
  // registers
  // TODO: if the call needs stack space, this must be undone in the unwind
  // block! LLVM emits .cfi_escape 0x2e, <off>, we should do the same?
  // (Current workaround by treating invoke as dynamic alloca.)
  if (!this->compile_call(inst_idx, inst)) {
    return false;
  }
  const auto off_after_call = this->assembler.text_cur_off();

  // build the eh table
  auto *unwind_block = invoke->getUnwindDest();
  llvm::LandingPadInst *landing_pad = nullptr;
  auto unwind_block_has_phi = false;

  for (auto it = unwind_block->begin(), end = unwind_block->end(); it != end;
       ++it) {
    llvm::Instruction *inst = &*it;
    if (llvm::isa<llvm::PHINode>(inst)) {
      unwind_block_has_phi = true;
      continue;
    }

    landing_pad = llvm::cast<llvm::LandingPadInst>(inst);
    break;
  }

  const auto unwind_block_ref = this->adaptor->block_lookup[unwind_block];
  const auto normal_block_ref =
      this->adaptor->block_lookup[invoke->getNormalDest()];
  auto unwind_label =
      this->block_labels[(u32)this->analyzer.block_idx(unwind_block_ref)];

  // we need to check whether the call result needs to be spilled, too.
  // This needs to be done since the invoke is conceptually a branch
  auto check_res_spill = [&]() {
    auto call_res_idx = llvm_val_idx(invoke);
    auto *a = this->val_assignment(this->val_idx(call_res_idx));
    if (a == nullptr) {
      // call has void result
      return;
    }

    auto cur_block = this->analyzer.block_ref(this->cur_block_idx);

    if (this->analyzer.block_ref(this->next_block()) == normal_block_ref &&
        !unwind_block_has_phi) {
      // this block will handle the spilling for us, the value is not
      // usable in unwind block. Always spill if the unwind block has phi nodes,
      // because the phi handling will modify the register states.
      return;
    }

    uint32_t num_phi_reads = 0;
    for (auto i : this->adaptor->block_phis(normal_block_ref)) {
      assert(this->adaptor->val_is_phi(i));
      auto phi = this->adaptor->val_as_phi(i);

      auto incoming_val = phi.incoming_val_for_block(cur_block);
      if (incoming_val == call_res_idx) {
        ++num_phi_reads;
      }
    }

    auto ap = AssignmentPartRef{a, 0};
    // no need to spill if only the phi-nodes in the (normal) successor read
    // the value
    if (a->references_left <= num_phi_reads &&
        (u32)this->analyzer.liveness_info(call_res_idx).last <=
            (u32)this->cur_block_idx) {
      return;
    }

    // spill
    // no need to spill fixed assignments
    while (true) {
      if (!ap.fixed_assignment() && ap.register_valid()) {
        // this is the call result...
        assert(ap.modified());
        ap.spill_if_needed(this);
        spilled |= 1ull << ap.full_reg_id();
        this->register_file.unmark_used(AsmReg{ap.full_reg_id()});
        ap.set_register_valid(false);
      }
      if (!ap.has_next_part()) {
        break;
      }
      ++ap.part;
    }
  };
  check_res_spill();

  // if the unwind block has phi-nodes, we need more code to propagate values
  // to it so do the propagation logic
  if (unwind_block_has_phi) {
    // generate the jump to the normal successor but don't allow
    // fall-through
    derived()->generate_branch_to_block(Derived::Jump::jmp,
                                        normal_block_ref,
                                        /* split */ false,
                                        /* last_inst */ false);

    unwind_label = this->assembler.label_create();
    this->assembler.label_place(unwind_label);

    // allocate the special registers that are set by the unwinding logic
    // so the phi-propagation does not use them as temporaries
    ScratchReg scratch1{derived()}, scratch2{derived()};
    assert(!this->register_file.is_used(Derived::LANDING_PAD_RES_REGS[0]));
    assert(!this->register_file.is_used(Derived::LANDING_PAD_RES_REGS[1]));
    scratch1.alloc_specific(Derived::LANDING_PAD_RES_REGS[0]);
    scratch2.alloc_specific(Derived::LANDING_PAD_RES_REGS[1]);

    derived()->generate_branch_to_block(Derived::Jump::jmp,
                                        unwind_block_ref,
                                        /* split */ false,
                                        /* last_inst */ false);
  } else {
    // allow fall-through
    derived()->generate_branch_to_block(Derived::Jump::jmp,
                                        normal_block_ref,
                                        /* split */ false,
                                        /* last_inst */ true);
  }

  this->release_spilled_regs(spilled);

  const auto is_cleanup = landing_pad->isCleanup();
  const auto num_clauses = landing_pad->getNumClauses();
  const auto only_cleanup = is_cleanup && num_clauses == 0;

  this->assembler.except_add_call_site(off_before_call,
                                       off_after_call - off_before_call,
                                       static_cast<u32>(unwind_label),
                                       only_cleanup);

  if (only_cleanup) {
    // no clause so we are done
    return true;
  }

  for (auto i = 0u; i < num_clauses; ++i) {
    if (landing_pad->isCatch(i)) {
      auto *C = landing_pad->getClause(i);
      SymRef sym = Assembler::INVALID_SYM_REF;
      if (!C->isNullValue()) {
        assert(llvm::dyn_cast<llvm::GlobalValue>(C));
        sym = lookup_type_info_sym(llvm_val_idx(C));
      }
      this->assembler.except_add_action(i == 0, sym);
    } else {
      assert(landing_pad->isFilter(i));
      auto *C = landing_pad->getClause(i);
      assert(C->getType()->isArrayTy());
      if (C->getType()->getArrayNumElements() == 0) {
        this->assembler.except_add_empty_spec_action(i == 0);
      } else {
        TPDE_LOG_ERR("Exception filters with non-zero length arrays "
                     "not supported");
        return false;
      }
    }
  }

  if (is_cleanup) {
    assert(num_clauses != 0);
    this->assembler.except_add_cleanup_action();
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_landing_pad(
    IRValueRef inst_idx, llvm::Instruction *) noexcept {
  auto res_ref_first = this->result_ref_lazy(inst_idx, 0);
  auto res_ref_second = this->result_ref_lazy(inst_idx, 1);

  this->set_value(res_ref_first, Derived::LANDING_PAD_RES_REGS[0]);
  this->set_value(res_ref_second, Derived::LANDING_PAD_RES_REGS[1]);

  res_ref_second.reset_without_refcount();
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_resume(
    IRValueRef, llvm::Instruction *inst) noexcept {
  IRValueRef arg = llvm_val_idx(inst->getOperand(0));

  const auto sym = get_or_create_sym_ref(sym_resume, "_Unwind_Resume");

  derived()->create_helper_call({&arg, 1}, {}, sym);
  return derived()->compile_unreachable(Adaptor::INVALID_VALUE_REF, nullptr);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::lookup_type_info_sym(
        IRValueRef value) noexcept {
  for (const auto &[val, sym] : type_info_syms) {
    if (val == value) {
      return sym;
    }
  }

  const auto sym = global_sym(
      llvm::dyn_cast<llvm::GlobalValue>(this->adaptor->values[value].val));

  u32 off;
  u8 tmp[8] = {};
  auto rodata = this->assembler.get_data_section(true, true);
  const auto addr_sym = this->assembler.sym_def_data(
      rodata, {}, {tmp, sizeof(tmp)}, 8, true, false, &off);
  this->assembler.reloc_abs(rodata, sym, off, 0);

  type_info_syms.emplace_back(value, addr_sym);
  return addr_sym;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_intrin(
    IRValueRef inst_idx,
    llvm::Instruction *inst,
    llvm::Function *intrin) noexcept {
  auto *intrinsic = llvm::cast<llvm::IntrinsicInst>(inst);
  const auto intrin_id = intrin->getIntrinsicID();

  switch (intrin_id) {
  case llvm::Intrinsic::donothing:
  case llvm::Intrinsic::sideeffect:
  case llvm::Intrinsic::experimental_noalias_scope_decl:
  case llvm::Intrinsic::dbg_assign:
  case llvm::Intrinsic::dbg_declare:
  case llvm::Intrinsic::dbg_label: return true;
  case llvm::Intrinsic::dbg_value:
    // reference counting
    this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    return true;
  case llvm::Intrinsic::assume:
  case llvm::Intrinsic::lifetime_start:
  case llvm::Intrinsic::lifetime_end:
  case llvm::Intrinsic::invariant_start:
  case llvm::Intrinsic::invariant_end:
    // reference counting
    for (llvm::Value *arg : intrinsic->args()) {
      this->val_ref(llvm_val_idx(arg), 0);
    }
    return true;
  case llvm::Intrinsic::memcpy: {
    const auto dst = llvm_val_idx(inst->getOperand(0));
    const auto src = llvm_val_idx(inst->getOperand(1));
    const auto len = llvm_val_idx(inst->getOperand(2));

    std::array<IRValueRef, 3> args{dst, src, len};

    const auto sym = get_or_create_sym_ref(sym_memcpy, "memcpy");
    derived()->create_helper_call(args, {}, sym);
    return true;
  }
  case llvm::Intrinsic::memset: {
    const auto dst = llvm_val_idx(inst->getOperand(0));
    const auto val = llvm_val_idx(inst->getOperand(1));
    const auto len = llvm_val_idx(inst->getOperand(2));

    std::array<IRValueRef, 3> args{dst, val, len};

    const auto sym = get_or_create_sym_ref(sym_memset, "memset");
    derived()->create_helper_call(args, {}, sym);
    return true;
  }
  case llvm::Intrinsic::memmove: {
    const auto dst = llvm_val_idx(inst->getOperand(0));
    const auto src = llvm_val_idx(inst->getOperand(1));
    const auto len = llvm_val_idx(inst->getOperand(2));

    std::array<IRValueRef, 3> args{dst, src, len};

    const auto sym = get_or_create_sym_ref(sym_memmove, "memmove");
    derived()->create_helper_call(args, {}, sym);
    return true;
  }
  case llvm::Intrinsic::load_relative: {
    if (!inst->getOperand(1)->getType()->isIntegerTy(64)) {
      return false;
    }

    auto ptr = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto off = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res{derived()};
    derived()->encode_loadreli64(std::move(ptr), std::move(off), res);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::vaend: {
    // no-op
    this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    return true;
  }
  case llvm::Intrinsic::is_fpclass: {
    return compile_is_fpclass(inst_idx, inst);
  }
  case llvm::Intrinsic::floor:
  case llvm::Intrinsic::ceil:
  case llvm::Intrinsic::round:
  case llvm::Intrinsic::trunc: {
    auto val = llvm_val_idx(inst->getOperand(0));

    const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    SymRef sym;
    if (intrin_id == llvm::Intrinsic::floor) {
      if (is_double) {
        sym = get_or_create_sym_ref(sym_floor, "floor");
      } else {
        sym = get_or_create_sym_ref(sym_floorf, "floorf");
      }
    } else if (intrin_id == llvm::Intrinsic::ceil) {
      if (is_double) {
        sym = get_or_create_sym_ref(sym_ceil, "ceil");
      } else {
        sym = get_or_create_sym_ref(sym_ceilf, "ceilf");
      }
    } else if (intrin_id == llvm::Intrinsic::round) {
      if (is_double) {
        sym = get_or_create_sym_ref(sym_round, "round");
      } else {
        sym = get_or_create_sym_ref(sym_roundf, "roundf");
      }
    } else {
      assert(intrin_id == llvm::Intrinsic::trunc);
      if (is_double) {
        sym = get_or_create_sym_ref(sym_trunc, "trunc");
      } else {
        sym = get_or_create_sym_ref(sym_truncf, "truncf");
      }
    }

    derived()->create_helper_call({&val, 1}, {&res_ref, 1}, sym);
    return true;
  }
  case llvm::Intrinsic::copysign: {
    auto *ty = inst->getType();
    if (!ty->isFloatTy() && !ty->isDoubleTy()) {
      return false;
    }

    auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res{derived()};

    if (ty->isDoubleTy()) {
      derived()->encode_copysignf64(std::move(lhs), std::move(rhs), res);
    } else {
      derived()->encode_copysignf32(std::move(lhs), std::move(rhs), res);
    }
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::fabs: {
    auto *val = inst->getOperand(0);
    auto *ty = val->getType();

    if (!ty->isFloatTy() && !ty->isDoubleTy()) {
      TPDE_LOG_ERR("only float/double supported for fabs");
      return false;
    }

    auto val_ref = this->val_ref(llvm_val_idx(val), 0);
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res_scratch{derived()};

    if (ty->isDoubleTy()) {
      derived()->encode_fabsf64(std::move(val_ref), res_scratch);
    } else {
      derived()->encode_fabsf32(std::move(val_ref), res_scratch);
    }
    this->set_value(res_ref, res_scratch);
    return true;
  }
  case llvm::Intrinsic::sqrt: {
    auto *val = inst->getOperand(0);
    auto *ty = val->getType();
    if (!ty->isFloatTy() && !ty->isDoubleTy()) {
      return false;
    }

    auto val_ref = this->val_ref(llvm_val_idx(val), 0);
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res_scratch{derived()};
    if (ty->isDoubleTy()) {
      derived()->encode_sqrtf64(std::move(val_ref), res_scratch);
    } else {
      derived()->encode_sqrtf32(std::move(val_ref), res_scratch);
    }
    this->set_value(res_ref, res_scratch);
    return true;
  }
  case llvm::Intrinsic::fmuladd: {
    auto op1_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto op2_ref = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    auto op3_ref = this->val_ref(llvm_val_idx(inst->getOperand(2)), 0);

    const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();

    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res_scratch{derived()};
    if (is_double) {
      derived()->encode_fmaf64(std::move(op1_ref),
                               std::move(op2_ref),
                               std::move(op3_ref),
                               res_scratch);
    } else {
      derived()->encode_fmaf32(std::move(op1_ref),
                               std::move(op2_ref),
                               std::move(op3_ref),
                               res_scratch);
    }
    this->set_value(res_ref, res_scratch);
    return true;
  }
  case llvm::Intrinsic::powi: {
    std::array<IRValueRef, 2> args = {
        {llvm_val_idx(inst->getOperand(0)), llvm_val_idx(inst->getOperand(1))}
    };

    auto *ty = inst->getOperand(0)->getType();
    if (!ty->isDoubleTy() && !ty->isFloatTy()) {
      return false;
    }
    const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();
    SymRef sym;
    if (is_double) {
      sym = get_or_create_sym_ref(sym_powidf2, "__powidf2");
    } else {
      sym = get_or_create_sym_ref(sym_powisf2, "__powisf2");
    }
    auto res_ref = this->result_ref_lazy(inst_idx, 0);

    derived()->create_helper_call(args, {&res_ref, 1}, sym);
    return true;
  }
  case llvm::Intrinsic::abs: {
    auto *val = inst->getOperand(0);
    auto *val_ty = val->getType();
    if (!val_ty->isIntegerTy()) {
      return false;
    }
    const auto width = val_ty->getIntegerBitWidth();
    if (width > 64) {
      return false;
    }

    GenericValuePart op = this->val_ref(llvm_val_idx(val), 0);
    if (width != 32 && width != 64) {
      unsigned dst_width = tpde::util::align_up(width, 32);
      op = derived()->ext_int(std::move(op), true, width, dst_width);
    }

    ScratchReg res{derived()};
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    if (width <= 32) {
      derived()->encode_absi32(std::move(op), res);
    } else {
      derived()->encode_absi64(std::move(op), res);
    }
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::umin:
  case llvm::Intrinsic::umax:
  case llvm::Intrinsic::smin:
  case llvm::Intrinsic::smax: {
    auto *ty = inst->getType();
    if (!ty->isIntegerTy()) {
      return false;
    }
    const auto width = ty->getIntegerBitWidth();
    if (width > 64) {
      return false;
    }

    GenericValuePart lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    GenericValuePart rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    if (width != 32 && width != 64) {
      unsigned dst_width = tpde::util::align_up(width, 32);
      lhs = derived()->ext_int(std::move(lhs), true, width, dst_width);
      rhs = derived()->ext_int(std::move(rhs), true, width, dst_width);
    }

    ScratchReg res{derived()};
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    using EncodeFnTy =
        bool (Derived::*)(GenericValuePart, GenericValuePart, ScratchReg &);
    EncodeFnTy encode_fn = nullptr;
    if (width <= 32) {
      switch (intrin_id) {
      case llvm::Intrinsic::umin: encode_fn = &Derived::encode_umini32; break;
      case llvm::Intrinsic::umax: encode_fn = &Derived::encode_umaxi32; break;
      case llvm::Intrinsic::smin: encode_fn = &Derived::encode_smini32; break;
      case llvm::Intrinsic::smax: encode_fn = &Derived::encode_smaxi32; break;
      default: __builtin_unreachable();
      }
    } else {
      switch (intrin_id) {
      case llvm::Intrinsic::umin: encode_fn = &Derived::encode_umini64; break;
      case llvm::Intrinsic::umax: encode_fn = &Derived::encode_umaxi64; break;
      case llvm::Intrinsic::smin: encode_fn = &Derived::encode_smini64; break;
      case llvm::Intrinsic::smax: encode_fn = &Derived::encode_smaxi64; break;
      default: __builtin_unreachable();
      }
    }
    if (!(derived()->*encode_fn)(std::move(lhs), std::move(rhs), res)) {
      return false;
    }
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::ptrmask: {
    // ptrmask is just an integer and.
    GenericValuePart lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    GenericValuePart rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    auto res = this->result_ref_lazy(inst_idx, 0);
    auto res_scratch = ScratchReg{derived()};
    derived()->encode_landi64(std::move(lhs), std::move(rhs), res_scratch);
    this->set_value(res, res_scratch);
    return true;
  }
  case llvm::Intrinsic::uadd_with_overflow:
    return compile_overflow_intrin(inst_idx, inst, OverflowOp::uadd);
  case llvm::Intrinsic::sadd_with_overflow:
    return compile_overflow_intrin(inst_idx, inst, OverflowOp::sadd);
  case llvm::Intrinsic::usub_with_overflow:
    return compile_overflow_intrin(inst_idx, inst, OverflowOp::usub);
  case llvm::Intrinsic::ssub_with_overflow:
    return compile_overflow_intrin(inst_idx, inst, OverflowOp::ssub);
  case llvm::Intrinsic::umul_with_overflow:
    return compile_overflow_intrin(inst_idx, inst, OverflowOp::umul);
  case llvm::Intrinsic::smul_with_overflow:
    return compile_overflow_intrin(inst_idx, inst, OverflowOp::smul);
  case llvm::Intrinsic::uadd_sat:
    return compile_saturating_intrin(inst_idx, inst, OverflowOp::uadd);
  case llvm::Intrinsic::sadd_sat:
    return compile_saturating_intrin(inst_idx, inst, OverflowOp::sadd);
  case llvm::Intrinsic::usub_sat:
    return compile_saturating_intrin(inst_idx, inst, OverflowOp::usub);
  case llvm::Intrinsic::ssub_sat:
    return compile_saturating_intrin(inst_idx, inst, OverflowOp::ssub);
  case llvm::Intrinsic::fshl:
  case llvm::Intrinsic::fshr: {
    if (!inst->getType()->isIntegerTy()) {
      return false;
    }
    const auto width = inst->getType()->getIntegerBitWidth();
    // Implementing non-powers-of-two is difficult, would require modulo of
    // shift amount. Doesn't really occur in practice.
    if (width != 8 && width != 16 && width != 32 && width != 64) {
      return false;
    }

    auto lhs = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto rhs = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
    auto amt = this->val_ref(llvm_val_idx(inst->getOperand(2)), 0);
    ScratchReg res{derived()};

    // TODO: generate better code for constant amounts.
    bool shift_left = intrin_id == llvm::Intrinsic::fshl;
    if (inst->getOperand(0) == inst->getOperand(1)) {
      // Better code for rotate.
      using EncodeFnTy =
          bool (Derived::*)(GenericValuePart, GenericValuePart, ScratchReg &);
      EncodeFnTy fn = nullptr;
      if (shift_left) {
        switch (width) {
        case 8: fn = &Derived::encode_roli8; break;
        case 16: fn = &Derived::encode_roli16; break;
        case 32: fn = &Derived::encode_roli32; break;
        case 64: fn = &Derived::encode_roli64; break;
        default: TPDE_UNREACHABLE("unreachable width");
        }
      } else {
        switch (width) {
        case 8: fn = &Derived::encode_rori8; break;
        case 16: fn = &Derived::encode_rori16; break;
        case 32: fn = &Derived::encode_rori32; break;
        case 64: fn = &Derived::encode_rori64; break;
        default: TPDE_UNREACHABLE("unreachable width");
        }
      }

      if (!(derived()->*fn)(std::move(lhs), std::move(amt), res)) {
        return false;
      }
    } else {
      using EncodeFnTy = bool (Derived::*)(
          GenericValuePart, GenericValuePart, GenericValuePart, ScratchReg &);
      EncodeFnTy fn = nullptr;
      if (shift_left) {
        switch (width) {
        case 8: fn = &Derived::encode_fshli8; break;
        case 16: fn = &Derived::encode_fshli16; break;
        case 32: fn = &Derived::encode_fshli32; break;
        case 64: fn = &Derived::encode_fshli64; break;
        default: TPDE_UNREACHABLE("unreachable width");
        }
      } else {
        switch (width) {
        case 8: fn = &Derived::encode_fshri8; break;
        case 16: fn = &Derived::encode_fshri16; break;
        case 32: fn = &Derived::encode_fshri32; break;
        case 64: fn = &Derived::encode_fshri64; break;
        default: TPDE_UNREACHABLE("unreachable width");
        }
      }

      if (!(derived()->*fn)(
              std::move(lhs), std::move(rhs), std::move(amt), res)) {
        return false;
      }
    }

    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::bswap: {
    auto *val = inst->getOperand(0);
    if (!val->getType()->isIntegerTy()) {
      return false;
    }
    const auto width = val->getType()->getIntegerBitWidth();
    if (width % 16 || width > 64) {
      return false;
    }

    using EncodeFnTy = bool (Derived::*)(GenericValuePart, ScratchReg &);
    static constexpr std::array<EncodeFnTy, 4> encode_fns = {
        &Derived::encode_bswapi16,
        &Derived::encode_bswapi32,
        &Derived::encode_bswapi48,
        &Derived::encode_bswapi64,
    };
    EncodeFnTy encode_fn = encode_fns[width / 16 - 1];

    auto val_ref = this->val_ref(llvm_val_idx(val), 0);
    ScratchReg res{derived()};
    if (!(derived()->*encode_fn)(std::move(val_ref), res)) {
      return false;
    }

    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::ctpop: {
    auto *val = inst->getOperand(0);
    if (!val->getType()->isIntegerTy()) {
      return false;
    }
    const auto width = val->getType()->getIntegerBitWidth();
    if (width > 64) {
      return false;
    }

    GenericValuePart op = this->val_ref(llvm_val_idx(val), 0);
    if (width % 32) {
      unsigned tgt_width = tpde::util::align_up(width, 32);
      op = derived()->ext_int(std::move(op), false, width, tgt_width);
    }

    ScratchReg res{derived()};
    if (width <= 32) {
      derived()->encode_ctpopi32(std::move(op), res);
    } else {
      derived()->encode_ctpopi64(std::move(op), res);
    }

    ValuePartRef res_ref = this->result_ref_lazy(inst_idx, 0);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::ctlz:
  case llvm::Intrinsic::cttz: {
    auto *val = inst->getOperand(0);
    assert(val->getType()->isIntegerTy());
    const auto width = val->getType()->getIntegerBitWidth();
    if (width != 8 && width != 16 && width != 32 && width != 64) {
      return false;
    }

    const auto zero_is_poison =
        !llvm::cast<llvm::ConstantInt>(inst->getOperand(1))->isZero();

    auto val_ref = this->val_ref(llvm_val_idx(val), 0);
    auto res_ref = this->result_ref_lazy(inst_idx, 0);
    ScratchReg res{derived()};

    if (intrin_id == llvm::Intrinsic::ctlz) {
      switch (width) {
      case 8:
        if (zero_is_poison) {
          derived()->encode_ctlzi8_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_ctlzi8(std::move(val_ref), res);
        }
        break;
      case 16:
        if (zero_is_poison) {
          derived()->encode_ctlzi16_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_ctlzi16(std::move(val_ref), res);
        }
        break;
      case 32:
        if (zero_is_poison) {
          derived()->encode_ctlzi32_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_ctlzi32(std::move(val_ref), res);
        }
        break;
      case 64:
        if (zero_is_poison) {
          derived()->encode_ctlzi64_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_ctlzi64(std::move(val_ref), res);
        }
        break;
      default: __builtin_unreachable();
      }
    } else {
      assert(intrin_id == llvm::Intrinsic::cttz);
      switch (width) {
      case 8:
        if (zero_is_poison) {
          derived()->encode_cttzi32_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_cttzi8(std::move(val_ref), res);
        }
        break;
      case 16:
        if (zero_is_poison) {
          derived()->encode_cttzi32_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_cttzi16(std::move(val_ref), res);
        }
        break;
      case 32:
        if (zero_is_poison) {
          derived()->encode_cttzi32_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_cttzi32(std::move(val_ref), res);
        }
        break;
      case 64:
        if (zero_is_poison) {
          derived()->encode_cttzi64_zero_poison(std::move(val_ref), res);
        } else {
          derived()->encode_cttzi64(std::move(val_ref), res);
        }
        break;
      default: __builtin_unreachable();
      }
    }

    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::bitreverse: {
    auto *val = inst->getOperand(0);
    if (!val->getType()->isIntegerTy()) {
      return false;
    }
    const auto width = val->getType()->getIntegerBitWidth();
    if (width > 64) {
      return false;
    }

    GenericValuePart op = this->val_ref(llvm_val_idx(val), 0);
    if (width % 32) {
      using EncodeImm = typename Derived::EncodeImm;
      ScratchReg shifted{this};
      if (width < 32) {
        derived()->encode_shli32(std::move(op), EncodeImm{32 - width}, shifted);
      } else {
        derived()->encode_shli64(std::move(op), EncodeImm{64 - width}, shifted);
      }
      op = std::move(shifted);
    }

    ScratchReg res{derived()};
    if (width <= 32) {
      derived()->encode_bitreversei32(std::move(op), res);
    } else {
      derived()->encode_bitreversei64(std::move(op), res);
    }

    ValuePartRef res_ref = this->result_ref_lazy(inst_idx, 0);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::prefetch: {
    auto ptr_ref = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);

    const auto rw =
        llvm::cast<llvm::ConstantInt>(inst->getOperand(1))->getZExtValue();
    const auto locality =
        llvm::cast<llvm::ConstantInt>(inst->getOperand(2))->getZExtValue();
    // for now, ignore instruction/data distinction

    if (rw == 0) {
      // read
      switch (locality) {
      case 0: derived()->encode_prefetch_rl0(std::move(ptr_ref)); break;
      case 1: derived()->encode_prefetch_rl1(std::move(ptr_ref)); break;
      case 2: derived()->encode_prefetch_rl2(std::move(ptr_ref)); break;
      default: assert(0);
      case 3: derived()->encode_prefetch_rl3(std::move(ptr_ref)); break;
      }
    } else {
      assert(rw == 1);
      // write
      switch (locality) {
      case 0: derived()->encode_prefetch_wl0(std::move(ptr_ref)); break;
      case 1: derived()->encode_prefetch_wl1(std::move(ptr_ref)); break;
      case 2: derived()->encode_prefetch_wl2(std::move(ptr_ref)); break;
      default: assert(0);
      case 3: derived()->encode_prefetch_wl3(std::move(ptr_ref)); break;
      }
    }
    return true;
  }
  case llvm::Intrinsic::eh_typeid_for: {
    auto *type = inst->getOperand(0);
    assert(llvm::isa<llvm::GlobalValue>(type));

    // not the most efficient but it's OK
    const auto type_info_sym = lookup_type_info_sym(llvm_val_idx(type));
    const auto idx = this->assembler.except_type_idx_for_sym(type_info_sym);

    auto res_ref = this->result_ref_eager(inst_idx, 0);
    auto const_ref = ValuePartRef{idx, Config::GP_BANK, 4};
    derived()->materialize_constant(const_ref, res_ref.cur_reg());
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  case llvm::Intrinsic::is_constant: {
    // > On the other hand, if constant folding is not run, it will never
    // evaluate to true, even in simple cases. example in
    // 641.leela_s:UCTNode.cpp

    // ref-count the argument
    this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
    auto res_ref = this->result_ref_eager(inst_idx, 0);
    auto const_ref = ValuePartRef{0, Config::GP_BANK, 4};
    derived()->materialize_constant(const_ref, res_ref.cur_reg());
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  default: {
    return derived()->handle_intrin(inst_idx, inst, intrin);
  }
  }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_is_fpclass(
    IRValueRef inst_idx, llvm::Instruction *inst) noexcept {
  using EncodeImm = typename Derived::EncodeImm;

  auto *op = inst->getOperand(0);
  auto *op_ty = op->getType();

  if (!op_ty->isFloatTy() && !op_ty->isDoubleTy()) {
    TPDE_LOG_ERR("is_fpclass only supports float and doubles");
    return false;
  }
  const auto is_double = op_ty->isDoubleTy();
  const auto test =
      llvm::dyn_cast<llvm::ConstantInt>(inst->getOperand(1))->getZExtValue();

  enum {
    SIGNALING_NAN = 1 << 0,
    QUIET_NAN = 1 << 1,
    NEG_INF = 1 << 2,
    NEG_NORM = 1 << 3,
    NEG_SUBNORM = 1 << 4,
    NEG_ZERO = 1 << 5,
    POS_ZERO = 1 << 6,
    POS_SUBNORM = 1 << 7,
    POS_NORM = 1 << 8,
    POS_INF = 1 << 9,

    IS_NAN = SIGNALING_NAN | QUIET_NAN,
    IS_INF = NEG_INF | POS_INF,
    IS_NORM = NEG_NORM | POS_NORM,
    IS_FINITE =
        NEG_NORM | NEG_SUBNORM | NEG_ZERO | POS_ZERO | POS_SUBNORM | POS_NORM,
  };

  ScratchReg res_scratch{derived()};
  auto res_ref = this->result_ref_lazy(inst_idx, 0);
  auto op_ref = this->val_ref(llvm_val_idx(op), 0);

  // handle common case
#define TEST(cond, name)                                                       \
  if (test == cond) {                                                          \
    if (is_double) {                                                           \
      derived()->encode_is_fpclass_##name##_double(                            \
          EncodeImm{0u}, std::move(op_ref), res_scratch);                      \
    } else {                                                                   \
      derived()->encode_is_fpclass_##name##_float(                             \
          EncodeImm{0u}, std::move(op_ref), res_scratch);                      \
    }                                                                          \
    this->set_value(res_ref, res_scratch);                                     \
    return true;                                                               \
  }

  TEST(IS_NAN, nan)
  TEST(IS_INF, inf)
  TEST(IS_NORM, norm)
#undef TEST

  // we OR' together the results from each test so initialize the result with
  // zero
  auto const_ref = ValuePartRef{0, Config::GP_BANK, 4};
  derived()->materialize_constant(const_ref, res_scratch.alloc_gp());

#define TEST(cond, name)                                                       \
  if (test & cond) {                                                           \
    if (is_double) {                                                           \
      /* note that the std::move(res_scratch) here creates a new               \
       * ScratchReg that manages the register inside the                       \
       * GenericValuePart and res_scratch becomes invalid by the time          \
       * the encode function is entered */                                     \
      derived()->encode_is_fpclass_##name##_double(                            \
          std::move(res_scratch), op_ref, res_scratch);                        \
    } else {                                                                   \
      derived()->encode_is_fpclass_##name##_float(                             \
          std::move(res_scratch), op_ref, res_scratch);                        \
    }                                                                          \
  }
  TEST(SIGNALING_NAN, snan)
  TEST(QUIET_NAN, qnan)
  TEST(NEG_INF, ninf)
  TEST(NEG_NORM, nnorm)
  TEST(NEG_SUBNORM, nsnorm)
  TEST(NEG_ZERO, nzero)
  TEST(POS_ZERO, pzero)
  TEST(POS_SUBNORM, psnorm)
  TEST(POS_NORM, pnorm)
  TEST(POS_INF, pinf)
#undef TEST

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_overflow_intrin(
    IRValueRef inst_idx, llvm::Instruction *inst, OverflowOp op) noexcept {
  auto *llvm_lhs = inst->getOperand(0);
  auto *llvm_rhs = inst->getOperand(1);

  auto *ty = llvm_lhs->getType();
  assert(ty->isIntegerTy());
  const auto width = ty->getIntegerBitWidth();

  if (width == 128) {
    auto lhs_ref = this->val_ref(llvm_val_idx(llvm_lhs), 0);
    auto rhs_ref = this->val_ref(llvm_val_idx(llvm_rhs), 0);
    lhs_ref.inc_ref_count();
    rhs_ref.inc_ref_count();
    GenericValuePart lhs_op_high = this->val_ref(llvm_val_idx(llvm_lhs), 1);
    GenericValuePart rhs_op_high = this->val_ref(llvm_val_idx(llvm_rhs), 1);
    ScratchReg res_val{derived()}, res_val_high{derived()}, res_of{derived()};

    if (!derived()->handle_overflow_intrin_128(op,
                                               std::move(lhs_ref),
                                               std::move(lhs_op_high),
                                               std::move(rhs_ref),
                                               std::move(rhs_op_high),
                                               res_val,
                                               res_val_high,
                                               res_of)) {
      return false;
    }

    auto res_ref_val = this->result_ref_lazy(inst_idx, 0);
    auto res_ref_high = this->result_ref_lazy(inst_idx, 1);
    auto res_ref_of = this->result_ref_lazy(inst_idx, 2);
    this->set_value(res_ref_val, res_val);
    this->set_value(res_ref_high, res_val_high);
    this->set_value(res_ref_of, res_of);

    res_ref_high.reset_without_refcount();
    res_ref_of.reset_without_refcount();
    return true;
  }

  u32 width_idx = 0;
  switch (width) {
  case 8: width_idx = 0; break;
  case 16: width_idx = 1; break;
  case 32: width_idx = 2; break;
  case 64: width_idx = 3; break;
  default:
    assert(0);
    TPDE_LOG_ERR("overflow op with width {} not supported", width);
    return false;
  }

  using EncodeFnTy = bool (Derived::*)(
      GenericValuePart, GenericValuePart, ScratchReg &, ScratchReg &);
  std::array<std::array<EncodeFnTy, 4>, 6> encode_fns = {
      {
       {&Derived::encode_of_add_u8,
           &Derived::encode_of_add_u16,
           &Derived::encode_of_add_u32,
           &Derived::encode_of_add_u64},
       {&Derived::encode_of_add_i8,
           &Derived::encode_of_add_i16,
           &Derived::encode_of_add_i32,
           &Derived::encode_of_add_i64},
       {&Derived::encode_of_sub_u8,
           &Derived::encode_of_sub_u16,
           &Derived::encode_of_sub_u32,
           &Derived::encode_of_sub_u64},
       {&Derived::encode_of_sub_i8,
           &Derived::encode_of_sub_i16,
           &Derived::encode_of_sub_i32,
           &Derived::encode_of_sub_i64},
       {&Derived::encode_of_mul_u8,
           &Derived::encode_of_mul_u16,
           &Derived::encode_of_mul_u32,
           &Derived::encode_of_mul_u64},
       {&Derived::encode_of_mul_i8,
           &Derived::encode_of_mul_i16,
           &Derived::encode_of_mul_i32,
           &Derived::encode_of_mul_i64},
       }
  };

  EncodeFnTy encode_fn = encode_fns[static_cast<u32>(op)][width_idx];

  GenericValuePart lhs_op = this->val_ref(llvm_val_idx(llvm_lhs), 0);
  GenericValuePart rhs_op = this->val_ref(llvm_val_idx(llvm_rhs), 0);
  ScratchReg res_val{derived()}, res_of{derived()};

  if (!(derived()->*encode_fn)(
          std::move(lhs_op), std::move(rhs_op), res_val, res_of)) {
    return false;
  }

  auto res_ref_val = this->result_ref_lazy(inst_idx, 0);
  auto res_ref_of = this->result_ref_lazy(inst_idx, 1);

  this->set_value(res_ref_val, res_val);
  this->set_value(res_ref_of, res_of);
  res_ref_of.reset_without_refcount();
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_saturating_intrin(
    IRValueRef inst_idx, llvm::Instruction *inst, OverflowOp op) noexcept {
  auto *ty = inst->getType();
  if (!ty->isIntegerTy()) {
    return false;
  }

  const auto width = ty->getIntegerBitWidth();
  u32 width_idx = 0;
  switch (width) {
  case 8: width_idx = 0; break;
  case 16: width_idx = 1; break;
  case 32: width_idx = 2; break;
  case 64: width_idx = 3; break;
  default: return false;
  }

  using EncodeFnTy =
      bool (Derived::*)(GenericValuePart, GenericValuePart, ScratchReg &);
  std::array<std::array<EncodeFnTy, 4>, 4> encode_fns{
      {
       {&Derived::encode_sat_add_u8,
           &Derived::encode_sat_add_u16,
           &Derived::encode_sat_add_u32,
           &Derived::encode_sat_add_u64},
       {&Derived::encode_sat_add_i8,
           &Derived::encode_sat_add_i16,
           &Derived::encode_sat_add_i32,
           &Derived::encode_sat_add_i64},
       {&Derived::encode_sat_sub_u8,
           &Derived::encode_sat_sub_u16,
           &Derived::encode_sat_sub_u32,
           &Derived::encode_sat_sub_u64},
       {&Derived::encode_sat_sub_i8,
           &Derived::encode_sat_sub_i16,
           &Derived::encode_sat_sub_i32,
           &Derived::encode_sat_sub_i64},
       }
  };

  EncodeFnTy encode_fn = encode_fns[static_cast<u32>(op)][width_idx];

  GenericValuePart lhs_op = this->val_ref(llvm_val_idx(inst->getOperand(0)), 0);
  GenericValuePart rhs_op = this->val_ref(llvm_val_idx(inst->getOperand(1)), 0);
  ScratchReg res{derived()};
  if (!(derived()->*encode_fn)(std::move(lhs_op), std::move(rhs_op), res)) {
    return false;
  }

  auto res_ref_val = this->result_ref_lazy(inst_idx, 0);
  this->set_value(res_ref_val, res);
  return true;
}

} // namespace tpde_llvm
