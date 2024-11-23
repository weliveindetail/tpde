// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <format>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TimeProfiler.h>

#include <tsl/hopscotch_map.h>

#include "base.hpp"
#include "tpde/util/SmallVector.hpp"
#include "tpde/util/misc.hpp"

namespace tpde_llvm {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

// very hacky
static u32 &val_idx_for_inst(llvm::Instruction *inst) noexcept {
    return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(inst)
                                    + offsetof(llvm::Instruction, DebugMarker)
                                    - 4);
    // static_assert(sizeof(llvm::Instruction) == 64);
}

static u32 val_idx_for_inst(const llvm::Instruction *inst) noexcept {
    return *reinterpret_cast<const u32 *>(
        reinterpret_cast<const u8 *>(inst)
        + offsetof(llvm::Instruction, DebugMarker) - 4);
    // static_assert(sizeof(llvm::Instruction) == 64);
}

static u32 &block_embedded_idx(llvm::BasicBlock *block) noexcept {
    return *reinterpret_cast<u32 *>(
        reinterpret_cast<u8 *>(block)
        + offsetof(llvm::BasicBlock, IsNewDbgInfoFormat) + 4);
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
    llvm::LLVMContext &context;
    llvm::Module      &mod;

    struct ValInfo {
        llvm::Value     *val;
        LLVMBasicValType type;
        bool             fused;
        bool             argument;
        u32              complex_part_tys_idx;
    };

    struct BlockAux {
        u32 aux1;
        u32 aux2;
        u32 idx_start;
        u32 idx_end;
        u32 idx_phi_end;
        u32 sibling;
    };

    struct BlockInfo {
        llvm::BasicBlock *block;
        BlockAux          aux;
    };

    tpde::util::SmallVector<ValInfo, 128>         values;
    tsl::hopscotch_map<const llvm::Value *, u32>  value_lookup;
    tsl::hopscotch_map<llvm::BasicBlock *, u32>   block_lookup;
    tpde::util::SmallVector<LLVMComplexPart, 32> complex_part_types;

    // helpers for faster lookup
    tpde::util::SmallVector<u32, 16> funcs_as_operands;
    tpde::util::SmallVector<u32, 8>  func_arg_indices;
    tpde::util::SmallVector<u32, 16> initial_stack_slot_indices;

    llvm::Function *cur_func                          = nullptr;
    bool            globals_init                      = false;
    bool            func_has_dynamic_alloca           = false;
    u32             global_idx_end                    = 0;
    u32             global_complex_part_types_end_idx = 0;

    tpde::util::SmallVector<llvm::Constant *, 32>     block_constants;
    tpde::util::SmallVector<BlockInfo, 128>           blocks;
    tpde::util::SmallVector<u32, 256>                 block_succ_indices;
    tpde::util::SmallVector<std::pair<u32, u32>, 128> block_succ_ranges;

    LLVMAdaptor(llvm::LLVMContext &ctx, llvm::Module &mod)
        : context(ctx), mod(mod) {}

    using IRValueRef = u32;
    using IRBlockRef = u32;
    using IRFuncRef  = llvm::Function *;

    static constexpr IRValueRef INVALID_VALUE_REF =
        static_cast<IRValueRef>(~0u);
    static constexpr IRBlockRef INVALID_BLOCK_REF =
        static_cast<IRBlockRef>(~0u);
    static constexpr IRFuncRef INVALID_FUNC_REF =
        nullptr; // NOLINT(*-misplaced-const)

    static constexpr bool TPDE_PROVIDES_HIGHEST_VAL_IDX = true;
    static constexpr bool TPDE_LIVENESS_VISIT_ARGS      = true;

    [[nodiscard]] u32 func_count() const noexcept {
        return mod.getFunctionList().size();
    }

    [[nodiscard]] auto funcs() const noexcept {
        struct FuncIter {
            struct Iter {
                llvm::Module::iterator it;

                Iter &operator++() {
                    ++it;
                    return *this;
                }

                bool operator!=(const Iter &rhs) const noexcept {
                    return it != rhs.it;
                }

                llvm::Function *operator*() const { return &*it; }
            };

            Iter first, last;

            explicit FuncIter(llvm::Module *mod)
                : first(mod->begin()), last(mod->end()) {}

            [[nodiscard]] Iter begin() const noexcept { return first; }

            [[nodiscard]] Iter end() const noexcept { return last; }
        };

        return FuncIter{&mod};
    }

    [[nodiscard]] auto funcs_to_compile() const noexcept { return funcs(); }

    [[nodiscard]] auto globals() const noexcept {
        struct GlobalIter {
            IRValueRef end_idx;

            struct iterator {
                IRValueRef it;

                iterator &operator++() noexcept {
                    ++it;
                    return *this;
                }

                IRValueRef operator*() const noexcept { return it; }

                bool operator!=(const iterator &rhs) const noexcept {
                    return it != rhs.it;
                }
            };

            [[nodiscard]] static iterator begin() noexcept {
                return iterator{.it = 0};
            }

            [[nodiscard]] iterator end() const noexcept {
                return iterator{.it = end_idx};
            }
        };

        return GlobalIter{.end_idx = global_idx_end};
    }

    [[nodiscard]] static std::string_view
        func_link_name(const IRFuncRef func) noexcept {
        return func->getName();
    }

    [[nodiscard]] static bool func_extern(const IRFuncRef func) noexcept {
        return func->isDeclaration();
    }

    [[nodiscard]] static bool func_only_local(const IRFuncRef func) noexcept {
        auto link = func->getLinkage();
        return (link == llvm::GlobalValue::InternalLinkage)
               || (link == llvm::GlobalValue::PrivateLinkage);
    }

    [[nodiscard]] static bool
        func_has_weak_linkage(const IRFuncRef func) noexcept {
        return func->getLinkage() == llvm::GlobalValue::LinkOnceODRLinkage;
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

        auto *fn = cur_func->getPersonalityFn();
        assert(llvm::dyn_cast<llvm::Function>(fn));
        return llvm::dyn_cast<llvm::Function>(fn);
    }

    [[nodiscard]] const auto &cur_args() const noexcept {
        return func_arg_indices;
    }

    [[nodiscard]] const auto &cur_static_allocas() const noexcept {
        return initial_stack_slot_indices;
    }

    [[nodiscard]] bool cur_has_dynamic_alloca() const noexcept {
        return func_has_dynamic_alloca;
    }

    [[nodiscard]] static IRBlockRef cur_entry_block() noexcept { return 0; }

    [[nodiscard]] IRBlockRef
        block_sibling(const IRBlockRef block) const noexcept {
        return blocks[block].aux.sibling;
    }

    [[nodiscard]] auto block_succs(const IRBlockRef block) const noexcept {
        struct BlockRange {
            const IRBlockRef *block_start, *block_end;

            [[nodiscard]] const IRBlockRef *begin() const noexcept {
                return block_start;
            }

            [[nodiscard]] const IRBlockRef *end() const noexcept {
                return block_end;
            }
        };

        auto &[start, end] = block_succ_ranges[block];
        return BlockRange{block_succ_indices.data() + start,
                          block_succ_indices.data() + end};
    }

    struct BlockValIter {
        IRValueRef start_idx, end_idx;

        struct Iterator {
            IRValueRef it;

            Iterator &operator++() noexcept {
                ++it;
                return *this;
            }

            [[nodiscard]] IRValueRef operator*() const noexcept { return it; }

            bool operator!=(const Iterator &rhs) const noexcept {
                return it != rhs.it;
            }
        };

        [[nodiscard]] Iterator begin() const noexcept {
            return Iterator{.it = start_idx};
        }

        [[nodiscard]] Iterator end() const noexcept {
            return Iterator{.it = end_idx};
        }
    };

    using IRInstIter = BlockValIter::Iterator;

    [[nodiscard]] auto block_values(const IRBlockRef block) const noexcept {
        const auto &aux = blocks[block].aux;
        return BlockValIter{.start_idx = aux.idx_start, .end_idx = aux.idx_end};
    }

    [[nodiscard]] auto block_phis(const IRBlockRef block) const noexcept {
        const auto &aux = blocks[block].aux;
        return BlockValIter{.start_idx = aux.idx_start,
                            .end_idx   = aux.idx_phi_end};
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
        std::string              buf;
        llvm::raw_string_ostream os{buf};
        blocks[block].block->printAsOperand(os);
        return buf;
    }

    [[nodiscard]] std::string
        value_fmt_ref(const IRValueRef value) const noexcept {
        std::string              buf;
        llvm::raw_string_ostream os{buf};
        values[value].val->print(os);
        return buf;
    }


    [[nodiscard]] static u32 val_local_idx(const IRValueRef value) noexcept {
        return value;
    }

    [[nodiscard]] auto val_operands(const IRValueRef idx) const noexcept {
        llvm::Value       *val  = values[idx].val;
        llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(val);
        assert(inst);

        struct OpIter {
            u32                idx;
            llvm::Instruction *inst;
            const LLVMAdaptor *self;

            [[nodiscard]] OpIter begin() noexcept { return *this; }

            [[nodiscard]] OpIter end() const noexcept {
                return OpIter{
                    .idx  = inst->getNumOperands(),
                    .inst = this->inst,
                    .self = this->self,
                };
            }

            OpIter &operator++() noexcept {
                ++idx;
                return *this;
            }

            [[nodiscard]] u32 operator*() const noexcept {
                return self->val_lookup_idx(inst->getOperand(idx));
            }

            bool operator!=(const OpIter &rhs) const {
                return (idx != rhs.idx) || (inst != rhs.inst)
                       || (self != rhs.self);
            }
        };

        return OpIter{
            .idx  = 0,
            .inst = inst,
            .self = this,
        };
    }

    [[nodiscard]] bool
        val_ignore_in_liveness_analysis(const IRValueRef idx) const noexcept {
        const auto *inst = llvm::dyn_cast<llvm::Instruction>(values[idx].val);
        if (!inst) {
            if (llvm::isa<llvm::Argument>(values[idx].val)) {
                return false;
            }
            // should handle constants and globals
            return true;
        }
        if (inst->getOpcode() != llvm::Instruction::Alloca) {
            return false;
        }
        return llvm::cast<llvm::AllocaInst>(inst)->isStaticAlloca();
    }

    [[nodiscard]] bool
        val_produces_result(const IRValueRef idx) const noexcept {
        const auto *inst = llvm::dyn_cast<llvm::Instruction>(values[idx].val);
        if (!inst) {
            return true;
        }

        return !inst->getType()->isVoidTy();
    }

    [[nodiscard]] bool val_is_arg(const IRValueRef idx) const noexcept {
        return values[idx].argument;
    }

    [[nodiscard]] bool val_is_phi(const IRValueRef idx) const noexcept {
        return llvm::isa<llvm::PHINode>(values[idx].val);
    }

    [[nodiscard]] auto val_as_phi(const IRValueRef idx) const noexcept {
        struct PHIRef {
            const llvm::PHINode *phi;
            const LLVMAdaptor   *self;

            [[nodiscard]] u32 incoming_count() const noexcept {
                return phi->getNumIncomingValues();
            }

            [[nodiscard]] IRValueRef
                incoming_val_for_slot(const u32 slot) const noexcept {
                auto *val = phi->getIncomingValue(slot);
                return self->val_lookup_idx(val);
            }

            [[nodiscard]] IRBlockRef
                incoming_block_for_slot(const u32 slot) const noexcept {
                const auto it =
                    self->block_lookup.find(phi->getIncomingBlock(slot));
                assert(it != self->block_lookup.end());
                return it->second;
            }

            [[nodiscard]] IRValueRef
                incoming_val_for_block(const IRBlockRef block) const noexcept {
                auto *val =
                    phi->getIncomingValueForBlock(self->blocks[block].block);
                return self->val_lookup_idx(val);
            }
        };

        const auto *phi = llvm::dyn_cast<llvm::PHINode>(values[idx].val);
        assert(phi);
        return PHIRef{
            .phi  = phi,
            .self = this,
        };
    }

    [[nodiscard]] u32 val_alloca_size(const IRValueRef idx) const noexcept {
        const auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(values[idx].val);
        assert(alloca);
        assert(alloca->isStaticAlloca());
        const u64 size = *alloca->getAllocationSize(mod.getDataLayout());
        assert(size <= std::numeric_limits<u32>::max());
        return size;
    }

    [[nodiscard]] u32 val_alloca_align(const IRValueRef idx) const noexcept {
        const auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(values[idx].val);
        assert(alloca);
        assert(alloca->isStaticAlloca());
        const u64 align = alloca->getAlign().value();
        assert(align <= std::numeric_limits<u64>::max());
        return align;
    }


#if 0
    std::string_view funcName(IRFuncRef func) { return func->getName(); }
#endif
    bool cur_arg_is_byval(const u32 idx) const noexcept {
        return cur_func->hasParamAttribute(idx,
                                           llvm::Attribute::AttrKind::ByVal);
    }

    u32 cur_arg_byval_align(const u32 idx) const noexcept {
        return cur_func->getParamAlign(idx)->value();
    }

    u32 cur_arg_byval_size(const u32 idx) const noexcept {
        return mod.getDataLayout().getTypeAllocSize(
            cur_func->getParamByValType(idx));
    }

    bool cur_arg_is_sret(const u32 idx) const noexcept {
        return cur_func->hasParamAttribute(
            idx, llvm::Attribute::AttrKind::StructRet);
    }
#if 0
    std::string valNameDbg(IRValue idx) {
        std::string out;
        auto        stream = llvm::raw_string_ostream(out);
        values[idx].val->print(stream);
        return out;
    }
#endif

    static void start_compile() noexcept {}

    static void end_compile() noexcept {}

    void switch_func(const IRFuncRef function) noexcept {
        cur_func = function;

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
        funcs_as_operands.clear();
        func_arg_indices.clear();
        initial_stack_slot_indices.clear();
        func_has_dynamic_alloca = false;

        // we keep globals around for all function compilation
        // and assign their value indices at the start of the compilation
        // TODO(ts): move this to start_compile?
        if (!globals_init) {
            globals_init = true;
            // reserve a constant size and optimize for smaller functions
            value_lookup.reserve(512);
            for (auto it = mod.global_begin(); it != mod.global_end(); ++it) {
                llvm::GlobalVariable *gv = &*it;
                assert(value_lookup.find(gv) == value_lookup.end());
                value_lookup.insert_or_assign(gv, values.size());
                const auto [ty, complex_part_idx] =
                    val_basic_type_uncached(gv, true);
                values.push_back(
                    ValInfo{.val      = static_cast<llvm::Value *>(gv),
                            .type     = ty,
                            .fused    = false,
                            .argument = false,
                            .complex_part_tys_idx = complex_part_idx});
            }

            for (auto it = mod.alias_begin(); it != mod.alias_end(); ++it) {
                llvm::GlobalAlias *ga = &*it;
                assert(value_lookup.find(ga) == value_lookup.end());
                value_lookup.insert_or_assign(ga, values.size());
                const auto [ty, complex_part_idx] =
                    val_basic_type_uncached(ga, true);
                values.push_back(
                    ValInfo{.val      = static_cast<llvm::Value *>(ga),
                            .type     = ty,
                            .fused    = false,
                            .argument = false,
                            .complex_part_tys_idx = complex_part_idx});
            }
            global_idx_end                    = values.size();
            global_complex_part_types_end_idx = complex_part_types.size();
        } else {
            values.resize(global_idx_end);
            complex_part_types.resize(global_complex_part_types_end_idx);
            // reserve a constant size and optimize for smaller functions
            value_lookup.reserve(512 + global_idx_end);
            for (u32 v = 0; v < global_idx_end; ++v) {
                value_lookup[values[v].val] = v;
            }
        }

        // add 20% overhead for constants and values that get duplicated
        // value_lookup.reserve(
        //   ((function->getInstructionCount() + function->arg_size()) * 6) /
        //   5);

        block_lookup.reserve(128);

        const size_t arg_count = function->arg_size();
        for (size_t i = 0; i < arg_count; ++i) {
            llvm::Argument *arg = function->getArg(i);
            value_lookup.insert_or_assign(arg, values.size());
            func_arg_indices.push_back(values.size());
            const auto [ty, complex_part_idx] =
                val_basic_type_uncached(arg, false);
            values.push_back(ValInfo{.val   = static_cast<llvm::Value *>(arg),
                                     .type  = ty,
                                     .fused = false,
                                     .argument             = true,
                                     .complex_part_tys_idx = complex_part_idx});
        }

        bool is_entry_block = true;
        for (llvm::BasicBlock &block : *function) {
            const u32 idx_start         = values.size();
            u32       phi_end           = idx_start;
            bool      found_phi_end     = false;
            bool      found_phi_end_bak = false;

            for (auto it = block.begin(); it != block.end();) {
                llvm::Instruction *inst = &*it;

                if (handle_inst_in_block(
                        &block, inst, it, is_entry_block, found_phi_end)) {
                    ++it;
                }

                if (found_phi_end != found_phi_end_bak) {
                    found_phi_end_bak = found_phi_end;
                    phi_end           = std::max(idx_start,
                                       static_cast<u32>(values.size() - 1));
                }
            }

            u32 idx_end = values.size();

            const auto block_idx = blocks.size();
            if (!blocks.empty()) {
                blocks.back().aux.sibling = block_idx;
            }

            blocks.push_back(BlockInfo{
                .block = &block,
                .aux   = BlockAux{.idx_start   = idx_start,
                                  .idx_end     = idx_end,
                                  .idx_phi_end = phi_end,
                                  .sibling     = INVALID_BLOCK_REF}
            });

            block_lookup[&block]       = block_idx;
            block_embedded_idx(&block) = block_idx;

            // blocks are also used as operands for branches so they need an
            // IRValueRef, too
            value_lookup.insert_or_assign(&block, values.size());
            values.push_back(ValInfo{.val  = static_cast<llvm::Value *>(&block),
                                     .type = LLVMBasicValType::invalid,
                                     .fused    = false,
                                     .argument = false});

            for (auto *C : block_constants) {
                if (value_lookup.find(C) == value_lookup.end()) {
                    value_lookup.insert_or_assign(C, values.size());
                    const auto [ty, complex_part_idx] =
                        val_basic_type_uncached(C, true);
                    values.push_back(
                        ValInfo{.val      = static_cast<llvm::Value *>(C),
                                .type     = ty,
                                .fused    = false,
                                .argument = false,
                                .complex_part_tys_idx = complex_part_idx});

                    if (auto *F = llvm::dyn_cast<llvm::Function>(C);
                        F && !F->isIntrinsic()) {
                        // globalIndices.push_back(values.size() - 1);
#if 1
                        funcs_as_operands.push_back(values.size() - 1);
#endif
                    }
                }
            }
            block_constants.clear();

            is_entry_block = false;
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
    }

    void reset() noexcept {
        values.clear();
        value_lookup.clear();
        block_lookup.clear();
        complex_part_types.clear();
        funcs_as_operands.clear();
        func_arg_indices.clear();
        initial_stack_slot_indices.clear();
        cur_func                          = nullptr;
        globals_init                      = false;
        global_idx_end                    = 0;
        global_complex_part_types_end_idx = 0;
        block_constants.clear();
        blocks.clear();
        block_succ_indices.clear();
        block_succ_ranges.clear();
    }

    // other public stuff for the compiler impl
    [[nodiscard]] LLVMBasicValType val_part_ty(const IRValueRef idx,
                                               u32 part_idx) const noexcept {
        auto ty = values[idx].type;
        if (ty == LLVMBasicValType::complex) {
            unsigned ty_idx = values[idx].complex_part_tys_idx;
            ty = complex_part_types[ty_idx + 1 + part_idx].part.type;
        }
        assert(ty != LLVMBasicValType::complex);
        return ty;
    }

    u32 val_part_count(const IRValueRef idx) const noexcept {
        if (values[idx].type != LLVMBasicValType::complex) {
            return basic_ty_part_count(values[idx].type);
        }
        return complex_part_types[values[idx].complex_part_tys_idx].num_parts;
    }

    u32 val_part_size(IRValueRef idx, u32 part_idx) const noexcept {
        return basic_ty_part_size(val_part_ty(idx, part_idx));
    }

    [[nodiscard]] bool val_fused(const IRValueRef idx) const noexcept {
        return values[idx].fused;
    }

    void val_set_fused(const IRValueRef idx, const bool fused) noexcept {
        values[idx].fused = fused;
    }

    [[nodiscard]] u32 val_lookup_idx(const llvm::Value *val) const noexcept {
        if (auto *inst = llvm::dyn_cast<llvm::Instruction>(val); inst) {
            const auto idx = inst_lookup_idx(inst);
            return idx;
        }
        const auto it = value_lookup.find(val);
        if (it == value_lookup.end()) {
            llvm::errs() << "unhandled value: " << *val << "\n";
        }
        assert(it != value_lookup.end());
        return it->second;
    }

    [[nodiscard]] u32
        inst_lookup_idx(const llvm::Instruction *inst) const noexcept {
        const auto idx = val_idx_for_inst(inst);
#ifndef NDEBUG
        assert(value_lookup.find(inst) != value_lookup.end()
               && value_lookup.find(inst)->second == idx);
#endif
        return idx;
    }

    // internal helpers
  private:
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
        case none: {
            assert(0);
            __builtin_unreachable();
        }
        }
    }

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
        case none: {
            assert(0);
            __builtin_unreachable();
        }
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
        case invalid: {
            TPDE_LOG_ERR("basic_ty_part_count for value with invalid type");
            assert(0);
            exit(1);
        }
        }
    }

    [[nodiscard]] static LLVMBasicValType
        llvm_ty_to_basic_ty_simple(const llvm::Type        *type,
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
            TPDE_LOG_ERR("Encountered unsupported integer bit width {}",
                         bit_width);
            assert(0);
            exit(1);
        }
        case llvm::Type::PointerTyID: return LLVMBasicValType::ptr;
        case llvm::Type::FixedVectorTyID: {
            const auto bit_width =
                type->getPrimitiveSizeInBits().getFixedValue();
            switch (bit_width) {
            case 32: return LLVMBasicValType::v32;
            case 64: return LLVMBasicValType::v64;
            case 128: return LLVMBasicValType::v128;
            case 256: return LLVMBasicValType::v256;
            case 512: return LLVMBasicValType::v512;
            default:
                assert(0);
                TPDE_LOG_ERR("Encountered unsupported integer bit width {}",
                             bit_width);
                exit(1);
            }
        }
        default: return LLVMBasicValType::invalid;
        }
    }

    /// Append basic types of specified type to complex_part_types. Returns the
    /// allocation size in bytes and the alignment.
    std::pair<unsigned, unsigned>
        complex_types_append(llvm::Type *type) noexcept {
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
            for (auto *el : llvm::cast<llvm::StructType>(type)->elements()) {
                unsigned prev = complex_part_types.size() - 1;
                auto [el_size, el_align] = complex_types_append(el);
                assert(el_size % el_align == 0
                       && "size must be multiple of alignment");

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
        default:
            llvm::errs() << "Type: " << *type << "\n";
            llvm::errs() << std::format("\nEncountered unsupported TypeID {}\n",
                                        static_cast<uint8_t>(type_id));
            assert(0);
            exit(1);
        }
    }

    [[nodiscard]] std::pair<LLVMBasicValType, u32>
        val_basic_type_uncached(const llvm::Value *val,
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

  public:
    /// Map insertvalue/extractvalue indices to parts. Returns (first part,
    /// last part (inclusive)).
    std::pair<unsigned, unsigned> complex_part_for_index(IRValueRef val_idx,
                                                         unsigned index) {
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
        exit(1);
    }

  private:
    [[nodiscard]] static bool
        fixup_phi_constants(llvm::Instruction     *&ins_before,
                            const llvm::BasicBlock *cur_block,
                            llvm::BasicBlock       *target) noexcept {
        auto replaced = false;
        for (auto it = target->begin(), end = target->end(); it != end; ++it) {
            auto *phi = llvm::dyn_cast<llvm::PHINode>(it);
            if (!phi) {
                break;
            }

            const auto idx = phi->getBasicBlockIndex(cur_block);
            if (idx == -1) {
                continue;
            }

            auto *val = phi->getIncomingValue(idx);
            assert(val);
            auto *cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val);
            if (!cexpr) {
                continue;
            }

            auto *inst = cexpr->getAsInstruction();

            inst->insertBefore(ins_before);

            // TODO: so apparently, it happens that a block can show up multiple
            // time in the same phi-node need to clarify the semantics of that
            // (does that mean the value has to be the same??)
            phi->setIncomingValueForBlock(cur_block, inst);
            ins_before = inst;
            replaced   = true;
        }
        return replaced;
    }

    // retval = increment iterator or not
    bool handle_inst_in_block(llvm::BasicBlock           *block,
                              llvm::Instruction          *inst,
                              llvm::BasicBlock::iterator &it,
                              bool                        is_entry_block,
                              bool                       &found_phi_end) {
        // check if the function contains dynamic allocas
        if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(inst); alloca) {
            if (!alloca->isStaticAlloca()) {
                func_has_dynamic_alloca = true;
            }
        }

        // I don't want to handle constant expressions in a store value operand
        // so we split it up here
        if (auto *store = llvm::dyn_cast<llvm::StoreInst>(inst); store) {
            auto *val = store->getValueOperand();
            if (auto *cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val); cexpr)
                [[unlikely]] {
                auto *exprInst = cexpr->getAsInstruction();
                exprInst->insertBefore(inst);
                store->setOperand(0, exprInst);
                // TODO: is it faster to use getIterator or decrement the
                // iterator?
                it = exprInst->getIterator();
                return false;
            }
        }

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
                        if (llvm::dyn_cast<llvm::ConstantInt>(idx_val)
                            == nullptr) {
                            assert(split_indices_size < 9);
                            split_indices[split_indices_size++] = idx;
                        }
                    }
                }
            }

            if (split_indices_size != 0) {
                auto replaced_it = false;
                auto idx_vec     = tpde::util::SmallVector<llvm::Value *, 8>{};

                auto  begin     = gep->idx_begin();
                auto *ty        = gep->getSourceElementType();
                auto *ptr_val   = gep->getPointerOperand();
                auto  start_idx = 0;
                auto  nullC =
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0);
                auto inbounds = gep->isInBounds();
                for (auto split_idx = 0; split_idx < split_indices_size;
                     ++split_idx) {
                    idx_vec.clear();
                    auto len = static_cast<size_t>(split_indices[split_idx]
                                                   - start_idx);
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

                    ty        = newGEP->getResultElementType();
                    ptr_val   = newGEP;
                    start_idx = split_indices[split_idx];

                    if (!replaced_it) {
                        it          = newGEP->getIterator();
                        replaced_it = true;
                    }
                }

                // last doesn't need the zero constant
                {
                    idx_vec.clear();
                    auto len =
                        static_cast<size_t>(gep->getNumIndices() - start_idx);
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
                    inst->removeFromParent();
                    inst->deleteValue();

                    if (!replaced_it) {
                        it = new_gep->getIterator();
                    }
                }

                // we want to handle the GEP normally now
                return false;
            }
        }

        bool cont = false;
        if (inst->isTerminator()) {
            assert(llvm::isa<llvm::IndirectBrInst>(inst) == false);
            assert(llvm::isa<llvm::CatchSwitchInst>(inst) == false);
            assert(llvm::isa<llvm::CatchReturnInst>(inst) == false);
            assert(llvm::isa<llvm::CleanupReturnInst>(inst) == false);
            assert(llvm::isa<llvm::CallBrInst>(inst) == false);
            auto *ins_before = inst;
            if (block->begin().getNodePtr() != ins_before) {
                auto *prev_inst = ins_before->getPrevNonDebugInstruction();
                if (prev_inst
                    && (prev_inst->getOpcode() == llvm::Instruction::ICmp
                        || prev_inst->getOpcode() == llvm::Instruction::FCmp)) {
                    // make sure fusing can still happen
                    ins_before = prev_inst;
                }
            }

            if (auto *br = llvm::dyn_cast<llvm::BranchInst>(inst); br) {
                if (fixup_phi_constants(
                        ins_before, block, br->getSuccessor(0))) {
                    cont = true;
                }
                if (!br->isUnconditional()) {
                    if (fixup_phi_constants(
                            ins_before, block, br->getSuccessor(1))) {
                        cont = true;
                    }
                }
            } else if (auto *sw = llvm::dyn_cast<llvm::SwitchInst>(inst); sw) {
                auto num_succs = sw->getNumSuccessors();
                for (auto i = 0u; i < num_succs; ++i) {
                    if (fixup_phi_constants(
                            ins_before, block, sw->getSuccessor(i))) {
                        cont = true;
                    }
                }
            } else if (auto *invoke = llvm::dyn_cast<llvm::InvokeInst>(inst);
                       invoke) {
                if (fixup_phi_constants(
                        ins_before, block, invoke->getNormalDest())) {
                    cont = true;
                }
                if (fixup_phi_constants(
                        ins_before, block, invoke->getUnwindDest())) {
                    cont = true;
                }
            }

            if (cont) {
                it = ins_before->getIterator();
                return false;
            }
        }

        // check operands for constants
        // TODO: just do this for all constants/globals in the context/module
        // always?
        size_t idx = 0;
        for (llvm::Value *val : inst->operands()) {
            if (auto *C = llvm::dyn_cast<llvm::Constant>(val); C) {
                if (auto *cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val);
                    cexpr) {
                    if (inst->getOpcode() == llvm::Instruction::PHI) {
                        // we didn't see the incoming block yet so we fix the
                        // constant expression here
                        auto *phi            = llvm::cast<llvm::PHINode>(inst);
                        auto *incoming_block = phi->getIncomingBlock(idx);
                        auto *ins_before     = incoming_block->getTerminator();
                        if (incoming_block->begin().getNodePtr()
                            != ins_before) {
                            auto *prev_inst =
                                ins_before->getPrevNonDebugInstruction();
                            if (prev_inst
                                && (prev_inst->getOpcode()
                                        == llvm::Instruction::ICmp
                                    || prev_inst->getOpcode()
                                           == llvm::Instruction::FCmp)) {
                                // make sure fusing can still happen
                                ins_before = prev_inst;
                            }
                        }

                        auto *expr_inst = cexpr->getAsInstruction();
                        expr_inst->insertBefore(ins_before);
                        phi->setIncomingValueForBlock(incoming_block,
                                                      expr_inst);
                        continue;
                    }

                    // clang creates these and we don't want to handle them so
                    // we split them up into their own values
                    auto *expr_inst = cexpr->getAsInstruction();
                    expr_inst->insertBefore(inst);
                    inst->setOperand(idx, expr_inst);
                    // TODO: do we need to clean up the ConstantExpr?
                    it   = expr_inst->getIterator();
                    cont = true;
                    break;
                }


                // we insert constants inbetween block so that instructions in a
                // block are consecutive
                if (!value_lookup.contains(C)) {
                    block_constants.push_back(C);
                }
            }
            ++idx;
        }

        if (cont) {
            return false;
        }

        auto fused = false;
        if (is_entry_block) {
            if (inst->getOpcode() == llvm::Instruction::Alloca) {
                const auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(inst);
                if (alloca->isStaticAlloca()) {
                    initial_stack_slot_indices.push_back(values.size());
                    // fuse static alloca's in the initial block so we dont try
                    // to dynamically allocate them
                    fused = true;
                }
            }
        }

        if (!found_phi_end && !llvm::isa<llvm::PHINode>(inst)) {
            found_phi_end = true;
        }

        auto val_idx           = values.size();
        val_idx_for_inst(inst) = val_idx;

#ifndef NDEBUG
        value_lookup.insert_or_assign(inst, val_idx);
#endif
        auto [ty, complex_part_idx] = val_basic_type_uncached(inst, false);
        values.push_back(ValInfo{.val      = static_cast<llvm::Value *>(inst),
                                 .type     = ty,
                                 .fused    = fused,
                                 .argument = false,
                                 .complex_part_tys_idx = complex_part_idx});
        return true;
    }
};

} // namespace tpde_llvm
