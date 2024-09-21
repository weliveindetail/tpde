// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "Analyzer.hpp"
#include "Compiler.hpp"
#include "CompilerConfig.hpp"
#include "IRAdaptor.hpp"

namespace tpde {
// TODO(ts): formulate concept for full compiler so that there is *some* check
// whether all the required derived methods are implemented?

/// The base class for the compiler.
/// It implements the main platform independent compilation logic and houses the
/// analyzer
template <IRAdaptor Adaptor,
          typename Derived,
          CompilerConfig Config = CompilerConfigDefault>
struct CompilerBase {
    // some forwards for the IR type defs
    using IRValueRef = typename Adaptor::IRValueRef;
    using IRBlockRef = typename Adaptor::IRBlockRef;
    using IRFuncRef  = typename Adaptor::IRFuncRef;

    using BlockIndex = typename Analyzer<Adaptor>::BlockIndex;

    using Assembler = typename Config::Assembler;
    using AsmReg    = typename Config::AsmReg;

#pragma region CompilerData
    Adaptor          *adaptor;
    Analyzer<Adaptor> analyzer;

    // data for frame management

    struct {
        /// The current size of the stack frame
        u32                                       frame_size          = 0;
        /// Free-Lists for 1/2/4/8/16 sized allocations
        // TODO(ts): make the allocations for 4/8 different from the others
        // since they are probably the one's most used?
        util::SmallVector<u32, 16>                fixed_free_lists[5] = {};
        /// Free-Lists for all other sizes
        // TODO(ts): think about which data structure we want here
        std::unordered_map<u32, std::vector<u32>> dynamic_free_lists{};
    } stack = {};

    typename Analyzer<Adaptor>::BlockIndex cur_block_idx;

    // Assignments

    enum class ValLocalIdx : u32 {
    };
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
            ValLocalIdx      next_delayed_free_entry;

            struct {
                u32 references_left;
                u8  max_part_size;
                u8  lock_count;

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

    struct AssignmentBuffer {
        std::unique_ptr<u8[]> data;
        u32                   cur_off = 0;
    };

    // TODO(ts): think about different ways to store this that are maybe more
    // compact?
    struct {
        util::SmallVector<AssignmentBuffer, 8> buffers    = {};
        u32                                    cur_buf    = 0;
        u32 cur_fixed_assignment_count[Config::NUM_BANKS] = {};
        util::SmallVector<ValueAssignment *, Analyzer<Adaptor>::SMALL_VALUE_NUM>
            value_ptrs;

        // free lists for two initial size classes (depending on the alignment
        // of ValueAssignment and the part type)
        ValueAssignment                           *fixed_free_lists[2] = {};
        std::unordered_map<u32, ValueAssignment *> dynamic_free_lists;

        util::SmallVector<ValLocalIdx, Analyzer<Adaptor>::SMALL_BLOCK_NUM>
            delayed_free_lists;
    } assignments = {};

    struct RegisterFile;
    RegisterFile register_file;

    Assembler                                    assembler;
    // TODO(ts): smallvector?
    std::vector<typename Assembler::SymRef>      func_syms;
    // TODO(ts): combine this with the block vectors in the analyzer to save on
    // allocations
    util::SmallVector<typename Assembler::Label> block_labels;


    struct ScratchReg;
    struct AssignmentPartRef;
    struct ValuePartRef;
#pragma endregion

    /// Initialize a CompilerBase, should be called by the derived classes
    explicit CompilerBase(Adaptor *adaptor, const bool generate_object)
        : adaptor(adaptor), analyzer(adaptor), assembler(generate_object) {
        static_assert(std::is_base_of_v<CompilerBase, Derived>);
        static_assert(Compiler<Derived, Config>);
    }

    /// shortcut for casting to the Derived class so that overloading
    /// works
    Derived *derived() { return static_cast<Derived *>(this); }

    const Derived *derived() const {
        return static_cast<const Derived *>(this);
    }

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
    ValueAssignment *allocate_assignment(u32  part_count,
                                         bool skip_free_list = false) noexcept;
    /// Puts an assignment back into the free list
    void             deallocate_assignment(ValLocalIdx local_idx) noexcept;

    void init_assignment(IRValueRef value, ValLocalIdx local_idx) noexcept;

    /// Frees an assignment, its stack slot and registers
    void free_assignment(ValLocalIdx local_idx) noexcept;

    u32  allocate_stack_slot(u32 size) noexcept;
    void free_stack_slot(u32 slot, u32 size) noexcept;

  public:
    ValuePartRef val_ref(IRValueRef value, u32 part) noexcept;

    /// Create a reference to a value using its local index
    /// \note The assignment must already exist, if that could not be the case
    /// use val_ref using an IRValueRef
    ValuePartRef val_ref(ValLocalIdx local_idx, u32 part) noexcept;

    /// Try to salvage the register of a value (i.e. if it does not have any
    /// references left) or get it into another register.
    ///
    /// ref_adjust is the amount of references that are allowed
    /// to be left (might be higher if you are currently referencing multiple
    /// parts)
    // TODO(ts): get the register type from the RegisterFile or the
    // CompilerConfig?
    u8 val_salvage(ValuePartRef &&val_ref,
                   ScratchReg    &scratch_out,
                   u32            ref_adjust = 1) noexcept;
    u8 val_salvage_into_specific(ValuePartRef &&val_ref,
                                 ScratchReg    &scratch_out,
                                 AsmReg         reg,
                                 u32            ref_adjust = 1) noexcept;

    /// Get the value as a register
    /// \warning This register must not be overwritten
    AsmReg val_as_reg(ValuePartRef &val_ref, ScratchReg &scratch) noexcept;

    /// Get the value into a specific register
    /// \warning The value is not saved specifically so may not be overwritten
    /// if it might be used later
    AsmReg val_as_specific_reg(ValuePartRef &val_ref, AsmReg reg) noexcept;

    /// Get a defining reference to a value
    ValuePartRef result_ref_lazy(ValLocalIdx local_idx, u32 part) noexcept;

    /// Get a defining reference to a value
    ValuePartRef result_ref_lazy(IRValueRef value, u32 part) noexcept;

    /// Get a defining reference to a value which will already have a register
    /// allocated that can be directly used as a result register
    ValuePartRef result_ref_eager(ValLocalIdx local_idx, u32 part) noexcept;

    /// Get a defining reference to a value which will already have a register
    /// allocated that can be directly used as a result register
    ValuePartRef result_ref_eager(IRValueRef value, u32 part) noexcept;

    /// Get a defining reference to a value and try to salvage the register of
    /// another value if possible, otherwise allocate a register
    ValuePartRef result_ref_salvage(ValLocalIdx    local_idx,
                                    u32            part,
                                    ValuePartRef &&arg,
                                    u32            ref_adjust = 1) noexcept;

    /// Get a defining reference to a value and try to salvage the register of
    /// another value if possible, otherwise allocate a register
    ValuePartRef result_ref_salvage(IRValueRef     value,
                                    u32            part,
                                    ValuePartRef &&arg,
                                    u32            ref_adjust = 1) noexcept;

    /// Get a defining reference to a value that reuses a register of another
    /// value and spills it if necessary. This is useful if the implemented
    /// operation overwrites a register and therefore the original value cannot
    /// be kept
    ValuePartRef result_ref_must_salvage(ValLocalIdx    local_idx,
                                         u32            part,
                                         ValuePartRef &&arg,
                                         u32 ref_adjust = 1) noexcept;

    /// Get a defining reference to a value that reuses a register of another
    /// value and spills it if necessary. This is useful if the implemented
    /// operation overwrites a register and therefore the original value cannot
    /// be kept
    ValuePartRef result_ref_must_salvage(IRValueRef     value,
                                         u32            part,
                                         ValuePartRef &&arg,
                                         u32 ref_adjust = 1) noexcept;

    /// Get a defining reference to a value and try to salvage the register of
    /// another value if possible, otherwise allocate a register. The register
    /// where the other value is located is also returned
    ValuePartRef result_ref_salvage_with_original(ValLocalIdx    local_idx,
                                                  u32            part,
                                                  ValuePartRef &&arg,
                                                  AsmReg        &lhs_reg,
                                                  u32 ref_adjust = 1);

    /// Get a defining reference to a value and try to salvage the register of
    /// another value if possible, otherwise allocate a register. The register
    /// where the other value is located is also returned
    ValuePartRef result_ref_salvage_with_original(IRValueRef     value,
                                                  u32            part,
                                                  ValuePartRef &&arg,
                                                  AsmReg        &lhs_reg,
                                                  u32 ref_adjust = 1);
    // TODO(ts): here we want smth that can output an operand that can house an
    // imm/mem or reg operand and maybe keep the overload that only outputs to a
    // reg? ValuePartRef result_ref_salvage_with_original(ValLocalIdx local_idx,
    // u32 part) noexcept;

    void set_value(ValuePartRef &val_ref, AsmReg reg) noexcept;
    void set_value(ValuePartRef &val_ref, ScratchReg &scratch) noexcept;

    void salvage_reg_for_values(ValuePartRef &to, ValuePartRef &from) noexcept;

    // TODO(ts): switch to a branch_spill_before naming style?
    typename RegisterFile::RegBitSet spill_before_branch() noexcept;
    void release_spilled_regs(typename RegisterFile::RegBitSet) noexcept;

    /// When reaching a point in the function where no other blocks will be
    /// reached anymore, use this function to release register assignments after
    /// the end of that block so the compiler does not accidentally use
    /// registers which don't contain any values
    void release_regs_after_return() noexcept;

    void move_to_phi_nodes(BlockIndex target) noexcept;

    bool branch_needs_split(IRBlockRef target) noexcept;

    BlockIndex next_block() const noexcept;

    bool try_force_fixed_assignment(IRValueRef) const noexcept { return false; }

    bool hook_post_func_sym_init() noexcept { return true; }

  protected:
    bool compile_func(IRFuncRef func, u32 func_idx) noexcept;

    bool compile_block(IRBlockRef block, u32 block_idx) noexcept;
};
} // namespace tpde

#include "AssignmentPartRef.hpp"
#include "RegisterFile.hpp"
#include "ScratchReg.hpp"
#include "ValuePartRef.hpp"

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValueAssignment::initialize(
    u32 frame_off, u32 size, u32 ref_count, u16 max_part_size) noexcept {
    this->frame_off       = frame_off;
    this->size            = size;
    this->references_left = ref_count;
    this->max_part_size   = max_part_size;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::compile() {
    // create function symbols

    assert(func_syms.empty());
    for (const IRFuncRef func : adaptor->funcs()) {
        derived()->define_func_idx(func, func_syms.size());
        if (adaptor->func_extern(func)) {
            func_syms.push_back(derived()->assembler.sym_add_undef(
                adaptor->func_link_name(func), false));
        } else {
            func_syms.push_back(derived()->assembler.sym_predef_func(
                adaptor->func_link_name(func),
                adaptor->func_only_local(func),
                adaptor->func_has_weak_linkage(func)));
        }
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
    assignments.buffers.clear();
    assignments.cur_buf             = 0;
    assignments.fixed_free_lists[0] = assignments.fixed_free_lists[1] = nullptr;
    assignments.dynamic_free_lists.clear();
    assignments.delayed_free_lists.clear();

    assembler.reset();
    func_syms.clear();
    block_labels.clear();
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValueAssignment *
    CompilerBase<Adaptor, Derived, Config>::allocate_assignment(
        const u32 part_count, bool skip_free_list) noexcept {
    u32 size = sizeof(ValueAssignment);

    constexpr u32 PARTS_INCLUDED =
        (sizeof(ValueAssignment) - offsetof(ValueAssignment, parts))
        / sizeof(ValueAssignment::first_part);
    u32 free_list_idx = 0;
    if (part_count > PARTS_INCLUDED) {
        size +=
            (part_count - PARTS_INCLUDED) * sizeof(ValueAssignment::first_part);
        size = util::align_up(size, alignof(ValueAssignment));
        assert((size & (alignof(ValueAssignment) - 1)) == 0);

        free_list_idx = (((part_count - PARTS_INCLUDED)
                          * sizeof(ValueAssignment::first_part))
                         / alignof(ValueAssignment))
                        + 1;
    }
    assert(size <= ASSIGNMENT_BUF_SIZE);

    if (!skip_free_list) {
        if (free_list_idx <= 1) [[likely]] {
            if (auto *a = assignments.fixed_free_lists[free_list_idx];
                a != nullptr) {
                assignments.fixed_free_lists[free_list_idx] =
                    a->next_free_list_entry;

                for (u32 i = 0; i < part_count; ++i) {
                    a->parts[i] = 0;
                }
                return a;
            }
        } else {
            auto it = assignments.dynamic_free_lists.find(free_list_idx);
            if (it != assignments.dynamic_free_lists.end()) {
                if (auto *a = it->second; a != nullptr) {
                    it->second = a->next_free_list_entry;
                    for (u32 i = 0; i < part_count; ++i) {
                        a->parts[i] = 0;
                    }
                    return a;
                }
                // TODO(ts): remove if it->second is nullptr?
            }
        }
    }

    // need to allocate new assignment
    if (ASSIGNMENT_BUF_SIZE - assignments.buffers[assignments.cur_buf].cur_off
        >= size) {
        auto &buf = assignments.buffers[assignments.cur_buf];
        auto *assignment =
            reinterpret_cast<ValueAssignment *>(buf.data.get() + buf.cur_off);
        buf.cur_off += size;

        new (assignment) ValueAssignment{};
        for (u32 i = 0; i < part_count; ++i) {
            assignment->parts[i] = 0;
        }
        return assignment;
    }

    ++assignments.cur_buf;
    if (assignments.cur_buf == assignments.buffers.size()) {
        assignments.buffers.emplace_back(
            std::make_unique<u8[]>(ASSIGNMENT_BUF_SIZE), 0);
    }
    assert(assignments.cur_buf < assignments.buffers.size());
    assert(assignments.buffers[assignments.cur_buf].cur_off == 0);

    auto &buf        = assignments.buffers[assignments.cur_buf];
    auto *assignment = reinterpret_cast<ValueAssignment *>(buf.data.get());
    buf.cur_off      = size;

    new (assignment) ValueAssignment{};
    for (u32 i = 0; i < part_count; ++i) {
        assignment->parts[i] = 0;
    }
    return assignment;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::deallocate_assignment(
    ValLocalIdx local_idx) noexcept {
    auto *assignment = assignments.value_ptrs[static_cast<u32>(local_idx)];

    u32 part_count = 0;
    while (true) {
        auto ap = AssignmentPartRef{assignment, part_count++};
        if (!ap.has_next_part()) {
            break;
        }
    }

    constexpr u32 PARTS_INCLUDED =
        (sizeof(ValueAssignment) - offsetof(ValueAssignment, parts))
        / sizeof(ValueAssignment::first_part);
    u32 free_list_idx = 0;
    if (part_count > PARTS_INCLUDED) {
        free_list_idx = (((part_count - PARTS_INCLUDED)
                          * sizeof(ValueAssignment::first_part))
                         / alignof(ValueAssignment))
                        + 1;
    }

    if (free_list_idx <= 1) [[likely]] {
        assignment->next_free_list_entry =
            assignments.fixed_free_lists[free_list_idx];
        assignments.fixed_free_lists[free_list_idx] = assignment;
    } else {
        auto &entry = assignments.dynamic_free_lists[free_list_idx];
        assignment->next_free_list_entry = entry;
        entry                            = assignment;
    }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::init_assignment(
    IRValueRef value, ValLocalIdx local_idx) noexcept {
    assert(val_assignment(local_idx) == nullptr);

    const u32 part_count = derived()->val_part_count(value);
    assert(part_count > 0);
    auto *assignment = allocate_assignment(part_count);
    assignments.value_ptrs[static_cast<u32>(local_idx)] = assignment;

    u32 max_part_size = 0;
    for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
        auto ap = AssignmentPartRef{assignment, part_idx};
        ap.set_bank(derived()->val_part_bank(value, part_idx));
        const u32 size = derived()->val_part_size(value, part_idx);
        assert(size > 0);
        max_part_size = std::max(max_part_size, size);
        ap.set_part_size(size);

        if (part_idx != part_count - 1) {
            ap.set_has_next_part(true);
        }
    }

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
        const auto &liveness =
            analyzer.liveness_info(static_cast<u32>(local_idx));
        auto ap = AssignmentPartRef{assignment, 0};

        auto try_fixed =
            liveness.last > cur_block_idx
            && cur_loop.definitions_in_childs
                       + assignments.cur_fixed_assignment_count[ap.bank()]
                   < Derived::NUM_FIXED_ASSIGNMENTS[ap.bank()];
        if (derived()->try_force_fixed_assignment(value)) {
            try_fixed = assignments.cur_fixed_assignment_count[ap.bank()]
                        < Derived::NUM_FIXED_ASSIGNMENTS[ap.bank()];
        }

        if (try_fixed) {
            // check if there is a fixed register available
            AsmReg reg =
                derived()->select_fixed_assignment_reg(ap.bank(), value);
            TPDE_LOG_TRACE("Trying to assign fixed reg to value {}",
                           static_cast<u32>(local_idx));

            if (!reg.invalid()) {
                if (register_file.is_used(reg)) {
                    assert(!register_file.is_fixed(reg));
                    auto *spill_assignment =
                        val_assignment(register_file.reg_local_idx(reg));
                    auto spill_ap = AssignmentPartRef{
                        spill_assignment, register_file.reg_part(reg)};
                    assert(!spill_ap.fixed_assignment());
                    assert(spill_ap.variable_ref() || !spill_ap.modified());
                    spill_ap.set_register_valid(false);
                    register_file.unmark_used(reg);
                }

                TPDE_LOG_TRACE(
                    "Assigning fixed assignment to reg {} for value {}",
                    reg.id(),
                    static_cast<u32>(local_idx));
                ap.set_full_reg_id(reg.id());
                ap.set_register_valid(true);
                ap.set_fixed_assignment(true);
                register_file.mark_used(reg, local_idx, 0);
                register_file.mark_fixed(reg);
                register_file.mark_clobbered(reg);
                ++assignments.cur_fixed_assignment_count[ap.bank()];
            }
        }
    }

    const auto size      = max_part_size * part_count;
    auto       frame_off = allocate_stack_slot(size);
    if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
        frame_off += size;
    }

    assert(max_part_size <= 256);
    assignment->max_part_size = max_part_size;
    assignment->lock_count    = 0;
    assignment->size          = size;
    assignment->frame_off     = frame_off;
    assignment->references_left =
        analyzer.liveness_info(static_cast<u32>(local_idx)).ref_count;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::free_assignment(
    const ValLocalIdx local_idx) noexcept {
    ValueAssignment *assignment =
        assignments.value_ptrs[static_cast<u32>(local_idx)];
    const auto is_var_ref = AssignmentPartRef{assignment, 0}.variable_ref();

    // free registers
    u32 part_idx = 0;
    while (true) {
        auto ap = AssignmentPartRef{assignment, part_idx};
        if (ap.fixed_assignment()) {
            const auto reg = AsmReg{ap.full_reg_id()};
            assert(register_file.is_fixed(reg));
            assert(register_file.reg_local_idx(reg) == local_idx);
            assert(register_file.reg_part(reg) == part_idx);
            register_file.unmark_fixed(reg);
            register_file.unmark_used(reg);
            ap.set_fixed_assignment(false);
            ap.set_register_valid(false);
            --assignments.cur_fixed_assignment_count[ap.bank()];
        } else if (ap.register_valid()) {
            const auto reg = AsmReg{ap.full_reg_id()};
            assert(!register_file.is_fixed(reg));
            register_file.unmark_used(reg);
            ap.set_register_valid(false);
        }
        if (!ap.has_next_part()) {
            break;
        }
        ++part_idx;
    }

    // variable references do not have a stack slot
    if (!is_var_ref) {
        auto slot = assignment->frame_off;
        if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
            slot -= assignment->size;
        }
        free_stack_slot(slot, assignment->size);
    }

    // TODO(ts): calculating part count twice here
    deallocate_assignment(local_idx);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
u32 CompilerBase<Adaptor, Derived, Config>::allocate_stack_slot(
    u32 size) noexcept {
    assert(size > 0);
    if (size <= 16) {
        assert(size == 1 || size == 2 || size == 4 || size == 8 || size == 16);

        const u32 free_list_idx = util::cnt_tz(size);
        if (!stack.fixed_free_lists[free_list_idx].empty()) {
            const auto slot = stack.fixed_free_lists[free_list_idx].back();
            stack.fixed_free_lists[free_list_idx].pop_back();
            return slot;
        }

        // align the frame size
        for (u32 list_idx = util::cnt_tz(stack.frame_size);
             list_idx < free_list_idx;
             list_idx = util::cnt_tz(stack.frame_size)) {
            stack.fixed_free_lists[list_idx].push_back(stack.frame_size);
            stack.frame_size += 1ull << list_idx;
        }
    } else {
        // align the size to 16
        size = util::align_up(size, 16);

        auto it = stack.dynamic_free_lists.find(size);
        if (it != stack.dynamic_free_lists.end() && !it->second.empty()) {
            const auto slot = it->second.back();
            it->second.pop_back();
            return slot;
        }

        // align the frame size up to 16
        for (u32 list_idx = util::cnt_tz(stack.frame_size); list_idx < 5;
             list_idx     = util::cnt_tz(stack.frame_size)) {
            stack.fixed_free_lists[list_idx].push_back(stack.frame_size);
            stack.frame_size += 1ull << list_idx;
        }
    }

    const auto slot   = stack.frame_size;
    stack.frame_size += size;
    return slot;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::free_stack_slot(
    const u32 slot, u32 size) noexcept {
    if (size <= 16) {
        assert(size == 1 || size == 2 || size == 4 || size == 8 || size == 16);

        const u32 free_list_idx = util::cnt_tz(size);
        stack.fixed_free_lists[free_list_idx].push_back(slot);
    } else {
        // align the size to 16
        size = util::align_up(size, 16);

        stack.dynamic_free_lists[size].push_back(slot);
    }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::val_ref(IRValueRef value,
                                                    u32        part) noexcept {
    const auto local_idx =
        static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

    // TODO(ts): use an overload with the IRValueRef?
    if (auto ref = derived()->val_ref_special(local_idx, part); ref) {
        return std::move(*ref);
    }

    if (val_assignment(local_idx) == nullptr) {
        init_assignment(value, local_idx);
    }
    assert(val_assignment(local_idx) != nullptr);

    return ValuePartRef{this, local_idx, part};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::val_ref(ValLocalIdx local_idx,
                                                    u32         part) noexcept {
    if (auto ref = derived()->val_ref_special(local_idx, part); ref) {
        return *ref;
    }

    assert(assignments.value_ptrs[static_cast<u32>(local_idx)] != nullptr);
    return ValuePartRef{this, local_idx, part};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::val_as_reg(
        ValuePartRef &val_ref, ScratchReg &scratch) noexcept {
    if (val_ref.is_const) {
        // TODO(ts): materialize constant
        (void)scratch;
        assert(0);
        exit(1);
    }

    const auto reg = val_ref.alloc_reg(true);
    val_ref.lock();
    return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::val_as_specific_reg(
        ValuePartRef &val_ref, AsmReg reg) noexcept {
    if (val_ref.is_const) {
        // TODO(ts): materialize constant
        (void)reg;
        assert(0);
        exit(1);
    }

    assert(!val_ref.assignment().fixed_assignment());
    const auto res = val_ref.move_into_specific(reg);
    val_ref.lock();
    return res;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_lazy(
        const ValLocalIdx local_idx, const u32 part) noexcept {
    assert(val_assignment(local_idx) != nullptr);
    return ValuePartRef{this, local_idx, part};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_lazy(IRValueRef value,
                                                            u32 part) noexcept {
    const auto local_idx =
        static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

    if (val_assignment(local_idx) == nullptr) {
        init_assignment(value, local_idx);
    }
    assert(val_assignment(local_idx) != nullptr);

    return ValuePartRef{this, local_idx, part};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_eager(
        ValLocalIdx local_idx, u32 part) noexcept {
    assert(val_assignment(local_idx) != nullptr);
    auto res_ref = ValuePartRef{this, local_idx, part};
    res_ref.alloc_reg(false);
    return res_ref;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_eager(
        IRValueRef value, u32 part) noexcept {
    const auto local_idx =
        static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

    if (val_assignment(local_idx) == nullptr) {
        init_assignment(value, local_idx);
    }
    assert(val_assignment(local_idx) != nullptr);

    return result_ref_eager(local_idx, part);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_must_salvage(
        ValLocalIdx    local_idx,
        u32            part,
        ValuePartRef &&arg,
        u32            ref_adjust) noexcept {
    assert(val_assignment(local_idx) != nullptr);
    auto res_ref = ValuePartRef{this, local_idx, part};
    auto ap_res  = res_ref.assignment();
    assert(ap_res.bank() == arg.bank());

    if (arg.is_const) {
        const auto reg = res_ref.alloc_reg(false);
        (void)reg;
        // TODO(ts): materialize constant
        assert(0);
        exit(1);
    } else {
        auto ap_arg = arg.assignment();

        const auto &liveness = analyzer.liveness_info((u32)arg.local_idx());
        if (ap_arg.register_valid() && arg.ref_count() <= ref_adjust
            && (liveness.last < cur_block_idx
                || (liveness.last == cur_block_idx && !liveness.last_full))) {
            // can salvage
            arg.unlock();
            salvage_reg_for_values(res_ref, arg);
        } else {
            const auto reg = res_ref.alloc_reg(false);
            arg.reload_into_specific(this, reg);
        }
    }

    res_ref.lock();
    return res_ref;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_must_salvage(
        IRValueRef     value,
        u32            part,
        ValuePartRef &&arg,
        u32            ref_adjust) noexcept {
    const auto local_idx =
        static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

    if (val_assignment(local_idx) == nullptr) {
        init_assignment(value, local_idx);
    }
    assert(val_assignment(local_idx) != nullptr);

    return result_ref_must_salvage(local_idx, part, std::move(arg), ref_adjust);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_salvage_with_original(
        ValLocalIdx    local_idx,
        u32            part,
        ValuePartRef &&arg,
        AsmReg        &lhs_reg,
        u32            ref_adjust) {
    assert(val_assignment(local_idx) != nullptr);

    auto res_ref = ValuePartRef{this, local_idx, part};
    auto ap_res  = res_ref.assignment();
    assert(ap_res.bank() == arg.bank());

    if (arg.is_const) {
        lhs_reg = res_ref.alloc_reg(false);
        // TODO(ts): materialize constant
        assert(0);
        exit(1);
    } else {
        lhs_reg = arg.alloc_reg();

        const auto &liveness = analyzer.liveness_info((u32)arg.local_idx());
        if (arg.ref_count() <= ref_adjust
            && (liveness.last < cur_block_idx
                || (liveness.last == cur_block_idx && !liveness.last_full))) {
            // can salvage
            arg.unlock();

            salvage_reg_for_values(res_ref, arg);
            lhs_reg = res_ref.cur_reg();
        } else {
            res_ref.alloc_reg(false);
        }
    }

    res_ref.lock();
    return res_ref;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
typename CompilerBase<Adaptor, Derived, Config>::ValuePartRef
    CompilerBase<Adaptor, Derived, Config>::result_ref_salvage_with_original(
        IRValueRef     value,
        u32            part,
        ValuePartRef &&arg,
        AsmReg        &lhs_reg,
        u32            ref_adjust) {
    const auto local_idx =
        static_cast<ValLocalIdx>(analyzer.adaptor->val_local_idx(value));

    if (val_assignment(local_idx) == nullptr) {
        init_assignment(value, local_idx);
    }
    assert(val_assignment(local_idx) != nullptr);

    return result_ref_salvage_with_original(
        local_idx, part, std::move(arg), lhs_reg, ref_adjust);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::set_value(ValuePartRef &val_ref,
                                                       AsmReg reg) noexcept {
    auto ap = val_ref.assignment();

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

    assert(!register_file.is_used(reg));

    register_file.mark_used(reg, val_ref.local_idx(), val_ref.part());
    register_file.mark_clobbered(reg);
    ap.set_full_reg_id(reg.id());
    ap.set_register_valid(true);
    ap.set_modified(true);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::set_value(
    ValuePartRef &val_ref, ScratchReg &scratch) noexcept {
    auto ap = val_ref.assignment();
    assert(!scratch.cur_reg.invalid());
    auto reg = scratch.cur_reg;

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

    scratch.reset();
    assert(!register_file.is_used(reg));

    register_file.mark_used(reg, val_ref.local_idx(), val_ref.part());
    register_file.mark_clobbered(reg);
    ap.set_full_reg_id(reg.id());
    ap.set_register_valid(true);
    ap.set_modified(true);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::salvage_reg_for_values(
    ValuePartRef &to, ValuePartRef &from) noexcept {
    assert(!to.is_const);
    assert(!from.is_const);

    const auto from_reg = from.cur_reg();
    auto       ap_from  = from.assignment();
    auto       ap_to    = to.assignment();

    if (ap_from.fixed_assignment()) {
        --assignments.cur_fixed_assignment_count[ap_from.bank()];

        // take the register from the argument value
        // with some special handling in case the result has a fixed
        // assignment
        if (ap_to.fixed_assignment()) {
            // free the register of `to` since we reuse `from`'s register
            const AsmReg res_reg = AsmReg{ap_to.full_reg_id()};
            register_file.unmark_fixed(res_reg);
            register_file.unmark_used(res_reg);
        } else {
            assert(!ap_to.register_valid());
            register_file.unmark_fixed(from_reg);
        }

        ap_from.set_fixed_assignment(false);
        ap_from.set_register_valid(false);
        ap_to.set_register_valid(true);
        ap_to.set_full_reg_id(from_reg.id());
        register_file.update_reg_assignment(
            from_reg, to.local_idx(), to.part());
    } else if (ap_to.fixed_assignment()) {
        // cannot salvage if result has a fixed assignment but the
        // source has not since this might cause non-callee-saved regs
        // to become fixed which would cause issues for calls
        const AsmReg to_reg = AsmReg{ap_to.full_reg_id()};
        assert(ap_from.part_size() >= ap_to.part_size());
        derived()->mov(to_reg, from_reg, ap_to.part_size());
    } else {
        assert(!register_file.is_fixed(from_reg));
        ap_from.set_register_valid(false);
        ap_to.set_full_reg_id(from_reg.id());
        ap_to.set_register_valid(true);
        register_file.update_reg_assignment(
            from_reg, to.local_idx(), to.part());
    }
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

    const IRBlockRef cur_block_ref      = analyzer.block_ref(cur_block_idx);
    auto             next_block_is_succ = false;
    auto             next_block_has_multiple_incoming = false;
    u32              succ_count                       = 0;
    for (const IRBlockRef succ : adaptor->block_succs(cur_block_ref)) {
        ++succ_count;
        if (static_cast<u32>(analyzer.block_idx(succ))
            == static_cast<u32>(cur_block_idx) + 1) {
            next_block_is_succ = true;
            if (analyzer.block_has_multiple_incoming(succ)) {
                next_block_has_multiple_incoming = true;
            }
        }
    }

    if (succ_count == 1 && next_block_is_succ
        && !next_block_has_multiple_incoming) {
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

    u16 phi_ref_count[RegisterFile::MAX_ID + 1] = {};
    for (const IRBlockRef succ : adaptor->block_succs(cur_block_ref)) {
        for (const IRValueRef phi_val : adaptor->block_phis(succ)) {
            const auto       phi_ref = adaptor->val_as_phi(phi_val);
            const IRValueRef inc_val =
                phi_ref.incoming_val_for_block(cur_block_ref);
            auto *assignment = val_assignment(val_idx(inc_val));
            u32   part_count = derived()->val_part_count(inc_val);
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
        auto part      = register_file.reg_part(AsmReg{reg});
        if (local_idx == INVALID_VAL_LOCAL_IDX) {
            // TODO(ts): can this actually happen?
            assert(0);
            return false;
        }
        auto *assignment = val_assignment(local_idx);
        auto  ap         = AssignmentPartRef{assignment, part};
        if (ap.fixed_assignment()) {
            // fixed registers do not need to be spilled
            return true;
        }

        if (!ap.modified()) {
            // no need to spill if the value was not modified
            return false;
        }

        const auto &liveness =
            analyzer.liveness_info(static_cast<u32>(local_idx));
        if (assignment->references_left <= phi_ref_count[reg]
            && liveness.last <= cur_block_idx) {
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
            if (static_cast<u32>(block_idx)
                == static_cast<u32>(cur_block_idx) + 1) {
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
        if (!reg_fixed
            && (next_block_has_multiple_incoming || !next_block_is_succ)) {
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

        auto local_idx = register_file.reg_local_idx(reg);
        auto part      = register_file.reg_part(reg);
        assert(local_idx != INVALID_VAL_LOCAL_IDX);
        assert(!register_file.is_fixed(reg));
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
        auto reg       = AsmReg{reg_id};
        auto local_idx = register_file.reg_local_idx(reg);
        auto part      = register_file.reg_part(reg);
        assert(local_idx != INVALID_VAL_LOCAL_IDX);
        assert(!register_file.is_fixed(reg));
        auto ap = AssignmentPartRef{val_assignment(local_idx), part};
        ap.set_register_valid(false);
        register_file.unmark_used(reg);
    }
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::move_to_phi_nodes(
    BlockIndex target) noexcept {
    // PHI-nodes are always moved to their stack-slot (unless they are fixed)
    //
    // However, we need to take care of PHI-dependencies (cycles and chains)
    // as to not overwrite values which might be needed.
    //
    // In most cases, we expect the number of PHIs to be small but we want to
    // stay reasonably efficient even with larger numbers of PHIs

    IRBlockRef target_ref = analyzer.block_ref(target);
    IRBlockRef cur_ref    = analyzer.block_ref(cur_block_idx);

    // collect all the nodes
    struct NodeEntry {
        IRValueRef val;
        u32        ref_count;
    };

    util::SmallVector<NodeEntry, 16> nodes;
    for (IRValueRef phi : adaptor->block_phis(target_ref)) {
        nodes.push_back(NodeEntry{phi, 0});
    }

    if (nodes.empty()) {
        return;
    }

    const auto move_to_phi = [this](IRValueRef phi, IRValueRef incoming_val) {
        // TODO(ts): if phi==incoming_val, we should be able to elide the move
        // even if the phi is in a fixed register, no?

        u32        part_count = derived()->val_part_count(incoming_val);
        ScratchReg scratch(this);
        for (u32 i = 0; i < part_count; ++i) {
            // TODO(ts): just have this outside the loop and change the part
            // index? :P
            auto phi_ref = val_ref(phi, i);
            auto val_ref = derived()->val_ref(incoming_val, i);

            AsmReg reg{};
            if (val_ref.is_const) {
                // TODO(ts): materialize constant
                assert(0);
                exit(1);
            } else if (val_ref.assignment().register_valid()
                       || val_ref.assignment().fixed_assignment()) {
                reg = AsmReg{val_ref.assignment().full_reg_id()};
            } else {
                reg = val_ref.reload_into_specific_fixed(
                    this,
                    scratch.alloc_from_bank(
                        derived()->val_part_bank(incoming_val, i)));
            }

            auto phi_ap = phi_ref.assignment();
            if (phi_ap.fixed_assignment()) {
                derived()->mov(
                    AsmReg{phi_ap.full_reg_id()}, reg, phi_ap.part_size());
            } else {
                derived()->spill_reg(
                    reg, phi_ap.frame_off(), phi_ap.part_size());
            }

            if (phi_ap.register_valid() && !phi_ap.fixed_assignment()) {
                auto cur_reg = AsmReg{phi_ap.full_reg_id()};
                assert(!register_file.is_fixed(cur_reg));
                register_file.unmark_used(cur_reg);
                phi_ap.set_register_valid(false);
            }

            if (i != part_count - 1) {
                val_ref.inc_ref_count();
                phi_ref.inc_ref_count();
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
        auto phi_ref      = adaptor->val_as_phi(node.val);
        auto incoming_val = phi_ref.incoming_val_for_block(cur_ref);

        // TODO(ts): special handling for self-references?
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

    u32        handled_count      = 0;
    u32        cur_tmp_part_count = 0;
    u32        cur_tmp_slot       = 0;
    u32        cur_tmp_slot_size  = 0;
    IRValueRef cur_tmp_val        = Adaptor::INVALID_VALUE_REF;
    ScratchReg tmp_reg1{this}, tmp_reg2{this};

    const auto move_from_tmp_phi = [&](IRValueRef target_phi) {
        // ref-count the source val
        auto inc_ref = val_ref(cur_tmp_val, 0);
        inc_ref.reset();

        ScratchReg scratch(this);
        if (cur_tmp_part_count <= 2) {
            auto phi_ref = val_ref(target_phi, 0);
            auto ap      = phi_ref.assignment();
            assert(!tmp_reg1.cur_reg.invalid());
            if (ap.fixed_assignment()) {
                derived()->mov(
                    AsmReg{ap.full_reg_id()}, tmp_reg1.cur_reg, ap.part_size());
            } else {
                derived()->spill_reg(
                    tmp_reg1.cur_reg, ap.frame_off(), ap.part_size());
            }

            if (cur_tmp_part_count == 2) {
                phi_ref.inc_ref_count();
                auto phi_ref_high = val_ref(target_phi, 1);
                auto ap_high      = phi_ref_high.assignment();
                assert(!ap_high.fixed_assignment());
                assert(!tmp_reg2.cur_reg.invalid());
                derived()->spill_reg(
                    tmp_reg2.cur_reg, ap_high.frame_off(), ap_high.part_size());
            }
            return;
        }

        for (u32 i = 0; i < cur_tmp_part_count; ++i) {
            // TODO(ts): just have this outside the loop and change the part
            // index? :P
            auto phi_ref = val_ref(target_phi, i);
            auto phi_ap  = phi_ref.assignment();
            assert(!phi_ap.fixed_assignment());

            auto reg = tmp_reg1.alloc_from_bank(phi_ap.bank());
            derived()->load_from_stack(
                reg, cur_tmp_slot + phi_ap.part_off(), phi_ap.part_size());
            derived()->spill_reg(reg, phi_ap.frame_off(), phi_ap.part_size());

            if (i != cur_tmp_part_count - 1) {
                phi_ref.inc_ref_count();
            }
        }
    };

    while (handled_count != nodes.size()) {
        if (ready_indices.empty()) {
            // need to break a cycle
            auto cur_idx = *waiting_nodes.first_set();
            assert(nodes[cur_idx].ref_count == 1);
            assert(cur_tmp_val == Adaptor::INVALID_VALUE_REF);

            auto phi_val       = nodes[cur_idx].val;
            cur_tmp_part_count = derived()->val_part_count(phi_val);
            cur_tmp_val        = phi_val;

            if (cur_tmp_part_count > 2) {
                // use a stack slot to store the temporaries
                auto *assignment = val_assignment(
                    static_cast<ValLocalIdx>(adaptor->val_local_idx(phi_val)));
                cur_tmp_slot = allocate_stack_slot(assignment->size);

                for (u32 i = 0; i < cur_tmp_part_count; ++i) {
                    auto ap = AssignmentPartRef{assignment, i};
                    assert(!ap.fixed_assignment());
                    if (ap.register_valid()) {
                        auto reg = AsmReg{ap.full_reg_id()};
                        derived()->spill_reg(
                            reg, cur_tmp_slot + ap.part_off(), ap.part_size());
                    } else {
                        auto reg = tmp_reg1.alloc_from_bank(ap.bank());
                        derived()->load_from_stack(
                            reg, ap.frame_off(), ap.part_size());
                        derived()->spill_reg(
                            reg, cur_tmp_slot + ap.part_off(), ap.part_size());
                    }
                }
            } else {
                // TODO(ts): if the PHI is not fixed, then we can just reuse its
                // register if it has one
                auto phi_ref = val_ref(phi_val, 0);
                phi_ref.inc_ref_count();

                auto reg = tmp_reg1.alloc_from_bank(phi_ref.bank());
                phi_ref.reload_into_specific_fixed(this, reg);

                if (cur_tmp_part_count == 2) {
                    // TODO(ts): just change the part ref on the lower ref?
                    auto phi_ref_high = val_ref(phi_val, 1);
                    phi_ref_high.inc_ref_count();

                    auto reg_high =
                        tmp_reg2.alloc_from_bank(phi_ref_high.bank());
                    phi_ref_high.reload_into_specific_fixed(this, reg_high);
                }
            }

            nodes[cur_idx].ref_count = 0;
            ready_indices.push_back(cur_idx);
        }

        for (u32 i = 0; i < ready_indices.size(); ++i) {
            ++handled_count;
            auto       cur_idx = ready_indices[i];
            auto       phi_val = nodes[cur_idx].val;
            IRValueRef incoming_val =
                adaptor->val_as_phi(phi_val).incoming_val_for_block(cur_ref);
            if (incoming_val == cur_tmp_val) {
                move_from_tmp_phi(phi_val);

                if (cur_tmp_part_count > 2) {
                    free_stack_slot(cur_tmp_slot, cur_tmp_slot_size);
                    cur_tmp_slot      = 0xFFFF'FFFF;
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
            auto it = std::lower_bound(
                nodes.begin(),
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
bool CompilerBase<Adaptor, Derived, Config>::branch_needs_split(
    IRBlockRef target) noexcept {
    // for now, if the target has PHI-nodes, we split
    for (auto phi : adaptor->block_phis(target)) {
        (void)phi;
        return true;
    }

    return false;
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

    adaptor->switch_func(func);
    analyzer.switch_func(func);

    stack.frame_size = 0;
    for (auto &e : stack.fixed_free_lists) {
        e.clear();
    }
    stack.dynamic_free_lists.clear();

    assignments.value_ptrs.clear();
    assignments.value_ptrs.resize(analyzer.liveness.size());
    for (auto &buf : assignments.buffers) {
        buf.cur_off = 0;
    }
    assignments.cur_buf             = 0;
    assignments.fixed_free_lists[0] = assignments.fixed_free_lists[1] = nullptr;
    assignments.dynamic_free_lists.clear();
    assignments.delayed_free_lists.clear();
    assignments.delayed_free_lists.resize(analyzer.block_layout.size(),
                                          INVALID_VAL_LOCAL_IDX);

    if (assignments.buffers.empty()) {
        assignments.buffers.emplace_back(
            std::make_unique<u8[]>(ASSIGNMENT_BUF_SIZE), 0);
    }

    cur_block_idx =
        static_cast<BlockIndex>(analyzer.block_idx(adaptor->cur_entry_block()));

    derived()->reset_register_file();

    assembler.start_func(func_syms[func_idx]);

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
            size      = util::align_up(size, adaptor->val_alloca_align(alloca));

            auto *assignment = allocate_assignment(1, true);
            auto  frame_off  = allocate_stack_slot(size);
            if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
                frame_off += size;
            }

            assignment->initialize(frame_off,
                                   size,
                                   analyzer.liveness_info(local_idx).ref_count,
                                   Config::PLATFORM_POINTER_SIZE);
            assignments.value_ptrs[local_idx] = assignment;

            auto ap = AssignmentPartRef{assignment, 0};
            ap.set_bank(Config::GP_BANK);
            ap.set_variable_ref(true);
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
        if (!derived()->compile_block(block_ref, i)) {
            return false;
        }
    }

    derived()->finish_func();
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
    for (const IRValueRef value : adaptor->block_values(block)) {
        if (!derived()->compile_inst(value)) {
            return false;
        }
    }

    if (static_cast<u32>(assignments.delayed_free_lists[block_idx]) != ~0u) {
        auto list_entry = assignments.delayed_free_lists[block_idx];
        while (static_cast<u32>(list_entry) != ~0u) {
            auto next_entry =
                assignments.value_ptrs[static_cast<u32>(list_entry)]
                    ->next_delayed_free_entry;
            derived()->free_assignment(list_entry);
            list_entry = next_entry;
        }
    }
    return true;
}

} // namespace tpde
