// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <format>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <tsl/hopscotch_map.h>

#include "base.hpp"
#include "tpde/util/SmallVector.hpp"
#include "tpde/util/misc.hpp"

namespace tpde_llvm {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

// very hacky
inline u32 &val_idx_for_inst(llvm::Instruction *inst) noexcept {
    return *reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(inst)
                                    + offsetof(llvm::Instruction, DebugMarker)
                                    - 4);
    // static_assert(sizeof(llvm::Instruction) == 64);
}

inline u32 val_idx_for_inst(const llvm::Instruction *inst) noexcept {
    return *reinterpret_cast<const u32 *>(
        reinterpret_cast<const u8 *>(inst)
        + offsetof(llvm::Instruction, DebugMarker) - 4);
    // static_assert(sizeof(llvm::Instruction) == 64);
}

inline u32 &block_embedded_idx(llvm::BasicBlock *block) noexcept {
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
    /// Map from global value to value index. Globals are the lowest values.
    /// Keep them separate so that we don't have to repeatedly insert them for
    /// every function.
    tsl::hopscotch_map<const llvm::GlobalValue *, u32> global_lookup;
    /// Map from non-global value to value index. This map contains constants
    /// that are not globals, arguments, and basic blocks. Instructions are
    /// only included in debug builds.
    tsl::hopscotch_map<const llvm::Value *, u32>  value_lookup;
    tsl::hopscotch_map<llvm::BasicBlock *, u32>   block_lookup;
    tpde::util::SmallVector<LLVMComplexPart, 32> complex_part_types;

    // helpers for faster lookup
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
        if (idx == INVALID_VALUE_REF) {
            return true;
        }
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
        return is_static_alloca(llvm::cast<llvm::AllocaInst>(inst));
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

        return PHIRef{
            .phi = llvm::cast<llvm::PHINode>(values[idx].val),
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
    [[nodiscard]] u32 val_alloca_size(const IRValueRef idx) const noexcept {
        const auto *alloca = llvm::cast<llvm::AllocaInst>(values[idx].val);
        assert(alloca->isStaticAlloca());
        const u64 size = *alloca->getAllocationSize(mod.getDataLayout());
        assert(size <= std::numeric_limits<u32>::max());
        return size;
    }

    [[nodiscard]] u32 val_alloca_align(const IRValueRef idx) const noexcept {
        const auto *alloca = llvm::cast<llvm::AllocaInst>(values[idx].val);
        assert(alloca->isStaticAlloca());
        const u64 align = alloca->getAlign().value();
        assert(align <= 16);
        return align;
    }

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

    static void start_compile() noexcept {}

    static void end_compile() noexcept {}

  private:
    /// Handle instruction during switch_func.
    /// retval = restart from instruction, or nullptr to continue
    llvm::Instruction *handle_inst_in_block(llvm::BasicBlock *block,
                                            llvm::Instruction *inst);

  public:
    void switch_func(const IRFuncRef function) noexcept;

    void reset() noexcept;

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
        if (auto *gv = llvm::dyn_cast<llvm::GlobalValue>(val)) {
            if (auto it = global_lookup.find(gv); it != global_lookup.end()) {
                return it->second;
            }
        }
        const auto it = value_lookup.find(val);
        if (it != value_lookup.end()) {
            return it->second;
        }
        // Ignore inlineasm, which can only occur in calls, and metadata, which
        // can only occur in intrinsics.
        if (llvm::isa<llvm::InlineAsm, llvm::MetadataAsValue>(val)) {
            return INVALID_VALUE_REF;
        }
        llvm::errs() << "unhandled value: " << *val << "\n";
        assert(0);
        return INVALID_VALUE_REF;
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

    /// Append basic types of specified type to complex_part_types. Returns the
    /// allocation size in bytes and the alignment.
    std::pair<unsigned, unsigned>
        complex_types_append(llvm::Type *type) noexcept;

    [[nodiscard]] std::pair<LLVMBasicValType, u32>
        val_basic_type_uncached(const llvm::Value *val,
                                const bool const_or_global) noexcept;

  public:
    /// Map insertvalue/extractvalue indices to parts. Returns (first part,
    /// last part (inclusive)).
    std::pair<unsigned, unsigned> complex_part_for_index(IRValueRef val_idx,
                                                         unsigned index);
};

} // namespace tpde_llvm
