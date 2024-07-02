// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "Analyzer.hpp"
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
        u32 frame_off;
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
        util::SmallVector<AssignmentBuffer, 8> buffers = {};
        u32                                    cur_buf = 0;
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

    Assembler                               assembler;
    // TODO(ts): smallvector?
    std::vector<typename Assembler::SymRef> func_syms;


    struct ScratchReg;
    struct AssignmentPartRef;
    struct ValuePartRef;
#pragma endregion

    /// Initialize a CompilerBase, should be called by the derived classes
    explicit CompilerBase(Adaptor *adaptor, const bool generate_object)
        : adaptor(adaptor), analyzer(adaptor), assembler(generate_object) {
        static_assert(std::is_base_of_v<CompilerBase, Derived>);
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

    ValuePartRef result_ref_lazy(ValLocalIdx local_idx, u32 part) noexcept;
    ValuePartRef result_ref_lazy(IRValueRef value, u32 part) noexcept;
    ValuePartRef result_ref_eager(ValLocalIdx local_idx, u32 part) noexcept;
    ValuePartRef result_ref_salvage(ValLocalIdx    local_idx,
                                    u32            part,
                                    ValuePartRef &&arg,
                                    u32            ref_adjust = 1) noexcept;
    ValuePartRef result_ref_salvage_into_result(ValLocalIdx    local_idx,
                                                u32            part,
                                                ValuePartRef &&arg,
                                                u32 ref_adjust = 1) noexcept;
    // TODO(ts): here we want smth that can output an operand that can house an
    // imm/mem or reg operand and maybe keep the overload that only outputs to a
    // reg? ValuePartRef result_ref_salvage_with_original(ValLocalIdx local_idx,
    // u32 part) noexcept;

    bool compile_func(IRFuncRef func, u32 func_idx) noexcept;

    bool compile_block(IRBlockRef block, u32 block_idx) noexcept;
};

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
        func_syms.push_back(derived()->assembler.sym_predef_func(
            adaptor->func_link_name(func),
            adaptor->func_only_local(func),
            adaptor->func_has_weak_linkage(func)));
    }

    // TODO(ts): create function labels?

    u32 func_idx = 0;
    for (const IRFuncRef func : adaptor->funcs()) {
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
                return a;
            }
        } else {
            auto it = assignments.dynamic_free_lists.find(free_list_idx);
            if (it != assignments.dynamic_free_lists.end()) {
                if (auto *a = it->second; a != nullptr) {
                    it->second = a->next_free_list_entry;
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

    const u32 part_count = analyzer.adaptor->val_part_count(value);
    auto     *assignment = allocate_assignment(part_count);
    assignments.value_ptrs[static_cast<u32>(local_idx)] = assignment;

    u32 max_part_size = 0;
    for (u32 part_idx = 0; part_idx < part_count; ++part_idx) {
        auto ap = AssignmentPartRef{assignment, part_count};
        ap.set_bank(analyzer.adaptor->val_part_bank(value, part_idx));
        const u32 size = analyzer.adaptor->val_part_size(value, part_idx);
        assert(size > 0);
        max_part_size = std::max(max_part_size, size);
        ap.set_part_size(size);
    }

    const auto size      = max_part_size * part_count;
    auto       frame_off = allocate_stack_slot(size);
    if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
        frame_off += size;
    }

    assert(max_part_size <= 256);
    assignment->max_part_size   = max_part_size;
    assignment->lock_count      = 0;
    assignment->size            = size;
    assignment->frame_off       = frame_off;
    assignment->references_left = analyzer.liveness(local_idx).ref_count;
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
        if (ap.register_valid()) {
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
        return *ref;
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

    return val_ref.alloc_reg(true);
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

    return val_ref.move_into_specific(reg);
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

    derived()->reset_register_file();

    assembler.start_func(func_syms[func_idx]);

    for (const IRValueRef alloca : adaptor->cur_static_allocas()) {
        // TODO(ts): add a flag in the adaptor to not do this if it is
        // unnecessary?
        const auto local_idx = adaptor->val_local_idx(alloca);
        if (const auto &info = analyzer.liveness_info(local_idx);
            info.ref_count == 1) {
            // the value is ref-counted and not used, so skip it
            continue;
        }

        auto *assignment = allocate_assignment(1, true);
        auto  slot       = allocate_stack_slot(Derived::PLATFORM_POINTER_SIZE);

        assignment->initialize(slot,
                               Derived::PLATFORM_POINTER_SIZE,
                               analyzer.liveness_info(local_idx).ref_count,
                               Derived::PLATFORM_POINTER_SIZE);
        assignments.value_ptrs[local_idx] = assignment;

        // TODO(ts): initialize the part
        assert(0);
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
    assembler.end_func();

    return true;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
bool CompilerBase<Adaptor, Derived, Config>::compile_block(
    const IRBlockRef block, const u32 block_idx) noexcept {
    cur_block_idx =
        static_cast<typename Analyzer<Adaptor>::BlockIndex>(block_idx);
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

#include "AssignmentPartRef.hpp"
#include "RegisterFile.hpp"
#include "ScratchReg.hpp"
#include "ValuePartRef.hpp"
