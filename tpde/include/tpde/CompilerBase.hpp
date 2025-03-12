// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <unordered_map>

#include "Analyzer.hpp"
#include "Compiler.hpp"
#include "CompilerConfig.hpp"
#include "IRAdaptor.hpp"
#include "tpde/RegisterFile.hpp"
#include "tpde/base.hpp"
#include "tpde/util/AddressSanitizer.hpp"
#include "tpde/util/BumpAllocator.hpp"
#include "tpde/util/misc.hpp"

namespace tpde {
// TODO(ts): formulate concept for full compiler so that there is *some* check
// whether all the required derived methods are implemented?

/// Thread-local storage access mode
enum class TLSModel {
  GlobalDynamic,
  LocalDynamic,
  InitialExec,
  LocalExec,
};

/// The base class for the compiler.
/// It implements the main platform independent compilation logic and houses the
/// analyzer
template <IRAdaptor Adaptor,
          typename Derived,
          CompilerConfig Config = CompilerConfigDefault>
struct CompilerBase {
  // some forwards for the IR type defs
  using IRValueRef = typename Adaptor::IRValueRef;
  using IRInstRef = typename Adaptor::IRInstRef;
  using IRBlockRef = typename Adaptor::IRBlockRef;
  using IRFuncRef = typename Adaptor::IRFuncRef;

  using BlockIndex = typename Analyzer<Adaptor>::BlockIndex;

  using Assembler = typename Config::Assembler;
  using AsmReg = typename Config::AsmReg;

  using RegisterFile = RegisterFile<Config::NUM_BANKS, 32>;

  /// A default implementation for ValRefSpecial.
  // Note: Subclasses can override this, always used Derived::ValRefSpecial.
  struct ValRefSpecial {
    uint8_t mode = 4;
    u64 const_data;
  };

#pragma region CompilerData
  Adaptor *adaptor;
  Analyzer<Adaptor> analyzer;

  // data for frame management

  struct {
    /// The current size of the stack frame
    u32 frame_size = 0;
    /// Free-Lists for 1/2/4/8/16 sized allocations
    // TODO(ts): make the allocations for 4/8 different from the others
    // since they are probably the one's most used?
    util::SmallVector<u32, 16> fixed_free_lists[5] = {};
    /// Free-Lists for all other sizes
    // TODO(ts): think about which data structure we want here
    std::unordered_map<u32, std::vector<u32>> dynamic_free_lists{};
  } stack = {};

  typename Analyzer<Adaptor>::BlockIndex cur_block_idx;

  // Assignments

  static constexpr ValLocalIdx INVALID_VAL_LOCAL_IDX =
      static_cast<ValLocalIdx>(~0u);

// disable Wpedantic here since these are compiler extensions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

// GCC < 15 does not support the flexible array member in the union
// see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53548
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ < 15
  #define GNU_ZERO 0
#else
  #define GNU_ZERO
#endif

  // TODO(ts): add option to use fixed size assignments if there is no need
  // for arbitrarily many parts since that comes with a 1-3% cost in
  // compile-time
  struct ValueAssignment {
    union {
      u32 frame_off;
      /// For variable-references, frame_off is unused so it can be used
      /// to store an index into a custom structure in case there is
      /// special handling for variable references
      u32 var_ref_custom_idx;
    };

    u32 size;

    // we want tight packing and with this construct we get a base size of
    // 16 bytes for values with only one part (the majority)
    union {
      ValueAssignment *next_free_list_entry;

      struct {
        union {
          ValLocalIdx next_delayed_free_entry;
          u32 references_left;
        };
        u8 max_part_size;

        /// Whether the assignment is in a delayed free list and the reference
        /// count is therefore invalid (zero). Set/used in debug builds only to
        /// catch use-after-frees.
        bool pending_free : 1;

        /// Whether the assignment is a single-part variable reference.
        bool variable_ref : 1;

        /// Whether to delay the free when the reference count reaches zero.
        /// (This is liveness.last_full, copied here for faster access).
        bool delay_free : 1;

        // TODO: get the type of parts from Derived
        // note: the top bit of each part is reserved to indicate
        // whether there is another part after this
        union {
          u16 first_part;
          u16 parts[GNU_ZERO];
        };
      };
    };

    void initialize(u32 frame_off,
                    u32 size,
                    u32 ref_count,
                    u16 max_part_size) noexcept;
  };

#undef GNU_ZERO
#pragma GCC diagnostic pop

  static constexpr size_t ASSIGNMENT_BUF_SIZE = 16 * 1024;

  // TODO(ts): think about different ways to store this that are maybe more
  // compact?
  struct AssignmentStorage {
    // Free list 0 holds VASize = sizeof(ValueAssignment).
    // Free list 1 holds VASize rounded to the next larger power of two.
    // Free list 2 holds twice as much as free list 1. Etc.
    static constexpr u32 NumFreeLists = 2;
    static constexpr u32 PartSize = sizeof(ValueAssignment::first_part);
    static constexpr u32 FirstPartOff = offsetof(ValueAssignment, parts);
    static constexpr u32 NumPartsIncluded =
        (sizeof(ValueAssignment) - FirstPartOff) / PartSize;

    /// Allocator for small assignments that get stored in free lists. Larger
    /// assignments are allocated directly on the heap.
    util::BumpAllocator<ASSIGNMENT_BUF_SIZE> alloc;
    /// Free lists for small ValueAssignments
    std::array<ValueAssignment *, NumFreeLists> fixed_free_lists{};

    std::array<u32, Config::NUM_BANKS> cur_fixed_assignment_count = {};
    util::SmallVector<ValueAssignment *, Analyzer<Adaptor>::SMALL_VALUE_NUM>
        value_ptrs;

    util::SmallVector<ValLocalIdx, Analyzer<Adaptor>::SMALL_BLOCK_NUM>
        delayed_free_lists;
  } assignments = {};

  RegisterFile register_file;

  Assembler assembler;
  // TODO(ts): smallvector?
  std::vector<typename Assembler::SymRef> func_syms;
  // TODO(ts): combine this with the block vectors in the analyzer to save on
  // allocations
  util::SmallVector<typename Assembler::Label> block_labels;


  struct ScratchReg;
  struct AssignmentPartRef;
  struct ValuePart;
  struct ValuePartRef;
  struct ValueRef;
  struct GenericValuePart;
#pragma endregion

  struct InstRange {
    using Range = decltype(std::declval<Adaptor>().block_insts(
        std::declval<IRBlockRef>()));
    using Iter = decltype(std::declval<Range>().begin());
    using EndIter = decltype(std::declval<Range>().end());
    Iter from;
    EndIter to;
  };

  /// Initialize a CompilerBase, should be called by the derived classes
  explicit CompilerBase(Adaptor *adaptor, const bool generate_object)
      : adaptor(adaptor), analyzer(adaptor), assembler(generate_object) {
    static_assert(std::is_base_of_v<CompilerBase, Derived>);
    static_assert(Compiler<Derived, Config>);
  }

  /// shortcut for casting to the Derived class so that overloading
  /// works
  Derived *derived() { return static_cast<Derived *>(this); }

  const Derived *derived() const { return static_cast<const Derived *>(this); }

  [[nodiscard]] ValLocalIdx val_idx(const IRValueRef value) const noexcept {
    return static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));
  }

  [[nodiscard]] ValueAssignment *
      val_assignment(const ValLocalIdx idx) noexcept {
    return assignments.value_ptrs[static_cast<u32>(idx)];
  }

  /// Compile the functions returned by Adaptor::funcs
  ///
  /// \warning If you intend to call this multiple times, you must call reset
  ///   inbetween the calls.
  ///
  /// \returns Whether the compilation was successful
  bool compile();

  /// Reset any leftover data from the previous compilation such that it will
  /// not affect the next compilation
  void reset();

protected:
  struct AssignmentAllocInfo {
    u32 size;
    u32 alloc_size;
    u32 free_list_idx;

    AssignmentAllocInfo(u32 part_count) noexcept;
  };

  ValueAssignment *allocate_assignment(u32 part_count) noexcept {
    if (part_count <= AssignmentStorage::NumPartsIncluded) [[likely]] {
      if (auto *res = assignments.fixed_free_lists[0]) {
        util::unpoison_memory_region(res, sizeof(ValueAssignment));
        assignments.fixed_free_lists[0] = res->next_free_list_entry;
        return res;
      }
      return new (assignments.alloc) ValueAssignment;
    }
    return allocate_assignment_slow(part_count);
  }

  ValueAssignment *
      allocate_assignment_slow(u32 part_count,
                               bool skip_free_list = false) noexcept;
  /// Puts an assignment back into the free list
  void deallocate_assignment(u32 part_count, ValLocalIdx local_idx) noexcept;

  void init_assignment(IRValueRef value, ValLocalIdx local_idx) noexcept;

  /// Frees an assignment, its stack slot and registers
  void free_assignment(ValLocalIdx local_idx) noexcept;

  u32 allocate_stack_slot(u32 size) noexcept;
  void free_stack_slot(u32 slot, u32 size) noexcept;

public:
  ValueRef val_ref(IRValueRef value) noexcept;

  std::pair<ValueRef, ValuePartRef> val_ref_single(IRValueRef value) noexcept;

  /// Get a defining reference to a value
  ValueRef result_ref(IRValueRef value) noexcept;

  std::pair<ValueRef, ValuePartRef>
      result_ref_single(IRValueRef value) noexcept;

  void set_value(ValuePartRef &val_ref, ScratchReg &scratch) noexcept;

  /// Get generic value part into a single register, evaluating expressions
  /// and materializing immediates as required.
  AsmReg gval_as_reg(GenericValuePart &gv) noexcept;

  /// Like gval_as_reg; if the GenericValuePart owns a reusable register
  /// (either a ScratchReg, possibly due to materialization, or a reusable
  /// ValuePartRef), store it in dst.
  AsmReg gval_as_reg_reuse(GenericValuePart &gv, ScratchReg &dst) noexcept;

  // TODO(ts): switch to a branch_spill_before naming style?
  typename RegisterFile::RegBitSet spill_before_branch() noexcept;
  void release_spilled_regs(typename RegisterFile::RegBitSet) noexcept;

  /// When reaching a point in the function where no other blocks will be
  /// reached anymore, use this function to release register assignments after
  /// the end of that block so the compiler does not accidentally use
  /// registers which don't contain any values
  void release_regs_after_return() noexcept;

  void move_to_phi_nodes(BlockIndex target) noexcept {
    if (analyzer.block_has_phis(target)) {
      move_to_phi_nodes_impl(target);
    }
  }

  void move_to_phi_nodes_impl(BlockIndex target) noexcept;

  bool branch_needs_split(IRBlockRef target) noexcept {
    // for now, if the target has PHI-nodes, we split
    return analyzer.block_has_phis(target);
  }

  BlockIndex next_block() const noexcept;

  bool try_force_fixed_assignment(IRValueRef) const noexcept { return false; }

  bool hook_post_func_sym_init() noexcept { return true; }

  void analysis_start() noexcept {}

  void analysis_end() noexcept {}

protected:
  bool compile_func(IRFuncRef func, u32 func_idx) noexcept;

  bool compile_block(IRBlockRef block, u32 block_idx) noexcept;
};
} // namespace tpde

#include "AssignmentPartRef.hpp"
#include "GenericValuePart.hpp"
#include "ScratchReg.hpp"
#include "ValuePartRef.hpp"
#include "ValueRef.hpp"

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValueAssignment::initialize(
    u32 frame_off, u32 size, u32 ref_count, u16 max_part_size) noexcept {
  this->frame_off = frame_off;
  this->size = size;
  this->references_left = ref_count;
  this->max_part_size = max_part_size;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::compile() {
  // create function symbols

  assert(func_syms.empty());
  for (const IRFuncRef func : adaptor->funcs()) {
    auto binding = Assembler::SymBinding::GLOBAL;
    if (adaptor->func_has_weak_linkage(func)) {
      binding = Assembler::SymBinding::WEAK;
    } else if (adaptor->func_only_local(func)) {
      binding = Assembler::SymBinding::LOCAL;
    }
    if (adaptor->func_extern(func)) {
      func_syms.push_back(derived()->assembler.sym_add_undef(
          adaptor->func_link_name(func), binding));
    } else {
      func_syms.push_back(derived()->assembler.sym_predef_func(
          adaptor->func_link_name(func), binding));
    }
    derived()->define_func_idx(func, func_syms.size() - 1);
  }

  if (!derived()->hook_post_func_sym_init()) {
    TPDE_LOG_ERR("hook_pust_func_sym_init failed");
    return false;
  }

  // TODO(ts): create function labels?

  u32 func_idx = 0;
  for (const IRFuncRef func : adaptor->funcs()) {
    if (adaptor->func_extern(func)) {
      TPDE_LOG_TRACE("Skipping compilation of func {}",
                     adaptor->func_link_name(func));
      ++func_idx;
      continue;
    }

    TPDE_LOG_TRACE("Compiling func {}", adaptor->func_link_name(func));
    if (!derived()->compile_func(func, func_idx)) {
      TPDE_LOG_ERR("Failed to compile function {}",
                   adaptor->func_link_name(func));
      return false;
    }
    ++func_idx;
  }

  // TODO(ts): generate object/map?

  return true;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::reset() {
  analyzer.reset();
  adaptor->reset();

  for (auto &e : stack.fixed_free_lists) {
    e.clear();
  }
  stack.dynamic_free_lists.clear();

  assignments.value_ptrs.clear();
  assignments.cur_fixed_assignment_count = {};
  assignments.fixed_free_lists = {};
  assignments.delayed_free_lists.clear();

  assembler.reset();
  func_syms.clear();
  block_labels.clear();
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::AssignmentAllocInfo::
    AssignmentAllocInfo(const u32 part_count) noexcept {
  constexpr u32 VASize = sizeof(ValueAssignment);
  constexpr u32 PartSize = AssignmentStorage::PartSize;

  size = VASize;
  alloc_size = VASize;
  free_list_idx = 0;
  if (part_count > AssignmentStorage::NumPartsIncluded) {
    size += (part_count - AssignmentStorage::NumPartsIncluded) * PartSize;
    // Round size to next power of two.
    static_assert((VASize & (VASize - 1)) == 0,
                  "non-power-of-two ValueAssignment size untested");
    constexpr u32 clz_off = util::cnt_lz<u32>(VASize >> 1);
    free_list_idx = clz_off - util::cnt_lz<u32>(size - 1);
    alloc_size = u32{1} << (32 - clz_off + free_list_idx);
  }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValueAssignment *
    CompilerBase<Adaptor, Derived, Config>::allocate_assignment_slow(
        const u32 part_count, bool skip_free_list) noexcept {
  AssignmentAllocInfo aai(part_count);

  if (aai.free_list_idx >= assignments.fixed_free_lists.size()) [[unlikely]] {
    auto *alloc = new std::byte[aai.size];
    return new (reinterpret_cast<ValueAssignment *>(alloc)) ValueAssignment{};
  }

  if (!skip_free_list) {
    auto &free_list = assignments.fixed_free_lists[aai.free_list_idx];
    if (auto *assignment = free_list) {
      util::unpoison_memory_region(assignment, aai.size);
      free_list = assignment->next_free_list_entry;
      return assignment;
    }
  }

  assert(aai.alloc_size < ASSIGNMENT_BUF_SIZE);
  auto *buf =
      assignments.alloc.allocate(aai.alloc_size, alignof(ValueAssignment));
  return new (reinterpret_cast<ValueAssignment *>(buf)) ValueAssignment{};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::deallocate_assignment(
    u32 part_count, ValLocalIdx local_idx) noexcept {
  AssignmentAllocInfo aai(part_count);

  ValueAssignment *assignment = val_assignment(local_idx);
  assignments.value_ptrs[static_cast<u32>(local_idx)] = nullptr;

  if (aai.free_list_idx < assignments.fixed_free_lists.size()) [[likely]] {
    assignment->next_free_list_entry =
        assignments.fixed_free_lists[aai.free_list_idx];
    assignments.fixed_free_lists[aai.free_list_idx] = assignment;
    util::poison_memory_region(assignment, aai.size);
  } else {
    assignment->~ValueAssignment();
    delete[] reinterpret_cast<std::byte *>(assignment);
  }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::init_assignment(
    IRValueRef value, ValLocalIdx local_idx) noexcept {
  assert(val_assignment(local_idx) == nullptr);
  TPDE_LOG_TRACE("Initializing assignment for value {}",
                 static_cast<u32>(local_idx));

  const auto parts = derived()->val_parts(value);
  const u32 part_count = parts.count();
  assert(part_count > 0);
  auto *assignment = allocate_assignment(part_count);
  assignments.value_ptrs[static_cast<u32>(local_idx)] = assignment;

  u32 max_part_size = 0;
  for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
    auto ap = AssignmentPartRef{assignment, part_idx};
    ap.reset();
    ap.set_bank(parts.reg_bank(part_idx));
    const u32 size = parts.size_bytes(part_idx);
    assert(size > 0);
    max_part_size = std::max(max_part_size, size);
    ap.set_part_size(size);

    if (part_idx != part_count - 1) {
      ap.set_has_next_part(true);
    }
  }

  const auto &liveness = analyzer.liveness_info(static_cast<u32>(local_idx));

  // if there is only one part, try to hand out a fixed assignment
  // if the value is used for longer than one block and there aren't too many
  // definitions in child loops this could interfere with
  // TODO(ts): try out only fixed assignments if the value is live for more
  // than two blocks?
  // TODO(ts): move this to ValuePartRef::alloc_reg to be able to defer this
  // for results?
  if (part_count == 1) {
    const auto &cur_loop =
        analyzer.loop_from_idx(analyzer.block_loop_idx(cur_block_idx));
    auto ap = AssignmentPartRef{assignment, 0};

    auto try_fixed =
        liveness.last > cur_block_idx &&
        cur_loop.definitions_in_childs +
                assignments.cur_fixed_assignment_count[ap.bank().id()] <
            Derived::NUM_FIXED_ASSIGNMENTS[ap.bank().id()];
    if (derived()->try_force_fixed_assignment(value)) {
      try_fixed = assignments.cur_fixed_assignment_count[ap.bank().id()] <
                  Derived::NUM_FIXED_ASSIGNMENTS[ap.bank().id()];
    }

    if (try_fixed) {
      // check if there is a fixed register available
      AsmReg reg = derived()->select_fixed_assignment_reg(ap.bank(), value);
      TPDE_LOG_TRACE("Trying to assign fixed reg to value {}",
                     static_cast<u32>(local_idx));

      if (!reg.invalid()) {
        if (register_file.is_used(reg)) {
          assert(!register_file.is_fixed(reg));
          auto *spill_assignment =
              val_assignment(register_file.reg_local_idx(reg));
          auto spill_ap =
              AssignmentPartRef{spill_assignment, register_file.reg_part(reg)};
          assert(!spill_ap.fixed_assignment());
          assert(spill_ap.variable_ref() || !spill_ap.modified());
          spill_ap.set_register_valid(false);
          register_file.unmark_used(reg);
        }

        TPDE_LOG_TRACE("Assigning fixed assignment to reg {} for value {}",
                       reg.id(),
                       static_cast<u32>(local_idx));
        ap.set_full_reg_id(reg.id());
        ap.set_register_valid(true);
        ap.set_fixed_assignment(true);
        register_file.mark_used(reg, local_idx, 0);
        register_file.inc_lock_count(reg); // fixed assignments always locked
        register_file.mark_clobbered(reg);
        ++assignments.cur_fixed_assignment_count[ap.bank().id()];
      }
    }
  }

  const auto size = max_part_size * part_count;
  const auto frame_off = allocate_stack_slot(size);
  const auto last_full = liveness.last_full;
  const auto ref_count = liveness.ref_count;

  assert(max_part_size <= 256);
  assignment->max_part_size = max_part_size;
#ifndef NDEBUG
  assignment->pending_free = false;
#endif
  assignment->variable_ref = false;
  assignment->delay_free = last_full;
  assignment->size = size;
  assignment->frame_off = frame_off;
  assignment->references_left = ref_count;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::free_assignment(
    const ValLocalIdx local_idx) noexcept {
  TPDE_LOG_TRACE("Freeing assignment for value {}",
                 static_cast<u32>(local_idx));

  ValueAssignment *assignment =
      assignments.value_ptrs[static_cast<u32>(local_idx)];
  const auto is_var_ref = assignment->variable_ref;

  // free registers
  u32 part_idx = 0;
  bool has_next_part = true;
  while (has_next_part) {
    auto ap = AssignmentPartRef{assignment, part_idx};
    has_next_part = ap.has_next_part();
    if (ap.fixed_assignment()) {
      const auto reg = AsmReg{ap.full_reg_id()};
      assert(register_file.is_fixed(reg));
      assert(register_file.reg_local_idx(reg) == local_idx);
      assert(register_file.reg_part(reg) == part_idx);
      --assignments.cur_fixed_assignment_count[ap.bank().id()];
      register_file.dec_lock_count_must_zero(reg); // release lock for fixed reg
      register_file.unmark_used(reg);
    } else if (ap.register_valid()) {
      const auto reg = AsmReg{ap.full_reg_id()};
      assert(!register_file.is_fixed(reg));
      register_file.unmark_used(reg);
    }
    ++part_idx;
  }

#ifdef TPDE_ASSERTS
  for (auto reg_id : register_file.used_regs()) {
    assert(register_file.reg_local_idx(AsmReg{reg_id}) != local_idx &&
           "freeing assignment that is still referenced by a register");
  }
#endif

  // variable references do not have a stack slot
  if (!is_var_ref) {
    auto slot = assignment->frame_off;
    free_stack_slot(slot, assignment->size);
  }

  deallocate_assignment(part_idx, local_idx);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
u32 CompilerBase<Adaptor, Derived, Config>::allocate_stack_slot(
    u32 size) noexcept {
  unsigned align_bits = 4;
  if (size == 0) {
    return 1; // 0 is used for variable references...
  } else if (size <= 16) {
    // Align up to next power of two.
    u32 free_list_idx = size == 1 ? 0 : 32 - util::cnt_lz<u32>(size - 1);
    assert(size <= 1 << free_list_idx);
    size = 1 << free_list_idx;
    align_bits = free_list_idx;

    if (!stack.fixed_free_lists[free_list_idx].empty()) {
      auto slot = stack.fixed_free_lists[free_list_idx].back();
      stack.fixed_free_lists[free_list_idx].pop_back();
      return slot;
    }
  } else {
    size = util::align_up(size, 16);
    auto it = stack.dynamic_free_lists.find(size);
    if (it != stack.dynamic_free_lists.end() && !it->second.empty()) {
      const auto slot = it->second.back();
      it->second.pop_back();
      return slot;
    }
  }

  // Align frame_size to align_bits
  for (u32 list_idx = util::cnt_tz(stack.frame_size); list_idx < align_bits;
       list_idx = util::cnt_tz(stack.frame_size)) {
    u32 slot = stack.frame_size;
    if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
      slot += 1ull << list_idx;
    }
    stack.fixed_free_lists[list_idx].push_back(slot);
    stack.frame_size += 1ull << list_idx;
  }

  auto slot = stack.frame_size;
  stack.frame_size += size;

  if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
    slot += size;
  }
  return slot;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::free_stack_slot(
    u32 slot, u32 size) noexcept {
  if (size == 0) [[unlikely]] {
    assert(slot == 1 && "unexpected slot for zero-sized stack-slot?");
    // Do nothing.
  } else if (size <= 16) [[likely]] {
    u32 free_list_idx = size == 1 ? 0 : 32 - util::cnt_lz<u32>(size - 1);
    stack.fixed_free_lists[free_list_idx].push_back(slot);
  } else {
    size = util::align_up(size, 16);
    stack.dynamic_free_lists[size].push_back(slot);
  }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValueRef
    CompilerBase<Adaptor, Derived, Config>::val_ref(IRValueRef value) noexcept {
  if (auto special = derived()->val_ref_special(value); special) {
    return ValueRef{this, std::move(*special)};
  }

  const auto local_idx =
      static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

  assert(val_assignment(local_idx) != nullptr && "value use before def");
  return ValueRef{this, local_idx};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
std::pair<typename CompilerBase<Adaptor, Derived, Config>::ValueRef,
          typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef>
    CompilerBase<Adaptor, Derived, Config>::val_ref_single(
        IRValueRef value) noexcept {
  std::pair<ValueRef, ValuePartRef> res{val_ref(value), this};
  res.second = res.first.part(0);
  return res;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValueRef
    CompilerBase<Adaptor, Derived, Config>::result_ref(
        IRValueRef value) noexcept {
  const auto local_idx =
      static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

  if (val_assignment(local_idx) == nullptr) {
    init_assignment(value, local_idx);
  }
  return ValueRef{this, local_idx};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
std::pair<typename CompilerBase<Adaptor, Derived, Config>::ValueRef,
          typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef>
    CompilerBase<Adaptor, Derived, Config>::result_ref_single(
        IRValueRef value) noexcept {
  std::pair<ValueRef, ValuePartRef> res{result_ref(value), this};
  res.second = res.first.part(0);
  return res;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::set_value(
    ValuePartRef &val_ref, ScratchReg &scratch) noexcept {
  auto ap = val_ref.assignment();
  assert(scratch.has_reg());
  auto reg = scratch.cur_reg();

  if (ap.fixed_assignment()) {
    auto cur_reg = AsmReg{ap.full_reg_id()};
    assert(register_file.is_used(cur_reg));
    assert(register_file.is_fixed(cur_reg));
    assert(register_file.reg_local_idx(cur_reg) == val_ref.local_idx());

    if (cur_reg.id() != reg.id()) {
      derived()->mov(cur_reg, reg, ap.part_size());
    }

    ap.set_register_valid(true);
    ap.set_modified(true);
    return;
  }

  if (ap.register_valid()) {
    auto cur_reg = AsmReg{ap.full_reg_id()};
    if (cur_reg.id() == reg.id()) {
      ap.set_modified(true);
      return;
    }
    val_ref.unlock();
    assert(!register_file.is_fixed(cur_reg));
    register_file.unmark_used(cur_reg);
  }

  // ScratchReg's reg is fixed and used => unfix, keep used, update assignment
  assert(register_file.is_used(reg));
  assert(register_file.is_fixed(reg));
  assert(register_file.is_clobbered(reg));
  scratch.force_set_reg(AsmReg::make_invalid());
  register_file.unmark_fixed(reg);
  register_file.update_reg_assignment(reg, val_ref.local_idx(), val_ref.part());
  ap.set_full_reg_id(reg.id());
  ap.set_register_valid(true);
  ap.set_modified(true);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::gval_as_reg(
        GenericValuePart &gv) noexcept {
  if (std::holds_alternative<ScratchReg>(gv.state)) {
    return std::get<ScratchReg>(gv.state).cur_reg();
  }
  if (std::holds_alternative<ValuePartRef>(gv.state)) {
    return std::get<ValuePartRef>(gv.state).load_to_reg();
  }
  if (auto *expr = std::get_if<typename GenericValuePart::Expr>(&gv.state)) {
    if (expr->has_base() && !expr->has_index() && expr->disp == 0) {
      return expr->base_reg();
    }
    return derived()->gval_expr_as_reg(gv);
  }
  TPDE_UNREACHABLE("gval_as_reg on empty GenericValuePart");
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::gval_as_reg_reuse(
        GenericValuePart &gv, ScratchReg &dst) noexcept {
  AsmReg reg = gval_as_reg(gv);
  if (!dst.has_reg()) {
    if (auto *scratch = std::get_if<ScratchReg>(&gv.state)) {
      dst = std::move(*scratch);
    } else if (auto *val_ref = std::get_if<ValuePartRef>(&gv.state)) {
      if (val_ref->can_salvage()) {
        dst.alloc_specific(val_ref->salvage());
        assert(dst.cur_reg() == reg && "salvaging unsuccessful");
      }
    }
  }
  return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::RegisterFile::RegBitSet
    CompilerBase<Adaptor, Derived, Config>::spill_before_branch() noexcept {
  // since we do not explicitly keep track of register assignments per block,
  // whenever we might branch off to a block that we do not directly compile
  // afterwards (i.e. the register assignments might change in between), we
  // need to spill all registers which are not fixed and remove them from the
  // register state.
  //
  // This leads to worse codegen but saves a significant overhead to
  // store/manage the register assignment for each block (256 bytes/block for
  // x64) and possible compiletime as there might be additional logic to move
  // values around

  // First, we consider the case that the current block only has one successor
  // which is compiled directly after the current one, in which case we do not
  // have to spill anything.
  //
  // Secondly, if the next block has multiple incoming edges, we always have
  // to spill and remove from the register assignment. Otherwise, we
  // only need to spill values if they are alive in any successor which is not
  // the next block.
  //
  // Values which are only read from PHI-Nodes and have no extended lifetimes,
  // do not need to be spilled as they die at the edge.

  using RegBitSet = typename RegisterFile::RegBitSet;

  const IRBlockRef cur_block_ref = analyzer.block_ref(cur_block_idx);
  auto next_block_is_succ = false;
  auto next_block_has_multiple_incoming = false;
  u32 succ_count = 0;
  for (const IRBlockRef succ : adaptor->block_succs(cur_block_ref)) {
    ++succ_count;
    if (static_cast<u32>(analyzer.block_idx(succ)) ==
        static_cast<u32>(cur_block_idx) + 1) {
      next_block_is_succ = true;
      if (analyzer.block_has_multiple_incoming(succ)) {
        next_block_has_multiple_incoming = true;
      }
    }
  }

  if (succ_count == 1 && next_block_is_succ &&
      !next_block_has_multiple_incoming) {
    return RegBitSet{};
  }

  /*if (!next_block_is_succ) {
      // spill and remove from reg assignment
      auto spilled = RegisterFile::RegBitSet{};
      for (auto reg : register_file.used_regs()) {
          auto local_idx = register_file.reg_local_idx(AsmReg{reg});
          auto part      = register_file.reg_part(AsmReg{reg});
          if (local_idx == INVALID_VAL_LOCAL_IDX) {
              // TODO(ts): can this actually happen?
              continue;
          }
          auto ap = AssignmentPartRef{val_assignment(local_idx), part};
          ap.spill_if_needed(this);
          spilled |= RegisterFile::RegBitSet{1ull} << reg;
      }
      return spilled;
  }*/

  u16 phi_ref_count[RegisterFile::NumRegs] = {};
  for (const IRBlockRef succ : adaptor->block_succs(cur_block_ref)) {
    for (const IRValueRef phi_val : adaptor->block_phis(succ)) {
      const auto phi_ref = adaptor->val_as_phi(phi_val);
      const IRValueRef inc_val = phi_ref.incoming_val_for_block(cur_block_ref);
      if (derived()->val_ref_special(inc_val)) {
        continue;
      }
      ValLocalIdx local_idx =
          static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(inc_val));
      auto *assignment = this->val_assignment(local_idx);
      if (!assignment) {
        continue;
      }
      u32 part_count = derived()->val_parts(inc_val).count();
      for (u32 i = 0; i < part_count; ++i) {
        auto ap = AssignmentPartRef{assignment, i};
        if (ap.register_valid()) {
          ++phi_ref_count[ap.full_reg_id()];
        }
      }
    }
  }

  const auto spill_reg_if_needed = [&](const auto reg) {
    auto local_idx = register_file.reg_local_idx(AsmReg{reg});
    auto part = register_file.reg_part(AsmReg{reg});
    if (local_idx == INVALID_VAL_LOCAL_IDX) {
      // scratch regs can never be held across blocks
      return true;
    }
    auto *assignment = val_assignment(local_idx);
    auto ap = AssignmentPartRef{assignment, part};
    if (ap.fixed_assignment()) {
      // fixed registers do not need to be spilled
      return true;
    }

    if (!ap.modified()) {
      // no need to spill if the value was not modified
      return false;
    }

    if (ap.variable_ref()) {
      return false;
    }

    const auto &liveness = analyzer.liveness_info(static_cast<u32>(local_idx));
    if (assignment->references_left <= phi_ref_count[reg] &&
        liveness.last <= cur_block_idx) {
      return false;
    }

    if (!next_block_is_succ || next_block_has_multiple_incoming) {
      ap.spill_if_needed(this);
      return false;
    }

    // spill if the value is alive in any successor that is not the next
    // block
    for (const IRBlockRef succ : adaptor->block_succs(cur_block_ref)) {
      const auto block_idx = analyzer.block_idx(succ);
      if (static_cast<u32>(block_idx) == static_cast<u32>(cur_block_idx) + 1) {
        continue;
      }
      if (block_idx >= liveness.first && block_idx <= liveness.last) {
        ap.spill_if_needed(this);
        return false;
      }
    }

    return false;
  };

  auto spilled = RegBitSet{};
  // TODO(ts): just use register_file.used_nonfixed_regs()?
  for (auto reg : register_file.used_regs()) {
    const auto reg_fixed = spill_reg_if_needed(reg);
    // remove from register assignment if the next block cannot rely on the
    // value being in the specific register
    if (!reg_fixed &&
        (next_block_has_multiple_incoming || !next_block_is_succ)) {
      // TODO(ts): this needs to be changed if this is supposed to work
      // with other RegisterFile implementations
      spilled |= RegBitSet{1ull} << reg;
    }
  }

  return spilled;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::release_spilled_regs(
    typename RegisterFile::RegBitSet regs) noexcept {
  // TODO(ts): needs changes for other RegisterFile impls
  for (auto reg_id : util::BitSetIterator<>{regs}) {
    auto reg = AsmReg{reg_id};
    if (!register_file.is_used(reg)) {
      continue;
    }
    if (register_file.is_fixed(reg)) {
      // we don't need to release fixed assignments but they should be
      // real assignments from values
      assert(register_file.reg_local_idx(reg) != INVALID_VAL_LOCAL_IDX);
      continue;
    }

    auto local_idx = register_file.reg_local_idx(reg);
    auto part = register_file.reg_part(reg);
    assert(local_idx != INVALID_VAL_LOCAL_IDX);
    auto ap = AssignmentPartRef{val_assignment(local_idx), part};
    ap.set_register_valid(false);
    register_file.unmark_used(reg);
  }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::
    release_regs_after_return() noexcept {
  // we essentially have to free all non-fixed registers
  for (auto reg_id : register_file.used_nonfixed_regs()) {
    auto reg = AsmReg{reg_id};
    auto local_idx = register_file.reg_local_idx(reg);
    auto part = register_file.reg_part(reg);
    assert(local_idx != INVALID_VAL_LOCAL_IDX);
    assert(!register_file.is_fixed(reg));
    auto ap = AssignmentPartRef{val_assignment(local_idx), part};
    ap.set_register_valid(false);
    register_file.unmark_used(reg);
  }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::move_to_phi_nodes_impl(
    BlockIndex target) noexcept {
  // PHI-nodes are always moved to their stack-slot (unless they are fixed)
  //
  // However, we need to take care of PHI-dependencies (cycles and chains)
  // as to not overwrite values which might be needed.
  //
  // In most cases, we expect the number of PHIs to be small but we want to
  // stay reasonably efficient even with larger numbers of PHIs

  struct ScratchWrapper {
    CompilerBase *self;
    AsmReg cur_reg = AsmReg::make_invalid();
    bool backed_up = false;
    bool was_modified = false;
    u8 part;
    ValLocalIdx local_idx;

    ScratchWrapper(CompilerBase *self) : self{self} {}

    ~ScratchWrapper() { reset(); }

    void reset() {
      if (cur_reg.invalid()) {
        return;
      }

      self->register_file.unmark_fixed(cur_reg);
      self->register_file.unmark_used(cur_reg);

      if (backed_up) {
        // restore the register state
        // TODO(ts): do we actually need the reload?
        auto *assignment = self->val_assignment(local_idx);
        // check if the value was free'd, then we dont need to restore
        // it
        if (assignment) {
          auto ap = AssignmentPartRef{assignment, part};
          if (!ap.variable_ref()) {
            // TODO(ts): assert that this always happens?
            assert(ap.stack_valid());
            self->derived()->load_from_stack(
                cur_reg, ap.frame_off(), ap.part_size());
          }
          ap.set_full_reg_id(cur_reg.id());
          ap.set_register_valid(true);
          ap.set_modified(was_modified);
          self->register_file.mark_used(cur_reg, local_idx, part);
        }
        backed_up = false;
      }
      cur_reg = AsmReg::make_invalid();
    }

    AsmReg alloc_from_bank(RegBank bank) {
      if (cur_reg.valid() && self->register_file.reg_bank(cur_reg) == bank) {
        return cur_reg;
      }
      if (cur_reg.valid()) {
        reset();
      }

      // TODO(ts): try to first find a non callee-saved/clobbered
      // register...
      auto &reg_file = self->register_file;
      auto reg = reg_file.find_first_free_excluding(bank, 0);
      if (reg.invalid()) {
        // TODO(ts): use clock here?
        reg = reg_file.find_first_nonfixed_excluding(bank, 0);
        if (reg.invalid()) {
          TPDE_FATAL("ran out of registers for scratch registers");
        }

        backed_up = true;
        local_idx = reg_file.reg_local_idx(reg);
        part = reg_file.reg_part(reg);
        AssignmentPartRef ap{self->val_assignment(local_idx), part};
        was_modified = ap.modified();
        // TODO(ts): this does not spill for variable refs
        ap.spill_if_needed(self);
        ap.set_register_valid(false);
        reg_file.unmark_used(reg);
      }

      reg_file.mark_used(reg, INVALID_VAL_LOCAL_IDX, 0);
      reg_file.mark_clobbered(reg);
      reg_file.mark_fixed(reg);
      cur_reg = reg;
      return reg;
    }

    ScratchWrapper &operator=(const ScratchWrapper &) = delete;
    ScratchWrapper &operator=(ScratchWrapper &&) = delete;
  };

  IRBlockRef target_ref = analyzer.block_ref(target);
  IRBlockRef cur_ref = analyzer.block_ref(cur_block_idx);

  // collect all the nodes
  struct NodeEntry {
    IRValueRef val;
    u32 ref_count;
  };

  util::SmallVector<NodeEntry, 16> nodes;
  for (IRValueRef phi : adaptor->block_phis(target_ref)) {
    nodes.push_back(NodeEntry{phi, 0});
  }

  // We check that the block has phi nodes before getting here.
  assert(!nodes.empty() && "block marked has having phi nodes has none");

  ScratchWrapper scratch{this};
  const auto move_to_phi = [this, &scratch](IRValueRef phi,
                                            IRValueRef incoming_val) {
    // TODO(ts): if phi==incoming_val, we should be able to elide the move
    // even if the phi is in a fixed register, no?

    const auto parts = derived()->val_parts(incoming_val);
    u32 part_count = parts.count();
    assert(part_count > 0);
    auto phi_vr = derived()->result_ref(phi);
    auto val_vr = derived()->val_ref(incoming_val);
    for (u32 i = 0; i < part_count; ++i) {
      // TODO(ts): just have this outside the loop and change the part
      // index? :P
      AsmReg reg{};
      ValuePartRef val_vpr = val_vr.part(i);
      if (val_vpr.is_const()) {
        reg = scratch.alloc_from_bank(val_vpr.bank());
        val_vpr.reload_into_specific_fixed(reg);
      } else if (val_vpr.assignment().register_valid() ||
                 val_vpr.assignment().fixed_assignment()) {
        reg = AsmReg{val_vpr.assignment().full_reg_id()};
      } else {
        reg = val_vpr.reload_into_specific_fixed(
            this, scratch.alloc_from_bank(parts.reg_bank(i)));
      }

      AssignmentPartRef phi_ap{phi_vr.assignment(), i};
      if (phi_ap.fixed_assignment()) {
        derived()->mov(AsmReg{phi_ap.full_reg_id()}, reg, phi_ap.part_size());
      } else {
        derived()->spill_reg(reg, phi_ap.frame_off(), phi_ap.part_size());
        phi_ap.set_stack_valid();
      }

      if (phi_ap.register_valid() && !phi_ap.fixed_assignment()) {
        auto cur_reg = AsmReg{phi_ap.full_reg_id()};
        assert(!register_file.is_fixed(cur_reg));
        register_file.unmark_used(cur_reg);
        phi_ap.set_register_valid(false);
      }
    }
  };

  if (nodes.size() == 1) {
    move_to_phi(
        nodes[0].val,
        adaptor->val_as_phi(nodes[0].val).incoming_val_for_block(cur_ref));
    return;
  }

  // sort so we can binary search later
  std::sort(nodes.begin(), nodes.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.val < rhs.val;
  });

  // fill in the refcount
  auto all_zero_ref = true;
  for (auto &node : nodes) {
    auto phi_ref = adaptor->val_as_phi(node.val);
    auto incoming_val = phi_ref.incoming_val_for_block(cur_ref);

    // We don't need to do anything for self-referencing PHIs.
    if (incoming_val == node.val) {
      continue;
    }

    auto it = std::lower_bound(nodes.begin(),
                               nodes.end(),
                               NodeEntry{.val = incoming_val},
                               [](const NodeEntry &lhs, const NodeEntry &rhs) {
                                 return lhs.val < rhs.val;
                               });
    if (it == nodes.end() || it->val != incoming_val) {
      continue;
    }
    ++it->ref_count;
    all_zero_ref = false;
  }

  if (all_zero_ref) {
    // no cycles/chain that we need to take care of
    for (auto &node : nodes) {
      move_to_phi(
          node.val,
          adaptor->val_as_phi(node.val).incoming_val_for_block(cur_ref));
    }
    return;
  }

  // TODO(ts): this is rather inefficient...
  util::SmallVector<u32, 32> ready_indices;
  ready_indices.reserve(nodes.size());
  util::SmallBitSet<256> waiting_nodes;
  waiting_nodes.resize(nodes.size());
  for (u32 i = 0; i < nodes.size(); ++i) {
    if (nodes[i].ref_count) {
      waiting_nodes.mark_set(i);
    } else {
      ready_indices.push_back(i);
    }
  }

  u32 handled_count = 0;
  u32 cur_tmp_part_count = 0;
  u32 cur_tmp_slot = 0;
  u32 cur_tmp_slot_size = 0;
  IRValueRef cur_tmp_val = Adaptor::INVALID_VALUE_REF;
  ScratchWrapper tmp_reg1{this}, tmp_reg2{this};

  const auto move_from_tmp_phi = [&](IRValueRef target_phi) {
    // ref-count the source val
    (void)val_ref(cur_tmp_val).part(0);

    auto phi_vr = val_ref(target_phi);
    if (cur_tmp_part_count <= 2) {
      AssignmentPartRef ap{phi_vr.assignment(), 0};
      assert(!tmp_reg1.cur_reg.invalid());
      if (ap.fixed_assignment()) {
        derived()->mov(
            AsmReg{ap.full_reg_id()}, tmp_reg1.cur_reg, ap.part_size());
      } else {
        derived()->spill_reg(tmp_reg1.cur_reg, ap.frame_off(), ap.part_size());
      }

      if (cur_tmp_part_count == 2) {
        AssignmentPartRef ap_high{phi_vr.assignment(), 1};
        assert(!ap_high.fixed_assignment());
        assert(!tmp_reg2.cur_reg.invalid());
        derived()->spill_reg(
            tmp_reg2.cur_reg, ap_high.frame_off(), ap_high.part_size());
      }
      (void)phi_vr.part(0); // ref-count
      return;
    }

    for (u32 i = 0; i < cur_tmp_part_count; ++i) {
      AssignmentPartRef phi_ap{phi_vr.assignment(), i};
      assert(!phi_ap.fixed_assignment());

      auto slot_off = cur_tmp_slot;
      if (Config::FRAME_INDEXING_NEGATIVE) {
        slot_off -= phi_ap.part_off();
      } else {
        slot_off += phi_ap.part_off();
      }

      auto reg = tmp_reg1.alloc_from_bank(phi_ap.bank());
      derived()->load_from_stack(reg, slot_off, phi_ap.part_size());
      derived()->spill_reg(reg, phi_ap.frame_off(), phi_ap.part_size());
    }
    (void)phi_vr.part(0); // ref-count
  };

  while (handled_count != nodes.size()) {
    if (ready_indices.empty()) {
      // need to break a cycle
      auto cur_idx_opt = waiting_nodes.first_set();
      assert(cur_idx_opt);
      auto cur_idx = *cur_idx_opt;
      assert(nodes[cur_idx].ref_count == 1);
      assert(cur_tmp_val == Adaptor::INVALID_VALUE_REF);

      auto phi_val = nodes[cur_idx].val;
      cur_tmp_part_count = derived()->val_parts(phi_val).count();
      cur_tmp_val = phi_val;

      if (cur_tmp_part_count > 2) {
        // use a stack slot to store the temporaries
        auto *assignment = val_assignment(
            static_cast<ValLocalIdx>(adaptor->val_local_idx(phi_val)));
        cur_tmp_slot = allocate_stack_slot(assignment->size);
        cur_tmp_slot_size = assignment->size;

        for (u32 i = 0; i < cur_tmp_part_count; ++i) {
          auto ap = AssignmentPartRef{assignment, i};
          assert(!ap.fixed_assignment());
          auto slot_off = cur_tmp_slot;
          if (Config::FRAME_INDEXING_NEGATIVE) {
            slot_off -= ap.part_off();
          } else {
            slot_off += ap.part_off();
          }

          if (ap.register_valid()) {
            auto reg = AsmReg{ap.full_reg_id()};
            derived()->spill_reg(reg, slot_off, ap.part_size());
          } else {
            auto reg = tmp_reg1.alloc_from_bank(ap.bank());
            assert(ap.stack_valid());
            derived()->load_from_stack(reg, ap.frame_off(), ap.part_size());
            derived()->spill_reg(reg, slot_off, ap.part_size());
          }
        }
      } else {
        // TODO(ts): if the PHI is not fixed, then we can just reuse its
        // register if it has one
        auto phi_vr = this->val_ref(phi_val);
        phi_vr.disown();
        auto phi_vpr = phi_vr.part(0);
        auto reg = tmp_reg1.alloc_from_bank(phi_vpr.bank());
        phi_vpr.reload_into_specific_fixed(this, reg);

        if (cur_tmp_part_count == 2) {
          // TODO(ts): just change the part ref on the lower ref?
          auto phi_vpr_high = phi_vr.part(1);
          auto reg_high = tmp_reg2.alloc_from_bank(phi_vpr_high.bank());
          phi_vpr_high.reload_into_specific_fixed(this, reg_high);
        }
      }

      nodes[cur_idx].ref_count = 0;
      ready_indices.push_back(cur_idx);
      waiting_nodes.mark_unset(cur_idx);
    }

    for (u32 i = 0; i < ready_indices.size(); ++i) {
      ++handled_count;
      auto cur_idx = ready_indices[i];
      auto phi_val = nodes[cur_idx].val;
      IRValueRef incoming_val =
          adaptor->val_as_phi(phi_val).incoming_val_for_block(cur_ref);
      if (incoming_val == phi_val) {
        // no need to do anything
        continue;
      }

      if (incoming_val == cur_tmp_val) {
        move_from_tmp_phi(phi_val);

        if (cur_tmp_part_count > 2) {
          free_stack_slot(cur_tmp_slot, cur_tmp_slot_size);
          cur_tmp_slot = 0xFFFF'FFFF;
          cur_tmp_slot_size = 0;
        }
        cur_tmp_val = Adaptor::INVALID_VALUE_REF;
        // skip the code below as the ref count of the temp val is
        // already 0 and we don't want to reinsert it into the ready
        // list
        continue;
      }

      move_to_phi(phi_val, incoming_val);

      // check if the incoming val was another PHI
      auto it =
          std::lower_bound(nodes.begin(),
                           nodes.end(),
                           NodeEntry{.val = incoming_val},
                           [](const NodeEntry &lhs, const NodeEntry &rhs) {
                             return lhs.val < rhs.val;
                           });
      if (it == nodes.end() || it->val != incoming_val) {
        continue;
      }

      assert(it->ref_count > 0);
      if (--it->ref_count == 0) {
        auto node_idx = static_cast<u32>(it - nodes.begin());
        ready_indices.push_back(node_idx);
        waiting_nodes.mark_unset(node_idx);
      }
    }
    ready_indices.clear();
  }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::BlockIndex
    CompilerBase<Adaptor, Derived, Config>::next_block() const noexcept {
  return static_cast<BlockIndex>(static_cast<u32>(cur_block_idx) + 1);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::compile_func(
    const IRFuncRef func, const u32 func_idx) noexcept {
  // reset per-func data
  analyzer.reset();

  if (!adaptor->switch_func(func)) {
    return false;
  }
  derived()->analysis_start();
  analyzer.switch_func(func);
  derived()->analysis_end();

  stack.frame_size = derived()->func_reserved_frame_size();
  for (auto &e : stack.fixed_free_lists) {
    e.clear();
  }
  stack.dynamic_free_lists.clear();

  assignments.value_ptrs.clear();
  assignments.value_ptrs.resize(analyzer.liveness.size());

  assignments.alloc.reset();
  assignments.fixed_free_lists = {};
  assignments.delayed_free_lists.clear();
  assignments.delayed_free_lists.resize(analyzer.block_layout.size(),
                                        INVALID_VAL_LOCAL_IDX);

  cur_block_idx =
      static_cast<BlockIndex>(analyzer.block_idx(adaptor->cur_entry_block()));

  register_file.reset();

  derived()->start_func(func_idx);

  block_labels.clear();
  block_labels.reserve(analyzer.block_layout.size());
  for (u32 i = 0; i < analyzer.block_layout.size(); ++i) {
    block_labels.push_back(assembler.label_create());
  }

  if constexpr (Config::DEFAULT_VAR_REF_HANDLING) {
    for (const IRValueRef alloca : adaptor->cur_static_allocas()) {
      // TODO(ts): add a flag in the adaptor to not do this if it is
      // unnecessary?
      const auto local_idx = adaptor->val_local_idx(alloca);
      if (const auto &info = analyzer.liveness_info(local_idx);
          info.ref_count == 1) {
        // the value is ref-counted and not used, so skip it
        continue;
      }

      auto size = adaptor->val_alloca_size(alloca);
      size = util::align_up(size, adaptor->val_alloca_align(alloca));

      auto *assignment = allocate_assignment_slow(1, true);
      const auto frame_off = allocate_stack_slot(size);

      assignment->initialize(frame_off,
                             size,
                             analyzer.liveness_info(local_idx).ref_count,
                             Config::PLATFORM_POINTER_SIZE);
      assignment->variable_ref = true;
      assignments.value_ptrs[local_idx] = assignment;

      auto ap = AssignmentPartRef{assignment, 0};
      ap.reset();
      ap.set_bank(Config::GP_BANK);
      ap.set_part_size(Config::PLATFORM_POINTER_SIZE);
    }
  } else {
    derived()->setup_var_ref_assignments();
  }

  // TODO(ts): place function label
  // TODO(ts): make function labels optional?

  derived()->gen_func_prolog_and_args();
  // TODO(ts): exception handling

  for (u32 i = 0; i < analyzer.block_layout.size(); ++i) {
    const auto block_ref = analyzer.block_layout[i];
    TPDE_LOG_TRACE(
        "Compiling block {} ({})", i, adaptor->block_fmt_ref(block_ref));
    if (!derived()->compile_block(block_ref, i)) [[unlikely]] {
      TPDE_LOG_ERR("Failed to compile block {} ({})",
                   i,
                   adaptor->block_fmt_ref(block_ref));
      return false;
    }
  }

  derived()->finish_func(func_idx);
#ifdef TPDE_ASSERTS
  assert(assembler.func_was_ended());
#endif

  return true;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::compile_block(
    const IRBlockRef block, const u32 block_idx) noexcept {
  cur_block_idx =
      static_cast<typename Analyzer<Adaptor>::BlockIndex>(block_idx);

  assembler.label_place(block_labels[block_idx]);
  auto &&val_range = adaptor->block_insts(block);
  auto end = val_range.end();
  for (auto it = val_range.begin(); it != end; ++it) {
    const IRInstRef inst = *it;
    if (this->adaptor->inst_fused(inst)) {
      continue;
    }

    auto it_cpy = it;
    ++it_cpy;
    if (!derived()->compile_inst(inst, InstRange{.from = it_cpy, .to = end}))
        [[unlikely]] {
      TPDE_LOG_ERR("Failed to compile instruction {}",
                   this->adaptor->inst_fmt_ref(inst));
      return false;
    }
  }

  if (static_cast<u32>(assignments.delayed_free_lists[block_idx]) != ~0u) {
    auto list_entry = assignments.delayed_free_lists[block_idx];
    while (static_cast<u32>(list_entry) != ~0u) {
      auto next_entry = assignments.value_ptrs[static_cast<u32>(list_entry)]
                            ->next_delayed_free_entry;
      derived()->free_assignment(list_entry);
      list_entry = next_entry;
    }
  }
  return true;
}

} // namespace tpde
