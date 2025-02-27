// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ConstantFolding.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/Support/AtomicOrdering.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/TimeProfiler.h>
#include <llvm/Support/raw_ostream.h>

#include "tpde/AssemblerElf.hpp"
#include "tpde/CompilerBase.hpp"
#include "tpde/base.hpp"
#include "tpde/util/BumpAllocator.hpp"
#include "tpde/util/SmallVector.hpp"
#include "tpde/util/misc.hpp"

#include "JITMapper.hpp"
#include "LLVMAdaptor.hpp"
#include "tpde-llvm/LLVMCompiler.hpp"

namespace tpde_llvm {

template <typename Adaptor, typename Derived, typename Config>
struct LLVMCompilerBase : public LLVMCompiler,
                          tpde::CompilerBase<LLVMAdaptor, Derived, Config> {
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
    std::variant<ValuePartRef, ScratchReg> base;
    std::optional<std::variant<ValuePartRef, ScratchReg>> index;
    u64 scale;
    u32 idx_size_bits;
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

  tpde::util::BumpAllocator<> const_allocator;

  llvm::DenseMap<const llvm::GlobalValue *, SymRef> global_syms;

  tpde::util::SmallVector<VarRefInfo, 16> variable_refs{};
  tpde::util::SmallVector<std::pair<IRValueRef, SymRef>, 16> type_info_syms;

  enum class LibFunc {
    divti3,
    udivti3,
    modti3,
    umodti3,
    fmod,
    fmodf,
    floorf,
    floor,
    ceilf,
    ceil,
    roundf,
    round,
    rintf,
    rint,
    memcpy,
    memset,
    memmove,
    resume,
    powisf2,
    powidf2,
    trunc,
    truncf,
    pow,
    powf,
    sin,
    sinf,
    cos,
    cosf,
    log,
    logf,
    log10,
    log10f,
    exp,
    expf,
    trunctfsf2,
    trunctfdf2,
    extendsftf2,
    extenddftf2,
    eqtf2,
    netf2,
    gttf2,
    getf2,
    lttf2,
    letf2,
    unordtf2,
    floatsitf,
    floatditf,
    floatunditf,
    floatunsitf,
    fixtfdi,
    fixunstfdi,
    addtf3,
    subtf3,
    multf3,
    divtf3,
    MAX
  };
  std::array<SymRef, static_cast<size_t>(LibFunc::MAX)> libfunc_syms;

  llvm::TimeTraceProfilerEntry *time_entry;

  LLVMCompilerBase(LLVMAdaptor *adaptor, const bool generate_obj)
      : Base{adaptor, generate_obj} {
    static_assert(tpde::Compiler<Derived, Config>);
    static_assert(std::is_same_v<Adaptor, LLVMAdaptor>);
    libfunc_syms.fill({});
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

  LLVMAdaptor::ValueParts val_parts(IRValueRef val) const noexcept {
    return this->adaptor->val_parts(val);
  }

  std::optional<ValuePartRef> val_ref_special(IRValueRef value,
                                              u32 part) noexcept {
    if (llvm::isa<llvm::Constant>(value)) {
      return val_ref_constant(value, part);
    }
    return std::nullopt;
  }

  std::optional<ValuePartRef> val_ref_constant(IRValueRef val_idx,
                                               u32 part) noexcept;

  ValuePartRef result_ref_lazy(const llvm::Value *v, u32 part) noexcept {
    return Base::result_ref_lazy(v, part);
  }

  /// Specialized for llvm::Instruction to avoid type check in val_local_idx.
  ValuePartRef result_ref_lazy(const llvm::Instruction *i, u32 part) noexcept {
    const auto local_idx =
        static_cast<ValLocalIdx>(this->adaptor->inst_lookup_idx(i));
    if (this->val_assignment(local_idx) == nullptr) {
      this->init_assignment(i, local_idx);
    }
    return ValuePartRef{this, local_idx, part};
  }

private:
  static typename Assembler::SymBinding
      convert_linkage(const llvm::GlobalValue *gv) noexcept {
    if (gv->hasLocalLinkage()) {
      return Assembler::SymBinding::LOCAL;
    } else if (gv->isWeakForLinker()) {
      return Assembler::SymBinding::WEAK;
    }
    return Assembler::SymBinding::GLOBAL;
  }

public:
  void define_func_idx(IRFuncRef func, const u32 idx) noexcept;
  bool hook_post_func_sym_init() noexcept;
  [[nodiscard]] bool
      global_init_to_data(const llvm::Value *reloc_base,
                          tpde::util::SmallVector<u8, 64> &data,
                          tpde::util::SmallVector<RelocInfo, 8> &relocs,
                          const llvm::DataLayout &layout,
                          const llvm::Constant *constant,
                          u32 off) noexcept;

  SymRef get_libfunc_sym(LibFunc func) noexcept;

  SymRef global_sym(const llvm::GlobalValue *global) const noexcept {
    SymRef res = global_syms.lookup(global);
    assert(res.valid());
    return res;
  }

  void setup_var_ref_assignments() noexcept;

  bool compile_func(IRFuncRef func, u32 idx) noexcept {
    // Reuse/release memory for stored constants from previous function
    const_allocator.reset();
    // We might encounter types that are unsupported during compilation, which
    // cause the flag in the adaptor to be set. In such cases, return false.
    return Base::compile_func(func, idx) && !this->adaptor->func_unsupported;
  }

  bool compile(llvm::Module &mod) noexcept;

  bool compile_inst(const llvm::Instruction *, InstRange) noexcept;

  bool compile_ret(const llvm::ReturnInst *) noexcept;
  bool compile_load_generic(const llvm::LoadInst *,
                            GenericValuePart &&) noexcept;
  bool compile_load(const llvm::LoadInst *) noexcept;
  bool compile_store_generic(const llvm::StoreInst *,
                             GenericValuePart &&) noexcept;
  bool compile_store(const llvm::StoreInst *) noexcept;
  bool compile_int_binary_op(const llvm::BinaryOperator *,
                             IntBinaryOp op) noexcept;
  bool compile_float_binary_op(const llvm::BinaryOperator *,
                               FloatBinaryOp op) noexcept;
  bool compile_fneg(const llvm::UnaryOperator *) noexcept;
  bool compile_float_ext_trunc(const llvm::CastInst *) noexcept;
  bool compile_float_to_int(const llvm::CastInst *, bool sign) noexcept;
  bool compile_int_to_float(const llvm::CastInst *, bool sign) noexcept;
  bool compile_int_trunc(const llvm::TruncInst *) noexcept;
  bool compile_int_ext(const llvm::CastInst *, bool sign) noexcept;
  bool compile_ptr_to_int(const llvm::PtrToIntInst *) noexcept;
  bool compile_int_to_ptr(const llvm::IntToPtrInst *) noexcept;
  bool compile_bitcast(const llvm::BitCastInst *) noexcept;
  bool compile_extract_value(const llvm::ExtractValueInst *) noexcept;
  bool compile_insert_value(const llvm::InsertValueInst *) noexcept;

  void extract_element(IRValueRef vec,
                       unsigned idx,
                       LLVMBasicValType ty,
                       ScratchReg &out) noexcept;
  void insert_element(IRValueRef vec,
                      unsigned idx,
                      LLVMBasicValType ty,
                      GenericValuePart el) noexcept;
  bool compile_extract_element(const llvm::ExtractElementInst *) noexcept;
  bool compile_insert_element(const llvm::InsertElementInst *) noexcept;
  bool compile_shuffle_vector(const llvm::ShuffleVectorInst *) noexcept;

  bool compile_cmpxchg(const llvm::AtomicCmpXchgInst *) noexcept;
  bool compile_atomicrmw(const llvm::AtomicRMWInst *) noexcept;
  bool compile_fence(const llvm::FenceInst *) noexcept;
  bool compile_freeze(const llvm::FreezeInst *) noexcept;
  bool compile_call(const llvm::CallBase *) noexcept;
  bool compile_select(const llvm::SelectInst *) noexcept;
  GenericValuePart resolved_gep_to_addr(ResolvedGEP &gep) noexcept;
  bool compile_gep(const llvm::GetElementPtrInst *, InstRange) noexcept;
  bool compile_fcmp(const llvm::FCmpInst *) noexcept;
  bool compile_switch(const llvm::SwitchInst *) noexcept;
  bool compile_invoke(const llvm::InvokeInst *) noexcept;
  bool compile_landing_pad(const llvm::LandingPadInst *) noexcept;
  bool compile_resume(const llvm::ResumeInst *) noexcept;
  SymRef lookup_type_info_sym(IRValueRef value) noexcept;
  bool compile_intrin(const llvm::IntrinsicInst *) noexcept;
  bool compile_is_fpclass(const llvm::IntrinsicInst *) noexcept;
  bool compile_overflow_intrin(const llvm::IntrinsicInst *,
                               OverflowOp) noexcept;
  bool compile_saturating_intrin(const llvm::IntrinsicInst *,
                                 OverflowOp) noexcept;

  bool compile_unreachable(const llvm::UnreachableInst *) noexcept {
    return false;
  }

  bool compile_alloca(const llvm::AllocaInst *) noexcept { return false; }

  bool compile_br(const llvm::BranchInst *) noexcept { return false; }

  bool compile_inline_asm(const llvm::CallBase *) { return false; }
  bool compile_call_inner(const llvm::CallInst *,
                          std::variant<SymRef, ValuePartRef> &,
                          bool) noexcept {
    return false;
  }

  bool compile_icmp(const llvm::ICmpInst *, InstRange) noexcept {
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

  bool handle_intrin(const llvm::IntrinsicInst *) noexcept { return false; }

  bool compile_to_elf(llvm::Module &mod,
                      std::vector<uint8_t> &buf) noexcept override;

  JITMapper compile_and_map(
      llvm::Module &mod,
      std::function<void *(std::string_view)> resolver) noexcept override;
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
    LLVMCompilerBase<Adaptor, Derived, Config>::val_ref_constant(
        IRValueRef val, u32 part) noexcept {
  auto *const_val = llvm::cast<llvm::Constant>(val);

  auto [ty, ty_idx] = this->adaptor->val_basic_type_uncached(val, true);
  unsigned sub_part = part;

  if (const_val && ty == LLVMBasicValType::complex) {
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
      // TODO: when can this happen?
      return {};
    }

    ty = part_descs[part].part.type;
  }

  // At this point, ty is the basic type of the element and sub_part the part
  // inside the basic type.

  if (llvm::isa<llvm::GlobalValue>(const_val)) {
    assert(ty == LLVMBasicValType::ptr && sub_part == 0);
    auto local_idx =
        static_cast<ValLocalIdx>(this->adaptor->val_local_idx(const_val));
    if (!this->val_assignment(local_idx)) {
      auto *assignment = this->allocate_assignment(1);
      assignment->initialize(u32(local_idx),
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
    u32 size = this->adaptor->basic_ty_part_size(ty);
    u32 bank = this->adaptor->basic_ty_part_bank(ty);
    static const std::array<u64, 8> zero{};
    assert(size <= zero.size() * sizeof(u64));
    return ValuePartRef(this, zero.data(), size, bank);
  }

  if (auto *cdv = llvm::dyn_cast<llvm::ConstantDataVector>(const_val)) {
    if (ty == LLVMBasicValType::invalid) {
      TPDE_FATAL("illegal vector constant of unsupported type");
    }
    assert(part == 0 && "multi-part vector constants not implemented");
    llvm::StringRef data = cdv->getRawDataValues();
    // TODO: this cast is actually invalid.
    const u64 *data_ptr = reinterpret_cast<const u64 *>(data.data());
    return ValuePartRef(this, data_ptr, data.size(), Config::FP_BANK);
  }

  if (llvm::isa<llvm::ConstantVector>(const_val)) {
    // TODO(ts): check how to handle this
    TPDE_FATAL("non-sequential vector constants should not be legal");
  }

  if (const auto *const_int = llvm::dyn_cast<llvm::ConstantInt>(const_val);
      const_int != nullptr) {
    const u64 *data = const_int->getValue().getRawData();
    switch (ty) {
      using enum LLVMBasicValType;
    case i1:
    case i8: return ValuePartRef(this, data, 1, Config::GP_BANK);
    case i16: return ValuePartRef(this, data, 2, Config::GP_BANK);
    case i32: return ValuePartRef(this, data, 4, Config::GP_BANK);
    case i64:
    case ptr: return ValuePartRef(this, data, 8, Config::GP_BANK);
    case i128: return ValuePartRef(this, data + sub_part, 8, Config::GP_BANK);
    default: TPDE_FATAL("illegal integer constant");
    }
  }

  if (const auto *const_fp = llvm::dyn_cast<llvm::ConstantFP>(const_val);
      const_fp != nullptr) {
    // APFloat has no bitwise storage of the floating-point number and
    // bitcastToAPInt constructs a new APInt, so we need to copy the value.
    llvm::APInt int_val = const_fp->getValue().bitcastToAPInt();
    u64 *data = new (const_allocator) u64[int_val.getNumWords()];
    std::memcpy(
        data, int_val.getRawData(), int_val.getNumWords() * sizeof(u64));
    switch (ty) {
      using enum LLVMBasicValType;
    case f32:
    case v32: return ValuePartRef(this, data, 4, Config::FP_BANK);
    case f64:
    case v64: return ValuePartRef(this, data, 8, Config::FP_BANK);
    case v128:
      return ValuePartRef(this, data, 16, Config::FP_BANK);
      // TODO(ts): support the rest
    default: TPDE_FATAL("illegal fp constant");
    }
  }

  std::string const_str;
  llvm::raw_string_ostream(const_str) << *const_val;
  TPDE_LOG_ERR("encountered unhandled constant {}", const_str);
  TPDE_FATAL("unhandled constant type");
}

template <typename Adaptor, typename Derived, typename Config>
void LLVMCompilerBase<Adaptor, Derived, Config>::define_func_idx(
    IRFuncRef func, const u32 idx) noexcept {
  global_syms[func] = this->func_syms[idx];
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::
    hook_post_func_sym_init() noexcept {
  llvm::TimeTraceScope time_scope("TPDE_GlobalGen");

  // create global symbols and their definitions
  const auto &llvm_mod = *this->adaptor->mod;
  auto &data_layout = llvm_mod.getDataLayout();

  global_syms.reserve(2 * llvm_mod.global_size());

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
        return false;
      }
      continue;
    }

    // TODO(ts): we ignore weak linkage here, should emit a weak symbol for
    // it in the data section and place an undef symbol in the symbol
    // lookup
    auto binding = convert_linkage(gv);
    if (gv->isThreadLocal()) {
      global_syms[gv] = this->assembler.sym_predef_tls(gv->getName(), binding);
    } else if (!gv->isDeclarationForLinker()) {
      global_syms[gv] = this->assembler.sym_predef_data(gv->getName(), binding);
    } else {
      global_syms[gv] = this->assembler.sym_add_undef(gv->getName(), binding);
    }
  }

  for (auto it = llvm_mod.alias_begin(); it != llvm_mod.alias_end(); ++it) {
    const llvm::GlobalAlias *ga = &*it;
    auto binding = convert_linkage(ga);
    if (ga->isThreadLocal()) {
      global_syms[ga] = this->assembler.sym_predef_tls(ga->getName(), binding);
    } else {
      global_syms[ga] = this->assembler.sym_add_undef(ga->getName(), binding);
    }
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

    auto size = data_layout.getTypeAllocSize(init->getType());
    auto align = gv->getAlign().valueOrOne().value();
    bool tls = gv->isThreadLocal();
    auto read_only = gv->isConstant();
    auto sym = global_sym(gv);

    // check if the data value is a zero aggregate and put into bss if that
    // is the case
    if (!read_only && init->isNullValue()) {
      auto secref = tls ? this->assembler.get_tbss_section()
                        : this->assembler.get_bss_section();
      this->assembler.sym_def_predef_zero(secref, sym, size, align);
      continue;
    }

    data.clear();
    relocs.clear();

    data.resize(size);
    if (!global_init_to_data(gv, data, relocs, data_layout, init, 0)) {
      return false;
    }

    u32 off;
    auto sec =
        tls ? this->assembler.get_tdata_section()
            : this->assembler.get_data_section(read_only, !relocs.empty());
    this->assembler.sym_def_predef_data(sec, sym, data, align, &off);
    for (auto &[inner_off, addend, target, type] : relocs) {
      if (type == RelocInfo::RELOC_ABS) {
        this->assembler.reloc_abs(sec, target, off + inner_off, addend);
      } else {
        assert(type == RelocInfo::RELOC_PC32);
        this->assembler.reloc_pc32(sec, target, off + inner_off, addend);
      }
    }
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

  // Handle all-zero values quickly.
  if (constant->isNullValue() || llvm::isa<llvm::UndefValue>(constant)) {
    return true;
  }

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

    bool success = true;
    for (auto i = 0u; i < num_elements; ++i) {
      auto idx = llvm::ConstantInt::get(ctx, llvm::APInt(32, (u64)i, false));
      auto agg_off = layout.getIndexedOffsetInType(ty, {c0, idx});
      success &= global_init_to_data(reloc_base,
                                     data,
                                     relocs,
                                     layout,
                                     CA->getAggregateElement(i),
                                     off + agg_off);
    }
    return success;
  }
  if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(constant); GV) {
    assert(alloc_size == 8);
    relocs.push_back({off, 0, global_sym(GV)});
    return true;
  }

  if (auto *expr = llvm::dyn_cast<llvm::ConstantExpr>(constant)) {
    // idk about this design, currently just hardcoding stuff i see
    // in theory i think this needs a new data buffer so we can recursively call
    // parseConstIntoByteArray
    switch (expr->getOpcode()) {
    case llvm::Instruction::IntToPtr:
      if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(expr->getOperand(0))) {
        auto alloc_size = layout.getTypeAllocSize(expr->getType());
        // TODO: endianess?
        llvm::StoreIntToMemory(CI->getValue(), data.data() + off, alloc_size);
        return true;
      }
      break;
    case llvm::Instruction::PtrToInt:
      if (auto *gv = llvm::dyn_cast<llvm::GlobalValue>(expr->getOperand(0))) {
        if (expr->getType()->isIntegerTy(64)) {
          relocs.push_back({off, 0, global_sym(gv)});
          return true;
        }
      }
      break;
    case llvm::Instruction::GetElementPtr: {
      auto *gep = llvm::cast<llvm::GEPOperator>(expr);
      auto *ptr = gep->getPointerOperand();
      if (auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(ptr); GV) {
        auto indices = tpde::util::SmallVector<llvm::Value *, 8>{};
        for (auto &idx : gep->indices()) {
          indices.push_back(idx.get());
        }

        const auto ty_off = layout.getIndexedOffsetInType(
            gep->getSourceElementType(),
            llvm::ArrayRef{indices.data(), indices.size()});
        relocs.push_back({off, static_cast<int32_t>(ty_off), global_sym(GV)});

        return true;
      }
      break;
    }
    case llvm::Instruction::Trunc:
      // recognize a truncation pattern where we need to emit PC32 relocations
      // i32 trunc (i64 sub (i64 ptrtoint (ptr <someglobal> to i64), i64
      // ptrtoint (ptr <relocBase> to i64)))
      if (expr->getType()->isIntegerTy(32)) {
        if (auto *sub = llvm::dyn_cast<llvm::ConstantExpr>(expr->getOperand(0));
            sub && sub->getOpcode() == llvm::Instruction::Sub &&
            sub->getType()->isIntegerTy(64)) {
          auto *lhs = llvm::dyn_cast<llvm::ConstantExpr>(sub->getOperand(0));
          auto *rhs = llvm::dyn_cast<llvm::ConstantExpr>(sub->getOperand(1));
          if (lhs && rhs && lhs->getOpcode() == llvm::Instruction::PtrToInt &&
              rhs->getOpcode() == llvm::Instruction::PtrToInt) {
            if (rhs->getOperand(0) == reloc_base &&
                llvm::isa<llvm::GlobalVariable>(lhs->getOperand(0))) {
              auto ptr_sym =
                  global_sym(llvm::cast<llvm::GlobalValue>(lhs->getOperand(0)));

              relocs.push_back({off,
                                static_cast<int32_t>(off),
                                ptr_sym,
                                RelocInfo::RELOC_PC32});
              return true;
            }
          }
        }
      }
    case llvm::Instruction::BitCast: {
      if (expr->getType()->isPointerTy()) {
        auto *op = expr->getOperand(0);
        if (llvm::isa<llvm::GlobalValue>(op)) {
          auto ptr_sym = global_sym(llvm::cast<llvm::GlobalValue>(op));
          // emit absolute relocation
          relocs.push_back({off, 0, ptr_sym, RelocInfo::RELOC_ABS});
          return true;
        }
      }
    } break;
    default: break;
    }
  }

  // It's not a simple constant that we can handle, probably some ConstantExpr.
  // Try constant folding to increase the change that we can handle it. Some
  // front-ends like flang like to generate trivially foldable expressions.
  if (auto *fc = llvm::ConstantFoldConstant(constant, layout); constant != fc) {
    // We folded the constant, so try again.
    return global_init_to_data(reloc_base, data, relocs, layout, fc, off);
  }

  TPDE_LOG_ERR("Encountered unknown constant in global initializer");
  llvm::errs() << *constant << "\n";
  return false;
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::SymRef
    LLVMCompilerBase<Adaptor, Derived, Config>::get_libfunc_sym(
        LibFunc func) noexcept {
  assert(func < LibFunc::MAX);
  SymRef &sym = libfunc_syms[static_cast<size_t>(func)];
  if (sym.valid()) [[likely]] {
    return sym;
  }

  std::string_view name = "???";
  switch (func) {
  case LibFunc::divti3: name = "__divti3"; break;
  case LibFunc::udivti3: name = "__udivti3"; break;
  case LibFunc::modti3: name = "__modti3"; break;
  case LibFunc::umodti3: name = "__umodti3"; break;
  case LibFunc::fmod: name = "fmod"; break;
  case LibFunc::fmodf: name = "fmodf"; break;
  case LibFunc::floorf: name = "floorf"; break;
  case LibFunc::floor: name = "floor"; break;
  case LibFunc::ceilf: name = "ceilf"; break;
  case LibFunc::ceil: name = "ceil"; break;
  case LibFunc::roundf: name = "roundf"; break;
  case LibFunc::round: name = "round"; break;
  case LibFunc::rintf: name = "rintf"; break;
  case LibFunc::rint: name = "rint"; break;
  case LibFunc::memcpy: name = "memcpy"; break;
  case LibFunc::memset: name = "memset"; break;
  case LibFunc::memmove: name = "memmove"; break;
  case LibFunc::resume: name = "_Unwind_Resume"; break;
  case LibFunc::powisf2: name = "__powisf2"; break;
  case LibFunc::powidf2: name = "__powidf2"; break;
  case LibFunc::trunc: name = "trunc"; break;
  case LibFunc::truncf: name = "truncf"; break;
  case LibFunc::pow: name = "pow"; break;
  case LibFunc::powf: name = "powf"; break;
  case LibFunc::sin: name = "sin"; break;
  case LibFunc::sinf: name = "sinf"; break;
  case LibFunc::cos: name = "cos"; break;
  case LibFunc::cosf: name = "cosf"; break;
  case LibFunc::log: name = "log"; break;
  case LibFunc::logf: name = "logf"; break;
  case LibFunc::log10: name = "log10"; break;
  case LibFunc::log10f: name = "log10f"; break;
  case LibFunc::exp: name = "exp"; break;
  case LibFunc::expf: name = "expf"; break;
  case LibFunc::trunctfsf2: name = "__trunctfsf2"; break;
  case LibFunc::trunctfdf2: name = "__trunctfdf2"; break;
  case LibFunc::extendsftf2: name = "__extendsftf2"; break;
  case LibFunc::extenddftf2: name = "__extenddftf2"; break;
  case LibFunc::eqtf2: name = "__eqtf2"; break;
  case LibFunc::netf2: name = "__netf2"; break;
  case LibFunc::gttf2: name = "__gttf2"; break;
  case LibFunc::getf2: name = "__getf2"; break;
  case LibFunc::lttf2: name = "__lttf2"; break;
  case LibFunc::letf2: name = "__letf2"; break;
  case LibFunc::unordtf2: name = "__unordtf2"; break;
  case LibFunc::floatsitf: name = "__floatsitf"; break;
  case LibFunc::floatditf: name = "__floatditf"; break;
  case LibFunc::floatunsitf: name = "__floatunsitf"; break;
  case LibFunc::floatunditf: name = "__floatunditf"; break;
  case LibFunc::fixtfdi: name = "__fixtfdi"; break;
  case LibFunc::fixunstfdi: name = "__fixunstfdi"; break;
  case LibFunc::addtf3: name = "__addtf3"; break;
  case LibFunc::subtf3: name = "__subtf3"; break;
  case LibFunc::multf3: name = "__multf3"; break;
  case LibFunc::divtf3: name = "__divtf3"; break;
  default: TPDE_UNREACHABLE("invalid libfunc");
  }

  sym = this->assembler.sym_add_undef(name, Assembler::SymBinding::GLOBAL);
  return sym;
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
    for (auto entry : this->adaptor->global_lookup) {
      variable_refs[entry.second].val = entry.first;
      variable_refs[entry.second].alloca = false;
      variable_refs[entry.second].local = entry.first->hasLocalLinkage();
      // assignments are initialized lazily in val_ref_special.
    }
  }

  // Allocate registers for TPDE's stack slots
  u32 cur_idx = this->adaptor->global_idx_end;
  for (auto v : this->adaptor->cur_static_allocas()) {
    variable_refs[cur_idx].val = v;
    variable_refs[cur_idx].alloca = true;
    // static allocas don't need to be compiled later
    this->adaptor->inst_set_fused(v, true);

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
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile(
    llvm::Module &mod) noexcept {
  this->adaptor->switch_module(mod);

  type_info_syms.clear();
  global_syms.clear();
  libfunc_syms.fill({});

  if (!Base::compile()) {
    return false;
  }

  // copy alias symbol definitions
  for (auto it = this->adaptor->mod->alias_begin();
       it != this->adaptor->mod->alias_end();
       ++it) {
    llvm::GlobalAlias *ga = &*it;
    auto *alias_target = llvm::dyn_cast<llvm::GlobalValue>(ga->getAliasee());
    if (alias_target == nullptr) {
      assert(0);
      continue;
    }
    auto dst_sym = global_sym(ga);
    auto from_sym = global_sym(alias_target);

    this->assembler.sym_copy(dst_sym, from_sym);
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_inst(
    const llvm::Instruction *i, InstRange remaining) noexcept {
  TPDE_LOG_TRACE("Compiling inst {}", this->adaptor->inst_fmt_ref(i));

  // TODO: const-correctness
  switch (i->getOpcode()) {
    // clang-format off
  case llvm::Instruction::Ret: return compile_ret(llvm::cast<llvm::ReturnInst>(i));
  case llvm::Instruction::Load: return compile_load(llvm::cast<llvm::LoadInst>(i));
  case llvm::Instruction::Store: return compile_store(llvm::cast<llvm::StoreInst>(i));
  case llvm::Instruction::Add: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::add);
  case llvm::Instruction::Sub: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::sub);
  case llvm::Instruction::Mul: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::mul);
  case llvm::Instruction::UDiv: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::udiv);
  case llvm::Instruction::SDiv: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::sdiv);
  case llvm::Instruction::URem: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::urem);
  case llvm::Instruction::SRem: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::srem);
  case llvm::Instruction::And: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::land);
  case llvm::Instruction::Or: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::lor);
  case llvm::Instruction::Xor: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::lxor);
  case llvm::Instruction::Shl: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::shl);
  case llvm::Instruction::LShr: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::shr);
  case llvm::Instruction::AShr: return compile_int_binary_op(llvm::cast<llvm::BinaryOperator>(i), IntBinaryOp::ashr);
  case llvm::Instruction::FAdd: return compile_float_binary_op(llvm::cast<llvm::BinaryOperator>(i), FloatBinaryOp::add);
  case llvm::Instruction::FSub: return compile_float_binary_op(llvm::cast<llvm::BinaryOperator>(i), FloatBinaryOp::sub);
  case llvm::Instruction::FMul: return compile_float_binary_op(llvm::cast<llvm::BinaryOperator>(i), FloatBinaryOp::mul);
  case llvm::Instruction::FDiv: return compile_float_binary_op(llvm::cast<llvm::BinaryOperator>(i), FloatBinaryOp::div);
  case llvm::Instruction::FRem: return compile_float_binary_op(llvm::cast<llvm::BinaryOperator>(i), FloatBinaryOp::rem);
  case llvm::Instruction::FNeg: return compile_fneg(llvm::cast<llvm::UnaryOperator>(i));
  case llvm::Instruction::FPExt:
  case llvm::Instruction::FPTrunc: return compile_float_ext_trunc(llvm::cast<llvm::CastInst>(i));
  case llvm::Instruction::FPToSI: return compile_float_to_int(llvm::cast<llvm::CastInst>(i), true);
  case llvm::Instruction::FPToUI: return compile_float_to_int(llvm::cast<llvm::CastInst>(i), false);
  case llvm::Instruction::SIToFP: return compile_int_to_float(llvm::cast<llvm::CastInst>(i), true);
  case llvm::Instruction::UIToFP: return compile_int_to_float(llvm::cast<llvm::CastInst>(i), false);
  case llvm::Instruction::Trunc: return compile_int_trunc(llvm::cast<llvm::TruncInst>(i));
  case llvm::Instruction::SExt: return compile_int_ext(llvm::cast<llvm::CastInst>(i), true);
  case llvm::Instruction::ZExt: return compile_int_ext(llvm::cast<llvm::CastInst>(i), false);
  case llvm::Instruction::PtrToInt: return compile_ptr_to_int(llvm::cast<llvm::PtrToIntInst>(i));
  case llvm::Instruction::IntToPtr: return compile_int_to_ptr(llvm::cast<llvm::IntToPtrInst>(i));
  case llvm::Instruction::BitCast: return compile_bitcast(llvm::cast<llvm::BitCastInst>(i));
  case llvm::Instruction::ExtractValue: return compile_extract_value(llvm::cast<llvm::ExtractValueInst>(i));
  case llvm::Instruction::InsertValue: return compile_insert_value(llvm::cast<llvm::InsertValueInst>(i));
  case llvm::Instruction::ExtractElement: return compile_extract_element(llvm::cast<llvm::ExtractElementInst>(i));
  case llvm::Instruction::InsertElement: return compile_insert_element(llvm::cast<llvm::InsertElementInst>(i));
  case llvm::Instruction::ShuffleVector: return compile_shuffle_vector(llvm::cast<llvm::ShuffleVectorInst>(i));
  case llvm::Instruction::AtomicCmpXchg: return compile_cmpxchg(llvm::cast<llvm::AtomicCmpXchgInst>(i));
  case llvm::Instruction::AtomicRMW: return compile_atomicrmw(llvm::cast<llvm::AtomicRMWInst>(i));
  case llvm::Instruction::Fence: return compile_fence(llvm::cast<llvm::FenceInst>(i));
  case llvm::Instruction::PHI: TPDE_UNREACHABLE("PHI nodes shouldn't be compiled");
  case llvm::Instruction::Freeze: return compile_freeze(llvm::cast<llvm::FreezeInst>(i));
  case llvm::Instruction::Unreachable: return derived()->compile_unreachable(llvm::cast<llvm::UnreachableInst>(i));
  case llvm::Instruction::Alloca: return derived()->compile_alloca(llvm::cast<llvm::AllocaInst>(i));
  case llvm::Instruction::Br: return derived()->compile_br(llvm::cast<llvm::BranchInst>(i));
  case llvm::Instruction::Call: return compile_call(llvm::cast<llvm::CallBase>(i));
  case llvm::Instruction::Select: return compile_select(llvm::cast<llvm::SelectInst>(i));
  case llvm::Instruction::GetElementPtr: return compile_gep(llvm::cast<llvm::GetElementPtrInst>(i), remaining);
  case llvm::Instruction::ICmp: return derived()->compile_icmp(llvm::cast<llvm::ICmpInst>(i), remaining);
  case llvm::Instruction::FCmp: return compile_fcmp(llvm::cast<llvm::FCmpInst>(i));
  case llvm::Instruction::Switch: return compile_switch(llvm::cast<llvm::SwitchInst>(i));
  case llvm::Instruction::Invoke: return compile_invoke(llvm::cast<llvm::InvokeInst>(i));
  case llvm::Instruction::LandingPad: return compile_landing_pad(llvm::cast<llvm::LandingPadInst>(i));
  case llvm::Instruction::Resume: return compile_resume(llvm::cast<llvm::ResumeInst>(i));
    // clang-format on

  default: return false;
  }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ret(
    const llvm::ReturnInst *ret) noexcept {
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
    const llvm::LoadInst *load, GenericValuePart &&ptr_op) noexcept {
  auto res = this->result_ref_lazy(load, 0);
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
    using EncodeFnTy = bool (Derived::*)(GenericValuePart &&, ScratchReg &);
    EncodeFnTy encode_fn = nullptr;
    if (order == llvm::AtomicOrdering::Monotonic) {
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_load_u8_mono; break;
      case 16: encode_fn = &Derived::encode_atomic_load_u16_mono; break;
      case 32: encode_fn = &Derived::encode_atomic_load_u32_mono; break;
      case 64: encode_fn = &Derived::encode_atomic_load_u64_mono; break;
      default: TPDE_UNREACHABLE("invalid size");
      }
    } else if (order == llvm::AtomicOrdering::Acquire) {
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_load_u8_acq; break;
      case 16: encode_fn = &Derived::encode_atomic_load_u16_acq; break;
      case 32: encode_fn = &Derived::encode_atomic_load_u32_acq; break;
      case 64: encode_fn = &Derived::encode_atomic_load_u64_acq; break;
      default: TPDE_UNREACHABLE("invalid size");
      }
    } else {
      assert(order == llvm::AtomicOrdering::SequentiallyConsistent);
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_load_u8_seqcst; break;
      case 16: encode_fn = &Derived::encode_atomic_load_u16_seqcst; break;
      case 32: encode_fn = &Derived::encode_atomic_load_u32_seqcst; break;
      case 64: encode_fn = &Derived::encode_atomic_load_u64_seqcst; break;
      default: TPDE_UNREACHABLE("invalid size");
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
  switch (this->adaptor->val_info(load).type) {
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
    auto res_high = this->result_ref_lazy(load, 1);

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

    auto ty_idx = this->adaptor->val_info(load).complex_part_tys_idx;
    const LLVMComplexPart *part_descs =
        &this->adaptor->complex_part_types[ty_idx + 1];
    unsigned part_count = part_descs[-1].num_parts;

    // TODO: fuse expr; not easy, because we lose the GVP
    AsmReg ptr_reg = this->gval_as_reg(ptr_op);

    unsigned off = 0;
    for (unsigned i = 0; i < part_count; i++) {
      auto part_ref = this->result_ref_lazy(load, i);
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
    const llvm::LoadInst *load) noexcept {
  auto ptr_ref = this->val_ref(load->getPointerOperand(), 0);
  if (ptr_ref.has_assignment() && ptr_ref.assignment().variable_ref()) {
    const auto ref_idx = ptr_ref.state.v.assignment->var_ref_custom_idx;
    if (this->variable_refs[ref_idx].alloca) {
      GenericValuePart addr = derived()->create_addr_for_alloca(ref_idx);
      return compile_load_generic(load, std::move(addr));
    }
  }

  return compile_load_generic(load, std::move(ptr_ref));
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_store_generic(
    const llvm::StoreInst *store, GenericValuePart &&ptr_op) noexcept {
  const auto *op_val = store->getValueOperand();
  auto op_ref = this->val_ref(op_val, 0);

  if (store->isAtomic()) {
    u32 width = 64;
    if (op_val->getType()->isIntegerTy()) {
      width = op_val->getType()->getIntegerBitWidth();
      if (width != 8 && width != 16 && width != 32 && width != 64) {
        TPDE_LOG_ERR("atomic loads not of i8/i16/i32/i64/ptr not supported");
        return false;
      }
    } else if (!op_val->getType()->isPointerTy()) {
      TPDE_LOG_ERR("atomic loads not of i8/i16/i32/i64/ptr not supported");
      return false;
    }

    if (auto align = store->getAlign().value(); align * 8 < width) {
      TPDE_LOG_ERR("unaligned store ({}) not implemented", align);
      return false;
    }

    const auto order = store->getOrdering();
    using EncodeFnTy =
        bool (Derived::*)(GenericValuePart &&, GenericValuePart &&);
    EncodeFnTy encode_fn = nullptr;
    if (order == llvm::AtomicOrdering::Monotonic) {
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_store_u8_mono; break;
      case 16: encode_fn = &Derived::encode_atomic_store_u16_mono; break;
      case 32: encode_fn = &Derived::encode_atomic_store_u32_mono; break;
      case 64: encode_fn = &Derived::encode_atomic_store_u64_mono; break;
      default: TPDE_UNREACHABLE("invalid size");
      }
    } else if (order == llvm::AtomicOrdering::Release) {
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_store_u8_rel; break;
      case 16: encode_fn = &Derived::encode_atomic_store_u16_rel; break;
      case 32: encode_fn = &Derived::encode_atomic_store_u32_rel; break;
      case 64: encode_fn = &Derived::encode_atomic_store_u64_rel; break;
      default: TPDE_UNREACHABLE("invalid size");
      }
    } else {
      assert(order == llvm::AtomicOrdering::SequentiallyConsistent);
      switch (width) {
      case 8: encode_fn = &Derived::encode_atomic_store_u8_seqcst; break;
      case 16: encode_fn = &Derived::encode_atomic_store_u16_seqcst; break;
      case 32: encode_fn = &Derived::encode_atomic_store_u32_seqcst; break;
      case 64: encode_fn = &Derived::encode_atomic_store_u64_seqcst; break;
      default: TPDE_UNREACHABLE("invalid size");
      }
    }

    if (!(derived()->*encode_fn)(std::move(ptr_op), std::move(op_ref))) {
      TPDE_LOG_ERR("fooooo");
      return false;
    }
    return true;
  }

  // TODO: don't recompute this, this is currently computed for every val part
  auto [ty, ty_idx] = this->adaptor->val_basic_type_uncached(op_val, true);

  switch (ty) {
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
    auto op_ref_high = this->val_ref(op_val, 1);

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

    const LLVMComplexPart *part_descs =
        &this->adaptor->complex_part_types[ty_idx + 1];
    unsigned part_count = part_descs[-1].num_parts;

    // TODO: fuse expr; not easy, because we lose the GVP
    AsmReg ptr_reg = this->gval_as_reg(ptr_op);

    unsigned off = 0;
    for (unsigned i = 0; i < part_count; i++) {
      auto part_ref = this->val_ref(op_val, i);
      auto part_addr =
          typename GenericValuePart::Expr{ptr_reg, static_cast<tpde::i32>(off)};
      // Note: val_ref might call val_ref_special, which calls val_parts, which
      // calls val_basic_type_uncached, which will invalidate part_descs.
      // TODO: don't recompute value parts for every constant part
      const LLVMComplexPart *part_descs =
          &this->adaptor->complex_part_types[ty_idx + 1];
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
    const llvm::StoreInst *store) noexcept {
  auto ptr_ref = this->val_ref(store->getPointerOperand(), 0);
  if (ptr_ref.has_assignment() && ptr_ref.assignment().variable_ref()) {
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
    const llvm::BinaryOperator *inst, const IntBinaryOp op) noexcept {
  auto *inst_ty = inst->getType();

  if (inst_ty->isVectorTy()) {
    auto *scalar_ty = inst_ty->getScalarType();
    auto int_width = scalar_ty->getIntegerBitWidth();

    using EncodeFnTy = bool (Derived::*)(
        GenericValuePart &&, GenericValuePart &&, ScratchReg &);
    EncodeFnTy encode_fn = nullptr;
    LLVMBasicValType bvt = this->adaptor->val_info(inst).type;
    switch (op) {
    case IntBinaryOp::add:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_addv8u8; break;
        case 16: encode_fn = &Derived::encode_addv4u16; break;
        case 32: encode_fn = &Derived::encode_addv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_addv16u8; break;
        case 16: encode_fn = &Derived::encode_addv8u16; break;
        case 32: encode_fn = &Derived::encode_addv4u32; break;
        case 64: encode_fn = &Derived::encode_addv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::sub:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_subv8u8; break;
        case 16: encode_fn = &Derived::encode_subv4u16; break;
        case 32: encode_fn = &Derived::encode_subv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_subv16u8; break;
        case 16: encode_fn = &Derived::encode_subv8u16; break;
        case 32: encode_fn = &Derived::encode_subv4u32; break;
        case 64: encode_fn = &Derived::encode_subv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::mul:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_mulv8u8; break;
        case 16: encode_fn = &Derived::encode_mulv4u16; break;
        case 32: encode_fn = &Derived::encode_mulv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_mulv16u8; break;
        case 16: encode_fn = &Derived::encode_mulv8u16; break;
        case 32: encode_fn = &Derived::encode_mulv4u32; break;
        case 64: encode_fn = &Derived::encode_mulv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::land:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_andv8u8; break;
        case 16: encode_fn = &Derived::encode_andv4u16; break;
        case 32: encode_fn = &Derived::encode_andv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_andv16u8; break;
        case 16: encode_fn = &Derived::encode_andv8u16; break;
        case 32: encode_fn = &Derived::encode_andv4u32; break;
        case 64: encode_fn = &Derived::encode_andv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::lxor:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_xorv8u8; break;
        case 16: encode_fn = &Derived::encode_xorv4u16; break;
        case 32: encode_fn = &Derived::encode_xorv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_xorv16u8; break;
        case 16: encode_fn = &Derived::encode_xorv8u16; break;
        case 32: encode_fn = &Derived::encode_xorv4u32; break;
        case 64: encode_fn = &Derived::encode_xorv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::lor:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_orv8u8; break;
        case 16: encode_fn = &Derived::encode_orv4u16; break;
        case 32: encode_fn = &Derived::encode_orv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_orv16u8; break;
        case 16: encode_fn = &Derived::encode_orv8u16; break;
        case 32: encode_fn = &Derived::encode_orv4u32; break;
        case 64: encode_fn = &Derived::encode_orv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::shl:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_shlv8u8; break;
        case 16: encode_fn = &Derived::encode_shlv4u16; break;
        case 32: encode_fn = &Derived::encode_shlv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_shlv16u8; break;
        case 16: encode_fn = &Derived::encode_shlv8u16; break;
        case 32: encode_fn = &Derived::encode_shlv4u32; break;
        case 64: encode_fn = &Derived::encode_shlv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::shr:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_lshrv8u8; break;
        case 16: encode_fn = &Derived::encode_lshrv4u16; break;
        case 32: encode_fn = &Derived::encode_lshrv2u32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_lshrv16u8; break;
        case 16: encode_fn = &Derived::encode_lshrv8u16; break;
        case 32: encode_fn = &Derived::encode_lshrv4u32; break;
        case 64: encode_fn = &Derived::encode_lshrv2u64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    case IntBinaryOp::ashr:
      switch (bvt) {
        using enum LLVMBasicValType;
      case v64:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_ashrv8i8; break;
        case 16: encode_fn = &Derived::encode_ashrv4i16; break;
        case 32: encode_fn = &Derived::encode_ashrv2i32; break;
        default: return false;
        }
        break;
      case v128:
        switch (int_width) {
        case 8: encode_fn = &Derived::encode_ashrv16i8; break;
        case 16: encode_fn = &Derived::encode_ashrv8i16; break;
        case 32: encode_fn = &Derived::encode_ashrv4i32; break;
        case 64: encode_fn = &Derived::encode_ashrv2i64; break;
        default: return false;
        }
        break;
      default: TPDE_UNREACHABLE("invalid basic type for int vector binary op");
      }
      break;
    default: return false;
    }

    auto lhs = this->val_ref(inst->getOperand(0), 0);
    auto rhs = this->val_ref(inst->getOperand(1), 0);
    auto res_ref = this->result_ref_lazy(inst, 0);
    ScratchReg res{this};
    if (!(derived()->*encode_fn)(std::move(lhs), std::move(rhs), res)) {
      return false;
    }
    this->set_value(res_ref, res);
    return true;
  }

  assert(inst_ty->isIntegerTy());

  const auto int_width = inst_ty->getIntegerBitWidth();
  // Storage for encode immediates
  u64 imm1, imm2;
  if (int_width == 128) {
    llvm::Value *lhs_op = inst->getOperand(0);
    llvm::Value *rhs_op = inst->getOperand(1);

    auto res_low = this->result_ref_lazy(inst, 0);
    res_low.inc_ref_count();
    auto res_high = this->result_ref_lazy(inst, 1);

    if (op == IntBinaryOp::udiv || op == IntBinaryOp::sdiv ||
        op == IntBinaryOp::urem || op == IntBinaryOp::srem) {
      LibFunc lf;
      switch (op) {
      case IntBinaryOp::udiv: lf = LibFunc::udivti3; break;
      case IntBinaryOp::sdiv: lf = LibFunc::divti3; break;
      case IntBinaryOp::urem: lf = LibFunc::umodti3; break;
      case IntBinaryOp::srem: lf = LibFunc::modti3; break;
      default: TPDE_UNREACHABLE("invalid div/rem operation");
      }

      std::array<IRValueRef, 2> args{lhs_op, rhs_op};
      std::array<ValuePartRef, 2> res{std::move(res_low), std::move(res_high)};
      derived()->create_helper_call(args, res, get_libfunc_sym(lf));
      return true;
    }

    auto lhs = this->val_ref(lhs_op, 0);
    auto rhs = this->val_ref(rhs_op, 0);
    // TODO(ts): better salvaging
    lhs.inc_ref_count();
    rhs.inc_ref_count();

    auto lhs_high = this->val_ref(lhs_op, 1);
    auto rhs_high = this->val_ref(rhs_op, 1);

    if ((op == IntBinaryOp::add || op == IntBinaryOp::mul ||
         op == IntBinaryOp::land || op == IntBinaryOp::lor ||
         op == IntBinaryOp::lxor) &&
        lhs.is_const() && lhs_high.is_const() && !rhs.is_const() &&
        !rhs_high.is_const()) {
      // TODO(ts): this is a hack since the encoder can currently not do
      // commutable operations so we reorder immediates manually here
      std::swap(lhs, rhs);
      std::swap(lhs_high, rhs_high);
    }

    ScratchReg scratch_low{derived()}, scratch_high{derived()};


    std::array<bool (Derived::*)(GenericValuePart &&,
                                 GenericValuePart &&,
                                 GenericValuePart &&,
                                 GenericValuePart &&,
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

    switch (op) {
    case IntBinaryOp::shl:
    case IntBinaryOp::shr:
    case IntBinaryOp::ashr:
      rhs_high.reset();
      if (rhs.is_const()) {
        imm1 = rhs.state.c.data[0] & 0b111'1111; // amt
        if (imm1 < 64) {
          imm2 = (64 - imm1) & 0b11'1111; // iamt
          if (op == IntBinaryOp::shl) {
            derived()->encode_shli128_lt64(
                std::move(lhs),
                std::move(lhs_high),
                ValuePartRef(this, &imm1, 1, Config::GP_BANK),
                ValuePartRef(this, &imm2, 1, Config::GP_BANK),
                scratch_low,
                scratch_high);
          } else if (op == IntBinaryOp::shr) {
            derived()->encode_shri128_lt64(
                std::move(lhs),
                std::move(lhs_high),
                ValuePartRef(this, &imm1, 1, Config::GP_BANK),
                ValuePartRef(this, &imm2, 1, Config::GP_BANK),
                scratch_low,
                scratch_high);
          } else {
            assert(op == IntBinaryOp::ashr);
            derived()->encode_ashri128_lt64(
                std::move(lhs),
                std::move(lhs_high),
                ValuePartRef(this, &imm1, 1, Config::GP_BANK),
                ValuePartRef(this, &imm2, 1, Config::GP_BANK),
                scratch_low,
                scratch_high);
          }
        } else {
          imm1 -= 64;
          if (op == IntBinaryOp::shl) {
            derived()->encode_shli128_ge64(
                std::move(lhs),
                ValuePartRef(this, &imm1, 1, Config::GP_BANK),
                scratch_low,
                scratch_high);
          } else if (op == IntBinaryOp::shr) {
            derived()->encode_shri128_ge64(
                std::move(lhs_high),
                ValuePartRef(this, &imm1, 1, Config::GP_BANK),
                scratch_low,
                scratch_high);
          } else {
            assert(op == IntBinaryOp::ashr);
            derived()->encode_ashri128_ge64(
                std::move(lhs_high),
                ValuePartRef(this, &imm1, 1, Config::GP_BANK),
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
      bool (Derived::*)(GenericValuePart &&, GenericValuePart &&, ScratchReg &);
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

  auto lhs = this->val_ref(inst->getOperand(0), 0);
  auto rhs = this->val_ref(inst->getOperand(1), 0);

  if ((op == IntBinaryOp::add || op == IntBinaryOp::mul ||
       op == IntBinaryOp::land || op == IntBinaryOp::lor ||
       op == IntBinaryOp::lxor) &&
      lhs.is_const() && !rhs.is_const()) {
    // TODO(ts): this is a hack since the encoder can currently not do
    // commutable operations so we reorder immediates manually here
    std::swap(lhs, rhs);
  }

  // TODO(ts): optimize div/rem by constant to a shift?
  const auto lhs_const = lhs.is_const();
  const auto rhs_const = rhs.is_const();
  u64 lhs_imm;
  u64 rhs_imm;
  GenericValuePart lhs_op = std::move(lhs);
  GenericValuePart rhs_op = std::move(rhs);

  unsigned ext_width = tpde::util::align_up(int_width, 32);
  if (ext_width != int_width) {
    bool ext_lhs = false, ext_rhs = false, sext = false;
    switch (op) {
    case IntBinaryOp::add: break;
    case IntBinaryOp::sub: break;
    case IntBinaryOp::mul: break;
    case IntBinaryOp::udiv: ext_lhs = ext_rhs = true; break;
    case IntBinaryOp::sdiv: ext_lhs = ext_rhs = sext = true; break;
    case IntBinaryOp::urem: ext_lhs = ext_rhs = true; break;
    case IntBinaryOp::srem: ext_lhs = ext_rhs = sext = true; break;
    case IntBinaryOp::land: break;
    case IntBinaryOp::lor: break;
    case IntBinaryOp::lxor: break;
    case IntBinaryOp::shl: break;
    case IntBinaryOp::shr: ext_lhs = true; break;
    case IntBinaryOp::ashr: ext_lhs = sext = true; break;
    }

    if (ext_lhs) {
      if (!lhs_const) {
        lhs_op =
            derived()->ext_int(std::move(lhs_op), sext, int_width, ext_width);
      } else if (sext) {
        lhs_imm = tpde::util::sext(lhs_op.imm64(), int_width);
        lhs_op = ValuePartRef(this, &lhs_imm, 8, Config::GP_BANK);
      }
    }
    if (ext_rhs) {
      if (!rhs_const) {
        rhs_op =
            derived()->ext_int(std::move(rhs_op), sext, int_width, ext_width);
      } else if (sext) {
        rhs_imm = tpde::util::sext(rhs_op.imm64(), int_width);
        rhs_op = ValuePartRef(this, &rhs_imm, 8, Config::GP_BANK);
      }
    }
  }

  auto res = this->result_ref_lazy(inst, 0);

  auto res_scratch = ScratchReg{derived()};

  (derived()->*(encode_ptrs[static_cast<u32>(op)][ext_width / 32 - 1]))(
      std::move(lhs_op), std::move(rhs_op), res_scratch);

  this->set_value(res, res_scratch);

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_binary_op(
    const llvm::BinaryOperator *inst, FloatBinaryOp op) noexcept {
  auto *inst_ty = inst->getType();
  auto *scalar_ty = inst_ty->getScalarType();

  if (inst_ty->isFP128Ty()) {
    LibFunc lf;
    switch (op) {
    case FloatBinaryOp::add: lf = LibFunc::addtf3; break;
    case FloatBinaryOp::sub: lf = LibFunc::subtf3; break;
    case FloatBinaryOp::mul: lf = LibFunc::multf3; break;
    case FloatBinaryOp::div: lf = LibFunc::divtf3; break;
    case FloatBinaryOp::rem: return false;
    default: TPDE_UNREACHABLE("invalid FloatBinaryOp");
    }
    SymRef sym = get_libfunc_sym(lf);
    std::array<IRValueRef, 2> srcs{inst->getOperand(0), inst->getOperand(1)};

    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
    derived()->create_helper_call(srcs, {&res_ref, 1}, sym);
    return true;
  }

  const bool is_double = scalar_ty->isDoubleTy();
  if (!scalar_ty->isFloatTy() && !scalar_ty->isDoubleTy()) {
    return false;
  }

  if (op == FloatBinaryOp::rem) {
    if (inst_ty->isVectorTy()) {
      return false;
    }
    // TODO(ts): encodegen cannot encode calls atm
    derived()->create_frem_calls(inst->getOperand(0),
                                 inst->getOperand(1),
                                 this->result_ref_lazy(inst, 0),
                                 is_double);
    return true;
  }

  using EncodeFnTy =
      bool (Derived::*)(GenericValuePart &&, GenericValuePart &&, ScratchReg &);
  EncodeFnTy encode_fn = nullptr;

  switch (this->adaptor->val_info(inst).type) {
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

  auto res = this->result_ref_lazy(inst, 0);
  auto lhs = this->val_ref(inst->getOperand(0), 0);
  auto rhs = this->val_ref(inst->getOperand(1), 0);
  ScratchReg res_scratch{derived()};
  if (!(derived()->*encode_fn)(std::move(lhs), std::move(rhs), res_scratch)) {
    return false;
  }
  this->set_value(res, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_fneg(
    const llvm::UnaryOperator *inst) noexcept {
  auto *scalar_ty = inst->getType()->getScalarType();
  const bool is_double = scalar_ty->isDoubleTy();
  if (!scalar_ty->isFloatTy() && !scalar_ty->isDoubleTy()) {
    return false;
  }

  auto src_ref = this->val_ref(inst->getOperand(0), 0);
  auto res_ref = this->result_ref_lazy(inst, 0);
  auto res_scratch = ScratchReg{derived()};
  switch (this->adaptor->val_info(inst).type) {
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
    const llvm::CastInst *inst) noexcept {
  auto *src_val = inst->getOperand(0);
  auto *src_ty = src_val->getType();
  auto *dst_ty = inst->getType();

  auto res_ref = this->result_ref_lazy(inst, 0);

  ScratchReg res_scratch{derived()};
  SymRef sym;
  if (src_ty->isDoubleTy() && dst_ty->isFloatTy()) {
    auto src_ref = this->val_ref(src_val, 0);
    derived()->encode_f64tof32(std::move(src_ref), res_scratch);
  } else if (src_ty->isFP128Ty() && dst_ty->isFloatTy()) {
    sym = get_libfunc_sym(LibFunc::trunctfsf2);
  } else if (src_ty->isFP128Ty() && dst_ty->isDoubleTy()) {
    sym = get_libfunc_sym(LibFunc::trunctfdf2);
  } else if (src_ty->isFloatTy() && dst_ty->isDoubleTy()) {
    auto src_ref = this->val_ref(src_val, 0);
    derived()->encode_f32tof64(std::move(src_ref), res_scratch);
  } else if (src_ty->isFloatTy() && dst_ty->isFP128Ty()) {
    sym = get_libfunc_sym(LibFunc::extendsftf2);
  } else if (src_ty->isDoubleTy() && dst_ty->isFP128Ty()) {
    sym = get_libfunc_sym(LibFunc::extenddftf2);
  }

  if (res_scratch.has_reg()) {
    this->set_value(res_ref, res_scratch);
  } else if (sym.valid()) {
    IRValueRef src_ref = src_val;
    derived()->create_helper_call({&src_ref, 1}, {&res_ref, 1}, sym);
  } else {
    return false;
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_float_to_int(
    const llvm::CastInst *inst, const bool sign) noexcept {
  const llvm::Value *src_val = inst->getOperand(0);
  auto *src_ty = src_val->getType();
  const auto bit_width = inst->getType()->getIntegerBitWidth();

  if (bit_width > 64) {
    return false;
  }

  if (src_ty->isFP128Ty()) {
    LibFunc lf = sign ? LibFunc::fixtfdi : LibFunc::fixunstfdi;
    SymRef sym = get_libfunc_sym(lf);

    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
    derived()->create_helper_call({&src_val, 1}, {&res_ref, 1}, sym);
    return true;
  }

  if (!src_ty->isFloatTy() && !src_ty->isDoubleTy()) {
    return false;
  }

  const auto src_double = src_ty->isDoubleTy();

  auto src_ref = this->val_ref(src_val, 0);
  auto res_ref = this->result_ref_lazy(inst, 0);
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
    const llvm::CastInst *inst, const bool sign) noexcept {
  const llvm::Value *src_val = inst->getOperand(0);
  auto *dst_ty = inst->getType();
  auto bit_width = src_val->getType()->getIntegerBitWidth();

  if (bit_width > 64) {
    return false;
  }

  if (dst_ty->isFP128Ty()) {
    LibFunc lf;
    if (bit_width == 32) {
      lf = sign ? LibFunc::floatsitf : LibFunc::floatunsitf;
    } else if (bit_width == 64) {
      lf = sign ? LibFunc::floatditf : LibFunc::floatunditf;
    } else {
      // TODO: extend, but create_helper_call currently takes only an IRValueRef
      return false;
    }

    SymRef sym = get_libfunc_sym(lf);

    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
    derived()->create_helper_call({&src_val, 1}, {&res_ref, 1}, sym);
    return true;
  }

  if (!dst_ty->isFloatTy() && !dst_ty->isDoubleTy()) {
    return false;
  }

  const auto dst_double = dst_ty->isDoubleTy();

  GenericValuePart src_op = this->val_ref(src_val, 0);
  auto res_ref = this->result_ref_lazy(inst, 0);
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
    const llvm::TruncInst *inst) noexcept {
  // this is a no-op since every operation that depends on it will
  // zero/sign-extend the value anyways
  auto res_ref = this->result_ref_lazy(inst, 0);
  res_ref.set_value(this->val_ref(inst->getOperand(0), 0));
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_ext(
    const llvm::CastInst *inst, bool sign) noexcept {
  auto *src_val = inst->getOperand(0);

  unsigned src_width = src_val->getType()->getIntegerBitWidth();
  unsigned dst_width = inst->getType()->getIntegerBitWidth();
  assert(dst_width >= src_width);

  auto src_ref = this->val_ref(src_val, 0);

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
        src_ref.load_to_reg();
      }
      res_scratch.alloc_specific(src_ref.salvage());
    } else {
      auto src = src_ref.load_to_reg();
      derived()->mov(res_scratch.alloc_gp(), src, 8);
    }
  }

  auto res_ref = this->result_ref_lazy(inst, 0);

  if (dst_width == 128) {
    if (src_width > 64) {
      return false;
    }
    res_ref.inc_ref_count();
    auto res_ref_high = this->result_ref_lazy(inst, 1);
    ScratchReg scratch_high{derived()};

    if (sign) {
      derived()->encode_fill_with_sign64(res_scratch.cur_reg(), scratch_high);
    } else {
      u64 zero = 0;
      derived()->materialize_constant(
          &zero, Config::GP_BANK, 8, scratch_high.alloc_gp());
    }

    this->set_value(res_ref_high, scratch_high);
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ptr_to_int(
    const llvm::PtrToIntInst *inst) noexcept {
  // this is a no-op since every operation that depends on it will
  // zero/sign-extend the value anyways
  auto res_ref = this->result_ref_lazy(inst, 0);
  res_ref.set_value(this->val_ref(inst->getOperand(0), 0));
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_int_to_ptr(
    const llvm::IntToPtrInst *inst) noexcept {
  // zero-extend the value
  auto *src_val = inst->getOperand(0);
  const auto bit_width = src_val->getType()->getIntegerBitWidth();

  auto src_ref = this->val_ref(src_val, 0);
  auto res_ref = this->result_ref_lazy(inst, 0);
  if (bit_width == 64) {
    // no-op
    res_ref.set_value(std::move(src_ref));
    return true;
  } else if (bit_width < 64) {
    auto res = derived()->ext_int(std::move(src_ref), false, bit_width, 64);
    this->set_value(res_ref, res);
    return true;
  }

  return false;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_bitcast(
    const llvm::BitCastInst *inst) noexcept {
  // at most this should be fine to implement as a copy operation
  // as the values cannot be aggregates
  const auto src = inst->getOperand(0);

  const auto src_parts = this->adaptor->val_parts(src);
  const auto dst_parts = this->adaptor->val_parts(inst);
  // TODO(ts): support 128bit values
  if (src_parts.count() != 1 || dst_parts.count() != 1) {
    return false;
  }

  auto src_ref = this->val_ref(src, 0);

  if (src_parts.reg_bank(0) == dst_parts.reg_bank(0)) {
    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
    res_ref.set_value(std::move(src_ref));
  } else {
    ValuePartRef res_ref = this->result_ref_eager(inst, 0);
    AsmReg src_reg = src_ref.load_to_reg();
    derived()->mov(res_ref.cur_reg(), src_reg, res_ref.part_size());
    this->set_value(res_ref, res_ref.cur_reg());
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_extract_value(
    const llvm::ExtractValueInst *extract) noexcept {
  auto src = extract->getAggregateOperand();

  auto [first_part, last_part] =
      this->adaptor->complex_part_for_index(src, extract->getIndices());

  for (unsigned i = first_part; i <= last_part; i++) {
    auto part_ref = this->val_ref(src, i);
    if (i != last_part) {
      part_ref.inc_ref_count();
    }

    AsmReg orig;
    auto res_ref =
        this->result_ref_salvage_with_original(extract,
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
    const llvm::InsertValueInst *insert) noexcept {
  auto agg = insert->getAggregateOperand();
  auto ins = insert->getInsertedValueOperand();

  unsigned part_count = this->adaptor->val_part_count(agg);
  auto [first_part, last_part] =
      this->adaptor->complex_part_for_index(insert, insert->getIndices());

  for (unsigned i = 0; i < part_count; i++) {
    ValuePartRef val_ref{this};
    bool inc_ref_count;
    if (i >= first_part && i <= last_part) {
      val_ref = this->val_ref(ins, i - first_part);
      inc_ref_count = i != last_part;
    } else {
      val_ref = this->val_ref(agg, i);
      inc_ref_count = i != part_count - 1 &&
                      (last_part != part_count - 1 || i != first_part - 1);
    }
    if (inc_ref_count) {
      val_ref.inc_ref_count();
    }
    AsmReg orig;
    auto res_ref = this->result_ref_salvage_with_original(
        insert, i, std::move(val_ref), orig, inc_ref_count ? 2 : 1);
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
  assert(this->adaptor->val_part_count(vec) == 1);

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
  assert(this->adaptor->val_part_count(vec) == 1);

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
    const llvm::ExtractElementInst *inst) noexcept {
  llvm::Value *src = inst->getOperand(0);
  llvm::Value *index = inst->getOperand(1);

  auto *vec_ty = llvm::cast<llvm::FixedVectorType>(src->getType());
  unsigned nelem = vec_ty->getNumElements();
  assert((nelem & (nelem - 1)) == 0 && "vector nelem must be power of two");
  assert(index->getType()->getIntegerBitWidth() >= 8);

  ValuePartRef result = this->result_ref_lazy(inst, 0);
  LLVMBasicValType bvt = this->adaptor->val_info(inst).type;

  ScratchReg scratch_res{this};
  if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(index)) {
    unsigned cidx = ci->getZExtValue();
    derived()->extract_element(src, cidx, bvt, scratch_res);

    (void)this->val_ref(src, 0); // ref-counting
    this->set_value(result, scratch_res);
    return true;
  }

  // TODO: deduplicate with code above somehow?
  // First, copy value into the spill slot.
  ValuePartRef vec_ref = this->val_ref(src, 0);
  vec_ref.spill();
  vec_ref.unlock();

  // Second, create address. Mask index, out-of-bounds access are just poison.
  ScratchReg idx_scratch{this};
  GenericValuePart addr = derived()->val_spill_slot(vec_ref);
  auto &expr = std::get<typename GenericValuePart::Expr>(addr.state);
  u64 mask = nelem - 1;
  derived()->encode_landi64(this->val_ref(index, 0),
                            ValuePartRef{this, &mask, 8, Config::GP_BANK},
                            idx_scratch);
  assert(expr.scale == 0);
  expr.scale = this->adaptor->basic_ty_part_size(bvt);
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
    const llvm::InsertElementInst *inst) noexcept {
  llvm::Value *index = inst->getOperand(2);

  auto *vec_ty = llvm::cast<llvm::FixedVectorType>(inst->getType());
  unsigned nelem = vec_ty->getNumElements();
  assert((nelem & (nelem - 1)) == 0 && "vector nelem must be power of two");
  assert(index->getType()->getIntegerBitWidth() >= 8);

  auto ins = inst->getOperand(1);
  ValuePartRef val = this->val_ref(ins, 0);

  ValuePartRef result = this->result_ref_lazy(inst, 0);
  auto [bvt, _] = this->adaptor->val_basic_type_uncached(ins, false);
  assert(bvt != LLVMBasicValType::complex);

  // We do the dynamic insert in the spill slot of result.
  // TODO: reuse spill slot of vec_ref if possible.

  // First, copy value into the spill slot. We must also do this for constant
  // indices, because the value reference must always be initialized.
  {
    ValuePartRef vec_ref = this->val_ref(inst->getOperand(0), 0);
    AsmReg orig_reg = vec_ref.load_to_reg();
    if (vec_ref.can_salvage()) {
      result.set_value(std::move(vec_ref));
    } else {
      // TODO: don't spill when target insert_element doesn't need it?
      unsigned frame_off = result.assignment().frame_off();
      unsigned part_size = result.assignment().part_size();
      derived()->spill_reg(orig_reg, frame_off, part_size);
    }
  }

  if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(index)) {
    unsigned cidx = ci->getZExtValue();
    derived()->insert_element(inst, cidx, bvt, std::move(val));
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
  u64 mask = nelem - 1;
  derived()->encode_landi64(this->val_ref(index, 0),
                            ValuePartRef{this, &mask, 8, Config::GP_BANK},
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
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_shuffle_vector(
    const llvm::ShuffleVectorInst *inst) noexcept {
  llvm::Value *lhs = inst->getOperand(0);
  llvm::Value *rhs = inst->getOperand(1);

  auto *vec_ty = llvm::cast<llvm::FixedVectorType>(inst->getType());
  unsigned nelem = vec_ty->getNumElements();
  assert((nelem & (nelem - 1)) == 0 && "vector nelem must be power of two");

  // TODO: deduplicate with adaptor
  LLVMBasicValType bvt;
  auto *elem_ty = vec_ty->getElementType();
  if (elem_ty->isFloatTy()) {
    bvt = LLVMBasicValType::f32;
  } else if (elem_ty->isDoubleTy()) {
    bvt = LLVMBasicValType::f64;
  } else if (elem_ty->isIntegerTy(8)) {
    bvt = LLVMBasicValType::i8;
  } else if (elem_ty->isIntegerTy(16)) {
    bvt = LLVMBasicValType::i16;
  } else if (elem_ty->isIntegerTy(32)) {
    bvt = LLVMBasicValType::i32;
  } else if (elem_ty->isIntegerTy(64)) {
    bvt = LLVMBasicValType::i64;
  } else {
    TPDE_UNREACHABLE("invalid element type for shufflevector");
  }

  {
    ScratchReg tmp{this};
    ValuePartRef result = this->result_ref_lazy(inst, 0);
    tmp.alloc(result.bank());
    this->set_value(result, tmp);
  }

  ScratchReg tmp{this};
  llvm::ArrayRef<int> mask = inst->getShuffleMask();
  for (unsigned i = 0; i < nelem; i++) {
    if (mask[i] == llvm::PoisonMaskElem) {
      continue;
    }
    IRValueRef src = unsigned(mask[i]) < nelem ? lhs : rhs;
    if (auto *cst = llvm::dyn_cast<llvm::Constant>(src)) {
      auto *cst_elem = cst->getAggregateElement(i);
      u64 const_elem;
      if (llvm::isa<llvm::PoisonValue>(cst_elem)) {
        continue;
      } else if (auto *ci = llvm::dyn_cast<llvm::ConstantInt>(cst_elem)) {
        const_elem = ci->getZExtValue();
      } else if (auto *cfp = llvm::dyn_cast<llvm::ConstantFP>(cst_elem)) {
        const_elem = cfp->getValue().bitcastToAPInt().getZExtValue();
      } else {
        TPDE_UNREACHABLE("invalid constant element type");
      }
      auto bank = this->adaptor->basic_ty_part_bank(bvt);
      auto size = this->adaptor->basic_ty_part_size(bvt);
      ValuePartRef const_ref{this, &const_elem, size, bank};
      derived()->materialize_constant(const_ref, tmp);
    } else {
      derived()->extract_element(src, mask[i] & (nelem - 1), bvt, tmp);
    }
    derived()->insert_element(inst, i, bvt, std::move(tmp));
  }
  (void)this->val_ref(lhs, 0); // ref-counting
  (void)this->val_ref(rhs, 0); // ref-counting
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_cmpxchg(
    const llvm::AtomicCmpXchgInst *cmpxchg) noexcept {
  auto *cmp_val = cmpxchg->getCompareOperand();
  auto *new_val = cmpxchg->getNewValOperand();
  auto *val_ty = new_val->getType();
  unsigned width = 64;
  if (val_ty->isIntegerTy()) {
    width = val_ty->getIntegerBitWidth();
    // LLVM only permits widths that are a power of two and >= 8.
    if (width > 64) {
      return false;
    }
  }

  unsigned width_idx;
  switch (width) {
  case 8: width_idx = 0; break;
  case 16: width_idx = 1; break;
  case 32: width_idx = 2; break;
  case 64: width_idx = 3; break;
  default: return false;
  }

  // ptr, cmp, new_val, old_val, success
  using EncodeFnTy = bool (Derived::*)(GenericValuePart &&,
                                       GenericValuePart &&,
                                       GenericValuePart &&,
                                       ScratchReg &,
                                       ScratchReg &);
  static const auto fns = []() constexpr {
    using enum llvm::AtomicOrdering;
    std::array<EncodeFnTy[size_t(LAST) + 1], 4> res;
    res[0][u32(Monotonic)] = &Derived::encode_cmpxchg_u8_monotonic;
    res[1][u32(Monotonic)] = &Derived::encode_cmpxchg_u16_monotonic;
    res[2][u32(Monotonic)] = &Derived::encode_cmpxchg_u32_monotonic;
    res[3][u32(Monotonic)] = &Derived::encode_cmpxchg_u64_monotonic;
    res[0][u32(Acquire)] = &Derived::encode_cmpxchg_u8_acquire;
    res[1][u32(Acquire)] = &Derived::encode_cmpxchg_u16_acquire;
    res[2][u32(Acquire)] = &Derived::encode_cmpxchg_u32_acquire;
    res[3][u32(Acquire)] = &Derived::encode_cmpxchg_u64_acquire;
    res[0][u32(Release)] = &Derived::encode_cmpxchg_u8_release;
    res[1][u32(Release)] = &Derived::encode_cmpxchg_u16_release;
    res[2][u32(Release)] = &Derived::encode_cmpxchg_u32_release;
    res[3][u32(Release)] = &Derived::encode_cmpxchg_u64_release;
    res[0][u32(AcquireRelease)] = &Derived::encode_cmpxchg_u8_acqrel;
    res[1][u32(AcquireRelease)] = &Derived::encode_cmpxchg_u16_acqrel;
    res[2][u32(AcquireRelease)] = &Derived::encode_cmpxchg_u32_acqrel;
    res[3][u32(AcquireRelease)] = &Derived::encode_cmpxchg_u64_acqrel;
    res[0][u32(SequentiallyConsistent)] = &Derived::encode_cmpxchg_u8_seqcst;
    res[1][u32(SequentiallyConsistent)] = &Derived::encode_cmpxchg_u16_seqcst;
    res[2][u32(SequentiallyConsistent)] = &Derived::encode_cmpxchg_u32_seqcst;
    res[3][u32(SequentiallyConsistent)] = &Derived::encode_cmpxchg_u64_seqcst;
    return res;
  }();

  auto *ptr_val = cmpxchg->getPointerOperand();
  assert(ptr_val->getType()->isPointerTy());
  auto ptr_ref = this->val_ref(ptr_val, 0);

  auto cmp_ref = this->val_ref(cmp_val, 0);
  auto new_ref = this->val_ref(new_val, 0);

  auto res_ref = this->result_ref_lazy(cmpxchg, 0);
  res_ref.inc_ref_count();
  auto res_ref_high = this->result_ref_lazy(cmpxchg, 1);

  ScratchReg orig_scratch{derived()};
  ScratchReg succ_scratch{derived()};

  llvm::AtomicOrdering order = cmpxchg->getMergedOrdering();
  EncodeFnTy encode_fn = fns[width_idx][size_t(order)];
  assert(encode_fn && "invalid cmpxchg ordering");
  if (!(derived()->*encode_fn)(std::move(ptr_ref),
                               std::move(cmp_ref),
                               std::move(new_ref),
                               orig_scratch,
                               succ_scratch)) {
    return false;
  }

  this->set_value(res_ref, orig_scratch);
  this->set_value(res_ref_high, succ_scratch);

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
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_atomicrmw(
    const llvm::AtomicRMWInst *rmw) noexcept {
  llvm::Type *ty = rmw->getType();
  unsigned size = this->adaptor->mod->getDataLayout().getTypeSizeInBits(ty);
  // This is checked by the IR verifier.
  assert(size >= 8 && (size & (size - 1)) == 0 && "invalid atomicrmw size");
  // Unaligned atomicrmw is very tricky to implement. While x86-64 supports
  // unaligned atomics, this is not always supported (to prevent split locks
  // from locking the bus in shared systems). Other platforms don't support
  // unaligned accesses at all. Therefore, supporting this needs to go through
  // library support from libgcc or compiler-rt with a cmpxchg loop.
  // TODO: implement support for unaligned atomics
  // TODO: do this check without consulting DataLayout
  if (rmw->getAlign().value() < size / 8) {
    TPDE_LOG_ERR("unaligned atomicrmw is not supported (align={} < size={})",
                 rmw->getAlign().value(),
                 size / 8);
    return false;
  }

  auto bvt = this->adaptor->val_info(rmw).type;

  // TODO: implement non-seq_cst orderings more efficiently
  // TODO: use more efficient implementation when the result is not used. On
  // x86-64, the current implementation gives many cmpxchg loops.
  bool (Derived::*fn)(GenericValuePart &&, GenericValuePart &&, ScratchReg &) =
      nullptr;
  switch (rmw->getOperation()) {
  case llvm::AtomicRMWInst::Xchg:
    // TODO: support f32/f64
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_xchg_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_xchg_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_xchg_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_xchg_u64_seqcst; break;
    case ptr: fn = &Derived::encode_atomic_xchg_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Add:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_add_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_add_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_add_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_add_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Sub:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_sub_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_sub_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_sub_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_sub_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::And:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_and_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_and_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_and_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_and_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Nand:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_nand_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_nand_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_nand_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_nand_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Or:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_or_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_or_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_or_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_or_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Xor:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_xor_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_xor_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_xor_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_xor_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Min:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_min_i8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_min_i16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_min_i32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_min_i64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::Max:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_max_i8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_max_i16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_max_i32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_max_i64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::UMin:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_min_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_min_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_min_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_min_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::UMax:
    switch (bvt) {
      using enum LLVMBasicValType;
    case i8: fn = &Derived::encode_atomic_max_u8_seqcst; break;
    case i16: fn = &Derived::encode_atomic_max_u16_seqcst; break;
    case i32: fn = &Derived::encode_atomic_max_u32_seqcst; break;
    case i64: fn = &Derived::encode_atomic_max_u64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::FAdd:
    switch (bvt) {
      using enum LLVMBasicValType;
    case f32: fn = &Derived::encode_atomic_add_f32_seqcst; break;
    case f64: fn = &Derived::encode_atomic_add_f64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::FSub:
    switch (bvt) {
      using enum LLVMBasicValType;
    case f32: fn = &Derived::encode_atomic_sub_f32_seqcst; break;
    case f64: fn = &Derived::encode_atomic_sub_f64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::FMin:
    switch (bvt) {
      using enum LLVMBasicValType;
    case f32: fn = &Derived::encode_atomic_min_f32_seqcst; break;
    case f64: fn = &Derived::encode_atomic_min_f64_seqcst; break;
    default: return false;
    }
    break;
  case llvm::AtomicRMWInst::FMax:
    switch (bvt) {
      using enum LLVMBasicValType;
    case f32: fn = &Derived::encode_atomic_max_f32_seqcst; break;
    case f64: fn = &Derived::encode_atomic_max_f64_seqcst; break;
    default: return false;
    }
    break;
  default: return false;
  }

  auto ptr_ref = this->val_ref(rmw->getPointerOperand(), 0);
  auto val_ref = this->val_ref(rmw->getValOperand(), 0);
  auto res_ref = this->result_ref_lazy(rmw, 0);
  ScratchReg res{this};
  if (!(derived()->*fn)(std::move(ptr_ref), std::move(val_ref), res)) {
    return false;
  }
  this->set_value(res_ref, res);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_fence(
    const llvm::FenceInst *fence) noexcept {
  if (fence->getSyncScopeID() == llvm::SyncScope::SingleThread) {
    // memory barrier only
    return true;
  }

  switch (fence->getOrdering()) {
    using enum llvm::AtomicOrdering;
  case Acquire: derived()->encode_fence_acq(); break;
  case Release: derived()->encode_fence_rel(); break;
  case AcquireRelease: derived()->encode_fence_acqrel(); break;
  case SequentiallyConsistent: derived()->encode_fence_seqcst(); break;
  default: return false;
  }

  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_freeze(
    const llvm::FreezeInst *inst) noexcept {
  // essentially a no-op
  auto *src_val = inst->getOperand(0);

  const auto part_count = this->adaptor->val_part_count(src_val);

  for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
    const auto last_part = (part_idx == part_count - 1);
    auto src_ref = this->val_ref(src_val, part_idx);
    if (!last_part) {
      src_ref.inc_ref_count();
    }
    AsmReg orig;
    auto res_ref = this->result_ref_salvage_with_original(
        inst, part_idx, std::move(src_ref), orig, last_part ? 1 : 2);
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
    const llvm::CallBase *call) noexcept {
  std::variant<SymRef, ValuePartRef> call_target;
  auto var_arg = false;

  if (auto *fn = call->getCalledFunction(); fn) {
    if (fn->isIntrinsic()) {
      return compile_intrin(llvm::cast<llvm::IntrinsicInst>(call));
    }

    // this is a direct call
    call_target = global_sym(fn);
    var_arg = fn->getFunctionType()->isVarArg();
  } else if (call->isInlineAsm()) {
    return derived()->compile_inline_asm(call);
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
      auto target_ref = this->val_ref(op, 0);
      call_target = std::move(target_ref);
    }
  }

  return derived()->compile_call_inner(call, call_target, var_arg);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_select(
    const llvm::SelectInst *inst) noexcept {
  auto cond = this->val_ref(inst->getOperand(0), 0);
  auto lhs = this->val_ref(inst->getOperand(1), 0);
  auto rhs = this->val_ref(inst->getOperand(2), 0);

  ScratchReg res_scratch{derived()};
  auto res_ref = this->result_ref_lazy(inst, 0);

  switch (this->adaptor->val_info(inst).type) {
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
  case complex: {
    // Handle case of complex with two i64 as i128, this is extremely hacky...
    // TODO(ts): support full complex types using branches
    const auto parts = this->adaptor->val_parts(inst);
    if (parts.count() != 2 || parts.reg_bank(0) != 0 ||
        parts.reg_bank(1) != 0) {
      return false;
    }
  }
    [[fallthrough]];
  case i128: {
    lhs.inc_ref_count();
    rhs.inc_ref_count();
    auto lhs_high = this->val_ref(inst->getOperand(1), 1);
    auto rhs_high = this->val_ref(inst->getOperand(2), 1);

    ScratchReg res_scratch_high{derived()};
    auto res_ref_high = this->result_ref_lazy(inst, 1);

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
typename LLVMCompilerBase<Adaptor, Derived, Config>::GenericValuePart
    LLVMCompilerBase<Adaptor, Derived, Config>::resolved_gep_to_addr(
        ResolvedGEP &gep) noexcept {
  typename GenericValuePart::Expr addr{};
  if (std::holds_alternative<ScratchReg>(gep.base)) {
    addr.base = std::move(std::get<ScratchReg>(gep.base));
  } else {
    auto &ref = std::get<ValuePartRef>(gep.base);
    auto reg = ref.load_to_reg();
    if (ref.can_salvage()) {
      ScratchReg scratch{this};
      scratch.alloc_specific(ref.salvage());
      addr.base = std::move(scratch);
    } else {
      addr.base = reg;
    }
  }

  if (gep.scale) {
    assert(gep.index);
    if (std::holds_alternative<ScratchReg>(*gep.index)) {
      assert(gep.idx_size_bits == 64);
      addr.index = std::move(std::get<ScratchReg>(*gep.index));
    } else {
      // check for sign-extension
      auto &ref = std::get<ValuePartRef>(*gep.index);
      const auto idx_size_bits = gep.idx_size_bits;
      if (idx_size_bits < 64) {
        AsmReg src_reg = ref.load_to_reg();
        AsmReg dst_reg;
        ScratchReg scratch{this};
        if (ref.can_salvage()) {
          dst_reg = scratch.alloc_specific(ref.salvage());
        } else {
          dst_reg = scratch.alloc_gp();
        }
        derived()->ext_int(dst_reg, src_reg, true, idx_size_bits, 64);
        addr.index = std::move(scratch);
      } else {
        assert(idx_size_bits == 64);
        addr.index = ref.load_to_reg();
      }
    }
  }

  addr.scale = gep.scale;
  addr.disp = gep.displacement;

  return addr;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_gep(
    const llvm::GetElementPtrInst *gep, InstRange remaining) noexcept {
  auto ptr = gep->getPointerOperand();

  auto resolved = ResolvedGEP{
      .base = this->val_ref(ptr, 0),
      .index = ScratchReg{derived()},
      .scale = 0,
      .idx_size_bits = 0,
      .displacement = 0,
  };

  auto &data_layout = this->adaptor->mod->getDataLayout();

  const auto handle_gep_indices = [this, &data_layout, &resolved](
                                      llvm::Type *source_ty,
                                      const llvm::Use *idx_start,
                                      const llvm::Use *idx_end) {
    if (idx_start == idx_end) {
      return;
    }

    auto *cur_ty = source_ty;
    for (auto it = idx_start; it != idx_end; ++it) {
      if (auto *Const = llvm::dyn_cast<llvm::ConstantInt>(it->get())) {
        i64 off_disp;
        if (it == idx_start) {
          // array index
          off_disp =
              data_layout.getTypeAllocSize(cur_ty) * Const->getSExtValue();
        } else {
          if (cur_ty->isStructTy()) {
            auto *struct_layout = data_layout.getStructLayout(
                llvm::cast<llvm::StructType>(cur_ty));
            cur_ty = cur_ty->getStructElementType(Const->getZExtValue());
            off_disp = struct_layout->getElementOffset(Const->getZExtValue());
          } else {
            assert(cur_ty->isArrayTy());
            cur_ty = cur_ty->getArrayElementType();
            off_disp =
                data_layout.getTypeAllocSize(cur_ty) * Const->getSExtValue();
          }
        }
        resolved.displacement += off_disp;
        continue;
      }

      // A non-constant GEP. This must either be an offset calculation (for
      // index == 0) or an array traversal
      assert(it == idx_start || cur_ty->isArrayTy());
      if (it != idx_start) {
        cur_ty = cur_ty->getArrayElementType();
      }

      if (resolved.scale) {
        // need to convert to simple base register
        derived()->resolved_gep_to_base_reg(resolved);
      }

      auto idx_ref = this->val_ref(it->get(), 0);

      auto *idx_ty = it->get()->getType();
      const auto idx_width = idx_ty->getIntegerBitWidth();
      assert(idx_width > 0 && idx_width <= 64);

      resolved.idx_size_bits = idx_width;
      resolved.index = std::move(idx_ref);
      resolved.scale = data_layout.getTypeAllocSize(cur_ty);
    }
  };

  handle_gep_indices(
      gep->getSourceElementType(), gep->idx_begin(), gep->idx_end());

  // fuse geps
  // TODO: use llvm statistic or analyzer liveness stat?
  const llvm::Instruction *final = gep;
  while (gep->hasOneUse() && remaining.from != remaining.to) {
    auto *next_val = *remaining.from;
    auto *next_gep = llvm::dyn_cast<llvm::GetElementPtrInst>(next_val);
    if (!next_gep || next_gep->getPointerOperand() != gep ||
        gep->getResultElementType() != next_gep->getResultElementType()) {
      break;
    }

    if (next_gep->hasIndices()) {
      handle_gep_indices(next_gep->getSourceElementType(),
                         next_gep->idx_begin(),
                         next_gep->idx_end());
    }

    this->adaptor->inst_set_fused(next_val, true);
    final = next_val; // we set the result for nextInst
    gep = next_gep;
    ++remaining.from;
  }

  GenericValuePart addr = this->resolved_gep_to_addr(resolved);

  if (gep->hasOneUse() && remaining.from != remaining.to) {
    auto *next_val = *remaining.from;
    if (auto *store = llvm::dyn_cast<llvm::StoreInst>(next_val);
        store && store->getPointerOperand() == gep) {
      this->adaptor->inst_set_fused(next_val, true);
      return compile_store_generic(store, std::move(addr));
    }
    if (auto *load = llvm::dyn_cast<llvm::LoadInst>(next_val);
        load && load->getPointerOperand() == gep) {
      this->adaptor->inst_set_fused(next_val, true);
      return compile_load_generic(load, std::move(addr));
    }
  }

  auto res_ref = this->result_ref_lazy(final, 0);

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
    const llvm::FCmpInst *cmp) noexcept {
  auto *cmp_ty = cmp->getOperand(0)->getType();
  const auto pred = cmp->getPredicate();

  if (pred == llvm::CmpInst::FCMP_FALSE || pred == llvm::CmpInst::FCMP_TRUE) {
    auto res_ref = this->result_ref_eager(cmp, 0);
    u64 val = pred == llvm::CmpInst::FCMP_FALSE ? 0u : 1u;
    auto const_ref = ValuePartRef{this, &val, 1, Config::GP_BANK};
    derived()->materialize_constant(const_ref, res_ref.cur_reg());
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }

  if (cmp_ty->isFP128Ty()) {
    SymRef sym;
    llvm::CmpInst::Predicate cmp_pred = llvm::CmpInst::ICMP_EQ;
    switch (pred) {
    case llvm::CmpInst::FCMP_OEQ:
      sym = get_libfunc_sym(LibFunc::eqtf2);
      cmp_pred = llvm::CmpInst::ICMP_EQ;
      break;
    case llvm::CmpInst::FCMP_UNE:
      sym = get_libfunc_sym(LibFunc::netf2);
      cmp_pred = llvm::CmpInst::ICMP_NE;
      break;
    case llvm::CmpInst::FCMP_OGT:
      sym = get_libfunc_sym(LibFunc::gttf2);
      cmp_pred = llvm::CmpInst::ICMP_SGT;
      break;
    case llvm::CmpInst::FCMP_ULE:
      sym = get_libfunc_sym(LibFunc::gttf2);
      cmp_pred = llvm::CmpInst::ICMP_SLE;
      break;
    case llvm::CmpInst::FCMP_OGE:
      sym = get_libfunc_sym(LibFunc::getf2);
      cmp_pred = llvm::CmpInst::ICMP_SGE;
      break;
    case llvm::CmpInst::FCMP_ULT:
      sym = get_libfunc_sym(LibFunc::getf2);
      cmp_pred = llvm::CmpInst::ICMP_SLT;
      break;
    case llvm::CmpInst::FCMP_OLT:
      sym = get_libfunc_sym(LibFunc::lttf2);
      cmp_pred = llvm::CmpInst::ICMP_SLT;
      break;
    case llvm::CmpInst::FCMP_UGE:
      sym = get_libfunc_sym(LibFunc::lttf2);
      cmp_pred = llvm::CmpInst::ICMP_SGE;
      break;
    case llvm::CmpInst::FCMP_OLE:
      sym = get_libfunc_sym(LibFunc::letf2);
      cmp_pred = llvm::CmpInst::ICMP_SLE;
      break;
    case llvm::CmpInst::FCMP_UGT:
      sym = get_libfunc_sym(LibFunc::letf2);
      cmp_pred = llvm::CmpInst::ICMP_SGT;
      break;
    case llvm::CmpInst::FCMP_ORD:
      sym = get_libfunc_sym(LibFunc::unordtf2);
      cmp_pred = llvm::CmpInst::ICMP_EQ;
      break;
    case llvm::CmpInst::FCMP_UNO:
      sym = get_libfunc_sym(LibFunc::unordtf2);
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

    IRValueRef lhs = cmp->getOperand(0);
    IRValueRef rhs = cmp->getOperand(1);
    std::array<IRValueRef, 2> args{lhs, rhs};

    ValuePartRef res_ref = this->result_ref_lazy(cmp, 0);
    derived()->create_helper_call(args, {&res_ref, 1}, sym);

    ValuePartRef res_ref2 = this->val_ref(cmp, 0);
    res_ref2.inc_ref_count();
    res_ref2.load_to_reg();
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
      bool (Derived::*)(GenericValuePart &&, GenericValuePart &&, ScratchReg &);
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

  GenericValuePart lhs_op = this->val_ref(cmp->getOperand(0), 0);
  GenericValuePart rhs_op = this->val_ref(cmp->getOperand(1), 0);
  ScratchReg res_scratch{derived()};
  auto res_ref = this->result_ref_lazy(cmp, 0);

  if (!(derived()->*fn)(std::move(lhs_op), std::move(rhs_op), res_scratch)) {
    return false;
  }

  this->set_value(res_ref, res_scratch);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_switch(
    const llvm::SwitchInst *switch_inst) noexcept {
  ScratchReg scratch{this};
  AsmReg cmp_reg;
  bool width_is_32 = false;
  {
    auto arg_ref = this->val_ref(switch_inst->getCondition(), 0);
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
        u64 mask = (1ull << width) - 1;
        derived()->encode_landi32(std::move(arg_ref),
                                  ValuePartRef{this, &mask, 4, Config::GP_BANK},
                                  scratch);
      }
      cmp_reg = scratch.cur_reg();
    } else if (width == 32) {
      width_is_32 = true;
      cmp_reg = arg_ref.load_to_reg();
      // make sure we can overwrite the register when we generate a jump
      // table
      if (arg_ref.can_salvage()) {
        scratch.alloc_specific(arg_ref.salvage());
      } else if (arg_ref.has_assignment()) {
        arg_ref.unlock();
        arg_ref.reload_into_specific_fixed(this, scratch.alloc_gp());
        cmp_reg = scratch.cur_reg();
      }
    } else if (width < 64) {
      u64 mask = (1ull << width) - 1;
      derived()->encode_landi64(std::move(arg_ref),
                                ValuePartRef{this, &mask, 8, Config::GP_BANK},
                                scratch);
      cmp_reg = scratch.cur_reg();
    } else {
      cmp_reg = arg_ref.load_to_reg();
      // make sure we can overwrite the register when we generate a jump
      // table
      if (arg_ref.can_salvage()) {
        scratch.alloc_specific(arg_ref.salvage());
      } else if (arg_ref.has_assignment()) {
        arg_ref.unlock();
        arg_ref.reload_into_specific_fixed(this, scratch.alloc_gp());
        cmp_reg = scratch.cur_reg();
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
        this->adaptor->block_lookup_idx(case_val.getCaseSuccessor())));
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
      this->adaptor->block_lookup_idx(switch_inst->getDefaultDest()),
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
    const llvm::InvokeInst *invoke) noexcept {
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
  if (!this->compile_call(invoke)) {
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

  const auto unwind_block_ref = this->adaptor->block_lookup_idx(unwind_block);
  const auto normal_block_ref =
      this->adaptor->block_lookup_idx(invoke->getNormalDest());
  auto unwind_label =
      this->block_labels[(u32)this->analyzer.block_idx(unwind_block_ref)];

  // we need to check whether the call result needs to be spilled, too.
  // This needs to be done since the invoke is conceptually a branch
  auto check_res_spill = [&]() {
    auto call_res_idx = invoke;
    auto *a = this->val_assignment(this->val_idx(call_res_idx));
    if (a == nullptr) {
      // call has void result
      return;
    }

    auto cur_block = this->analyzer.block_ref(this->cur_block_idx);

    // TODO: temporarily disable the optimization, it doesn't work reliably.
    // First, if the unwind block has PHIs, we currently assume that the result
    // registers (which are the landing pad result registers) are reserveable.
    // Second, the result registers might be in the result from spill_before_br,
    // which would cause the result to be freed immediately after the branch.
    // Both cases could be handled by inspecting the result registers. However,
    // a proper solution requires some more thought. Therefore, as a temporary
    // workaround, *always* (=> && false) spill the result of an invoke if it is
    // used outside of a PHI node in the normal block.
    //
    // This used to be: if (normal_is_next) return;

    uint32_t num_phi_reads = 0;
    for (auto i : this->adaptor->block_phis(normal_block_ref)) {
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
        (u32)this->analyzer
                .liveness_info(this->adaptor->val_local_idx(call_res_idx))
                .last <= (u32)this->cur_block_idx) {
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
      SymRef sym;
      if (!C->isNullValue()) {
        assert(llvm::dyn_cast<llvm::GlobalValue>(C));
        sym = lookup_type_info_sym(C);
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
    const llvm::LandingPadInst *inst) noexcept {
  auto res_ref_first = this->result_ref_lazy(inst, 0);
  auto res_ref_second = this->result_ref_lazy(inst, 1);

  this->set_value(res_ref_first, Derived::LANDING_PAD_RES_REGS[0]);
  this->set_value(res_ref_second, Derived::LANDING_PAD_RES_REGS[1]);

  res_ref_second.reset_without_refcount();
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_resume(
    const llvm::ResumeInst *inst) noexcept {
  IRValueRef arg = inst->getOperand(0);

  const auto sym = get_libfunc_sym(LibFunc::resume);

  derived()->create_helper_call({&arg, 1}, {}, sym);
  return derived()->compile_unreachable(nullptr);
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

  const auto sym = global_sym(llvm::cast<llvm::GlobalValue>(value));

  u32 off;
  u8 tmp[8] = {};
  auto rodata = this->assembler.get_data_section(true, true);
  const auto addr_sym = this->assembler.sym_def_data(
      rodata, {}, {tmp, sizeof(tmp)}, 8, Assembler::SymBinding::LOCAL, &off);
  this->assembler.reloc_abs(rodata, sym, off, 0);

  type_info_syms.emplace_back(value, addr_sym);
  return addr_sym;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_intrin(
    const llvm::IntrinsicInst *inst) noexcept {
  const auto intrin_id = inst->getIntrinsicID();

  switch (intrin_id) {
  case llvm::Intrinsic::donothing:
  case llvm::Intrinsic::sideeffect:
  case llvm::Intrinsic::experimental_noalias_scope_decl:
  case llvm::Intrinsic::dbg_assign:
  case llvm::Intrinsic::dbg_declare:
  case llvm::Intrinsic::dbg_label: return true;
  case llvm::Intrinsic::dbg_value:
    // reference counting
    this->val_ref(inst->getOperand(1), 0);
    return true;
  case llvm::Intrinsic::assume:
  case llvm::Intrinsic::lifetime_start:
  case llvm::Intrinsic::lifetime_end:
  case llvm::Intrinsic::invariant_start:
  case llvm::Intrinsic::invariant_end:
    // reference counting
    for (llvm::Value *arg : inst->args()) {
      this->val_ref(arg, 0);
    }
    return true;
  case llvm::Intrinsic::memcpy: {
    const auto dst = inst->getOperand(0);
    const auto src = inst->getOperand(1);
    const auto len = inst->getOperand(2);

    std::array<IRValueRef, 3> args{dst, src, len};

    const auto sym = get_libfunc_sym(LibFunc::memcpy);
    derived()->create_helper_call(args, {}, sym);
    return true;
  }
  case llvm::Intrinsic::memset: {
    const auto dst = inst->getOperand(0);
    const auto val = inst->getOperand(1);
    const auto len = inst->getOperand(2);

    std::array<IRValueRef, 3> args{dst, val, len};

    const auto sym = get_libfunc_sym(LibFunc::memset);
    derived()->create_helper_call(args, {}, sym);
    return true;
  }
  case llvm::Intrinsic::memmove: {
    const auto dst = inst->getOperand(0);
    const auto src = inst->getOperand(1);
    const auto len = inst->getOperand(2);

    std::array<IRValueRef, 3> args{dst, src, len};

    const auto sym = get_libfunc_sym(LibFunc::memmove);
    derived()->create_helper_call(args, {}, sym);
    return true;
  }
  case llvm::Intrinsic::load_relative: {
    if (!inst->getOperand(1)->getType()->isIntegerTy(64)) {
      return false;
    }

    auto ptr = this->val_ref(inst->getOperand(0), 0);
    auto off = this->val_ref(inst->getOperand(1), 0);
    auto res_ref = this->result_ref_lazy(inst, 0);
    ScratchReg res{derived()};
    derived()->encode_loadreli64(std::move(ptr), std::move(off), res);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::threadlocal_address: {
    auto gv = llvm::cast<llvm::GlobalValue>(inst->getOperand(0));
    auto res_ref = this->result_ref_lazy(inst, 0);
    // TODO: optimize for different TLS access models
    ScratchReg res =
        derived()->tls_get_addr(global_sym(gv), tpde::TLSModel::GlobalDynamic);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::vaend: {
    // no-op
    this->val_ref(inst->getOperand(0), 0);
    return true;
  }
  case llvm::Intrinsic::is_fpclass: {
    return compile_is_fpclass(inst);
  }
  case llvm::Intrinsic::floor:
  case llvm::Intrinsic::ceil:
  case llvm::Intrinsic::round:
  case llvm::Intrinsic::rint:
  case llvm::Intrinsic::trunc:
  case llvm::Intrinsic::pow:
  case llvm::Intrinsic::powi:
  case llvm::Intrinsic::sin:
  case llvm::Intrinsic::cos:
  case llvm::Intrinsic::log:
  case llvm::Intrinsic::log10:
  case llvm::Intrinsic::exp: {
    // Floating-point intrinsics that can be mapped directly to libcalls.
    const auto is_double = inst->getType()->isDoubleTy();
    if (!is_double && !inst->getType()->isFloatTy()) {
      return false;
    }

    LibFunc func;
    switch (intrin_id) {
      using enum llvm::Intrinsic::IndependentIntrinsics;
    case floor: func = is_double ? LibFunc::floor : LibFunc::floorf; break;
    case ceil: func = is_double ? LibFunc::ceil : LibFunc::ceilf; break;
    case round: func = is_double ? LibFunc::round : LibFunc::roundf; break;
    case rint: func = is_double ? LibFunc::rint : LibFunc::rintf; break;
    case trunc: func = is_double ? LibFunc::trunc : LibFunc::truncf; break;
    case pow: func = is_double ? LibFunc::pow : LibFunc::powf; break;
    case powi: func = is_double ? LibFunc::powidf2 : LibFunc::powisf2; break;
    case sin: func = is_double ? LibFunc::sin : LibFunc::sinf; break;
    case cos: func = is_double ? LibFunc::cos : LibFunc::cosf; break;
    case log: func = is_double ? LibFunc::log : LibFunc::logf; break;
    case log10: func = is_double ? LibFunc::log10 : LibFunc::log10f; break;
    case exp: func = is_double ? LibFunc::exp : LibFunc::expf; break;
    default: TPDE_UNREACHABLE("invalid library fp intrinsic");
    }

    llvm::SmallVector<IRValueRef, 2> ops;
    for (auto &op : inst->args()) {
      ops.push_back(op);
    }
    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
    derived()->create_helper_call(ops, {&res_ref, 1}, get_libfunc_sym(func));
    return true;
  }
  case llvm::Intrinsic::minnum:
  case llvm::Intrinsic::maxnum:
  case llvm::Intrinsic::copysign: {
    // Floating-point intrinsics with two operands
    const auto is_double = inst->getType()->isDoubleTy();
    if (!is_double && !inst->getType()->isFloatTy()) {
      return false;
    }

    using EncodeFnTy = bool (Derived::*)(
        GenericValuePart &&, GenericValuePart &&, ScratchReg &);
    EncodeFnTy fn;
    switch (intrin_id) {
      using enum llvm::Intrinsic::IndependentIntrinsics;
    case minnum:
      fn = is_double ? &Derived::encode_minnumf64 : &Derived::encode_minnumf32;
      break;
    case maxnum:
      fn = is_double ? &Derived::encode_maxnumf64 : &Derived::encode_maxnumf32;
      break;
    case copysign:
      fn = is_double ? &Derived::encode_copysignf64
                     : &Derived::encode_copysignf32;
      break;
    default: TPDE_UNREACHABLE("invalid binary fp intrinsic");
    }

    auto lhs = this->val_ref(inst->getOperand(0), 0);
    auto rhs = this->val_ref(inst->getOperand(1), 0);
    auto res_ref = this->result_ref_lazy(inst, 0);
    ScratchReg res{derived()};
    if (!(derived()->*fn)(std::move(lhs), std::move(rhs), res)) {
      return false;
    }
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::fabs: {
    auto *val = inst->getOperand(0);
    auto *ty = val->getType();

    if (!ty->isFloatTy() && !ty->isDoubleTy()) {
      return false;
    }

    auto val_ref = this->val_ref(val, 0);
    auto res_ref = this->result_ref_lazy(inst, 0);
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

    auto val_ref = this->val_ref(val, 0);
    auto res_ref = this->result_ref_lazy(inst, 0);
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
    auto op1_ref = this->val_ref(inst->getOperand(0), 0);
    auto op2_ref = this->val_ref(inst->getOperand(1), 0);
    auto op3_ref = this->val_ref(inst->getOperand(2), 0);

    if (!inst->getType()->isFloatTy() && !inst->getType()->isDoubleTy()) {
      return false;
    }

    const auto is_double = inst->getOperand(0)->getType()->isDoubleTy();

    auto res_ref = this->result_ref_lazy(inst, 0);
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

    GenericValuePart op = this->val_ref(val, 0);
    if (width != 32 && width != 64) {
      unsigned dst_width = tpde::util::align_up(width, 32);
      op = derived()->ext_int(std::move(op), true, width, dst_width);
    }

    ScratchReg res{derived()};
    auto res_ref = this->result_ref_lazy(inst, 0);
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

    GenericValuePart lhs = this->val_ref(inst->getOperand(0), 0);
    GenericValuePart rhs = this->val_ref(inst->getOperand(1), 0);
    if (width != 32 && width != 64) {
      unsigned dst_width = tpde::util::align_up(width, 32);
      lhs = derived()->ext_int(std::move(lhs), true, width, dst_width);
      rhs = derived()->ext_int(std::move(rhs), true, width, dst_width);
    }

    ScratchReg res{derived()};
    auto res_ref = this->result_ref_lazy(inst, 0);
    using EncodeFnTy = bool (Derived::*)(
        GenericValuePart &&, GenericValuePart &&, ScratchReg &);
    EncodeFnTy encode_fn = nullptr;
    if (width <= 32) {
      switch (intrin_id) {
      case llvm::Intrinsic::umin: encode_fn = &Derived::encode_umini32; break;
      case llvm::Intrinsic::umax: encode_fn = &Derived::encode_umaxi32; break;
      case llvm::Intrinsic::smin: encode_fn = &Derived::encode_smini32; break;
      case llvm::Intrinsic::smax: encode_fn = &Derived::encode_smaxi32; break;
      default: TPDE_UNREACHABLE("invalid intrinsic");
      }
    } else {
      switch (intrin_id) {
      case llvm::Intrinsic::umin: encode_fn = &Derived::encode_umini64; break;
      case llvm::Intrinsic::umax: encode_fn = &Derived::encode_umaxi64; break;
      case llvm::Intrinsic::smin: encode_fn = &Derived::encode_smini64; break;
      case llvm::Intrinsic::smax: encode_fn = &Derived::encode_smaxi64; break;
      default: TPDE_UNREACHABLE("invalid intrinsic");
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
    GenericValuePart lhs = this->val_ref(inst->getOperand(0), 0);
    GenericValuePart rhs = this->val_ref(inst->getOperand(1), 0);
    auto res = this->result_ref_lazy(inst, 0);
    auto res_scratch = ScratchReg{derived()};
    derived()->encode_landi64(std::move(lhs), std::move(rhs), res_scratch);
    this->set_value(res, res_scratch);
    return true;
  }
  case llvm::Intrinsic::uadd_with_overflow:
    return compile_overflow_intrin(inst, OverflowOp::uadd);
  case llvm::Intrinsic::sadd_with_overflow:
    return compile_overflow_intrin(inst, OverflowOp::sadd);
  case llvm::Intrinsic::usub_with_overflow:
    return compile_overflow_intrin(inst, OverflowOp::usub);
  case llvm::Intrinsic::ssub_with_overflow:
    return compile_overflow_intrin(inst, OverflowOp::ssub);
  case llvm::Intrinsic::umul_with_overflow:
    return compile_overflow_intrin(inst, OverflowOp::umul);
  case llvm::Intrinsic::smul_with_overflow:
    return compile_overflow_intrin(inst, OverflowOp::smul);
  case llvm::Intrinsic::uadd_sat:
    return compile_saturating_intrin(inst, OverflowOp::uadd);
  case llvm::Intrinsic::sadd_sat:
    return compile_saturating_intrin(inst, OverflowOp::sadd);
  case llvm::Intrinsic::usub_sat:
    return compile_saturating_intrin(inst, OverflowOp::usub);
  case llvm::Intrinsic::ssub_sat:
    return compile_saturating_intrin(inst, OverflowOp::ssub);
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

    auto lhs = this->val_ref(inst->getOperand(0), 0);
    auto rhs = this->val_ref(inst->getOperand(1), 0);
    auto amt = this->val_ref(inst->getOperand(2), 0);
    ScratchReg res{derived()};

    // TODO: generate better code for constant amounts.
    bool shift_left = intrin_id == llvm::Intrinsic::fshl;
    if (inst->getOperand(0) == inst->getOperand(1)) {
      // Better code for rotate.
      using EncodeFnTy = bool (Derived::*)(
          GenericValuePart &&, GenericValuePart &&, ScratchReg &);
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
      using EncodeFnTy = bool (Derived::*)(GenericValuePart &&,
                                           GenericValuePart &&,
                                           GenericValuePart &&,
                                           ScratchReg &);
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

    auto res_ref = this->result_ref_lazy(inst, 0);
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

    using EncodeFnTy = bool (Derived::*)(GenericValuePart &&, ScratchReg &);
    static constexpr std::array<EncodeFnTy, 4> encode_fns = {
        &Derived::encode_bswapi16,
        &Derived::encode_bswapi32,
        &Derived::encode_bswapi48,
        &Derived::encode_bswapi64,
    };
    EncodeFnTy encode_fn = encode_fns[width / 16 - 1];

    auto val_ref = this->val_ref(val, 0);
    ScratchReg res{derived()};
    if (!(derived()->*encode_fn)(std::move(val_ref), res)) {
      return false;
    }

    auto res_ref = this->result_ref_lazy(inst, 0);
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

    GenericValuePart op = this->val_ref(val, 0);
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

    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
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

    auto val_ref = this->val_ref(val, 0);
    auto res_ref = this->result_ref_lazy(inst, 0);
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
      default: TPDE_UNREACHABLE("invalid size");
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
      default: TPDE_UNREACHABLE("invalid size");
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

    GenericValuePart op = this->val_ref(val, 0);
    if (width % 32) {
      u64 amt = (width < 32 ? 32 : 64) - width;
      ValuePartRef amt_val{this, &amt, 8, Config::GP_BANK};
      ScratchReg shifted{this};
      if (width < 32) {
        derived()->encode_shli32(std::move(op), std::move(amt_val), shifted);
      } else {
        derived()->encode_shli64(std::move(op), std::move(amt_val), shifted);
      }
      op = std::move(shifted);
    }

    ScratchReg res{derived()};
    if (width <= 32) {
      derived()->encode_bitreversei32(std::move(op), res);
    } else {
      derived()->encode_bitreversei64(std::move(op), res);
    }

    ValuePartRef res_ref = this->result_ref_lazy(inst, 0);
    this->set_value(res_ref, res);
    return true;
  }
  case llvm::Intrinsic::prefetch: {
    auto ptr_ref = this->val_ref(inst->getOperand(0), 0);

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
      case 3: derived()->encode_prefetch_rl3(std::move(ptr_ref)); break;
      default: TPDE_UNREACHABLE("invalid prefetch locality");
      }
    } else {
      assert(rw == 1);
      // write
      switch (locality) {
      case 0: derived()->encode_prefetch_wl0(std::move(ptr_ref)); break;
      case 1: derived()->encode_prefetch_wl1(std::move(ptr_ref)); break;
      case 2: derived()->encode_prefetch_wl2(std::move(ptr_ref)); break;
      case 3: derived()->encode_prefetch_wl3(std::move(ptr_ref)); break;
      default: TPDE_UNREACHABLE("invalid prefetch locality");
      }
    }
    return true;
  }
  case llvm::Intrinsic::eh_typeid_for: {
    auto *type = inst->getOperand(0);
    assert(llvm::isa<llvm::GlobalValue>(type));

    // not the most efficient but it's OK
    const auto type_info_sym = lookup_type_info_sym(type);
    const u64 idx = this->assembler.except_type_idx_for_sym(type_info_sym);

    auto res_ref = this->result_ref_eager(inst, 0);
    auto const_ref = ValuePartRef{this, &idx, 4, Config::GP_BANK};
    derived()->materialize_constant(const_ref, res_ref.cur_reg());
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  case llvm::Intrinsic::is_constant: {
    // > On the other hand, if constant folding is not run, it will never
    // evaluate to true, even in simple cases. example in
    // 641.leela_s:UCTNode.cpp

    // ref-count the argument
    this->val_ref(inst->getOperand(0), 0);
    auto res_ref = this->result_ref_eager(inst, 0);
    u64 zero = 0;
    auto const_ref = ValuePartRef{this, &zero, 4, Config::GP_BANK};
    derived()->materialize_constant(const_ref, res_ref.cur_reg());
    this->set_value(res_ref, res_ref.cur_reg());
    return true;
  }
  default: {
    return derived()->handle_intrin(inst);
  }
  }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_is_fpclass(
    const llvm::IntrinsicInst *inst) noexcept {
  auto *op = inst->getOperand(0);
  auto *op_ty = op->getType();

  if (!op_ty->isFloatTy() && !op_ty->isDoubleTy()) {
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
  auto res_ref = this->result_ref_lazy(inst, 0);
  auto op_ref = this->val_ref(op, 0);

  u64 zero = 0;
  auto zero_ref = ValuePartRef{this, &zero, 4, Config::GP_BANK};

  // handle common case
#define TEST(cond, name)                                                       \
  if (test == cond) {                                                          \
    if (is_double) {                                                           \
      derived()->encode_is_fpclass_##name##_double(                            \
          zero_ref, std::move(op_ref), res_scratch);                           \
    } else {                                                                   \
      derived()->encode_is_fpclass_##name##_float(                             \
          zero_ref, std::move(op_ref), res_scratch);                           \
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
  derived()->materialize_constant(zero_ref, res_scratch.alloc_gp());

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
    const llvm::IntrinsicInst *inst, OverflowOp op) noexcept {
  auto *llvm_lhs = inst->getOperand(0);
  auto *llvm_rhs = inst->getOperand(1);

  auto *ty = llvm_lhs->getType();
  assert(ty->isIntegerTy());
  const auto width = ty->getIntegerBitWidth();

  if (width == 128) {
    auto lhs_ref = this->val_ref(llvm_lhs, 0);
    auto rhs_ref = this->val_ref(llvm_rhs, 0);
    lhs_ref.inc_ref_count();
    rhs_ref.inc_ref_count();
    GenericValuePart lhs_op_high = this->val_ref(llvm_lhs, 1);
    GenericValuePart rhs_op_high = this->val_ref(llvm_rhs, 1);
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

    auto res_ref_val = this->result_ref_lazy(inst, 0);
    auto res_ref_high = this->result_ref_lazy(inst, 1);
    auto res_ref_of = this->result_ref_lazy(inst, 2);
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
  default: return false;
  }

  using EncodeFnTy = bool (Derived::*)(
      GenericValuePart &&, GenericValuePart &&, ScratchReg &, ScratchReg &);
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

  GenericValuePart lhs_op = this->val_ref(llvm_lhs, 0);
  GenericValuePart rhs_op = this->val_ref(llvm_rhs, 0);
  ScratchReg res_val{derived()}, res_of{derived()};

  if (!(derived()->*encode_fn)(
          std::move(lhs_op), std::move(rhs_op), res_val, res_of)) {
    return false;
  }

  auto res_ref_val = this->result_ref_lazy(inst, 0);
  auto res_ref_of = this->result_ref_lazy(inst, 1);

  this->set_value(res_ref_val, res_val);
  this->set_value(res_ref_of, res_of);
  res_ref_of.reset_without_refcount();
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_saturating_intrin(
    const llvm::IntrinsicInst *inst, OverflowOp op) noexcept {
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
      bool (Derived::*)(GenericValuePart &&, GenericValuePart &&, ScratchReg &);
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

  GenericValuePart lhs_op = this->val_ref(inst->getOperand(0), 0);
  GenericValuePart rhs_op = this->val_ref(inst->getOperand(1), 0);
  ScratchReg res{derived()};
  if (!(derived()->*encode_fn)(std::move(lhs_op), std::move(rhs_op), res)) {
    return false;
  }

  auto res_ref_val = this->result_ref_lazy(inst, 0);
  this->set_value(res_ref_val, res);
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_to_elf(
    llvm::Module &mod, std::vector<uint8_t> &buf) noexcept {
  if (this->adaptor->mod) {
    derived()->reset();
  }
  if (!compile(mod)) {
    return false;
  }

  llvm::TimeTraceScope time_scope("TPDE_EmitObj");
  buf = this->assembler.build_object_file();
  return true;
}

template <typename Adaptor, typename Derived, typename Config>
JITMapper LLVMCompilerBase<Adaptor, Derived, Config>::compile_and_map(
    llvm::Module &mod,
    std::function<void *(std::string_view)> resolver) noexcept {
  if (this->adaptor->mod) {
    derived()->reset();
  }
  if (!compile(mod)) {
    return JITMapper{nullptr};
  }

  auto res = std::make_unique<JITMapperImpl>(std::move(global_syms));
  if (!res->map(this->assembler, resolver)) {
    return JITMapper{nullptr};
  }

  return JITMapper{std::move(res)};
}

} // namespace tpde_llvm
