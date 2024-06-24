// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <unordered_set>

#include "tpde/base.hpp"

namespace tpde::test {

struct TestIR {
    struct Value {
        enum class Type : u8 {
            normal,
            arg,
            alloca,
            phi,
        };

        enum class Op : u8 {
            none,
            add
        };
        inline static constexpr const char *OP_NAMES[] = {"none", "add"};

        std::string name;
        u32         local_idx;
        Type        type;
        Op          op;

        union {
            u32 alloca_size;
            u32 op_count;
        };

        u32 op_begin_idx, op_end_idx;
    };

    struct Block {
        std::string name;
        u32         succ_begin_idx, succ_end_idx;
        u32         inst_begin_idx, inst_end_idx;
        bool        has_sibling = false;
        u32         block_info, block_info2;
    };

    struct Function {
        std::string name;
        u32         block_begin_idx, block_end_idx;
        u32         arg_begin_idx, arg_end_idx;
    };

    std::vector<Value>    values;
    std::vector<u32>      value_operands;
    std::vector<Block>    blocks;
    std::vector<u32>      block_succs;
    std::vector<Function> functions;

    struct PendingBlockEntry {
        enum class Type : u8 {
            succ,
            value,
        };
        Type type;

        union {
            u32 block_idx;
            u32 value_idx;
        };

        union {
            u32 succ_idx;
            u32 op_idx;
        };
    };

    struct BodyParseState {
        u32                                       func_idx;
        u32                                       cur_block;
        bool                                      block_finished;
        bool                                      block_finished_phis;
        std::unordered_map<std::string_view, u32> value_map;
        std::unordered_map<std::string_view, u32> block_map;
        std::unordered_map<std::string_view, std::vector<std::pair<u32, u32>>>
            pending_value_resolves;
        std::unordered_map<std::string_view, std::vector<PendingBlockEntry>>
            pending_block_resolves;
    };

    [[nodiscard]] bool parse_ir(std::string_view text) noexcept;
    [[nodiscard]] bool
                       parse_func(std::string_view                     &text,
                                  std::unordered_set<std::string_view> &func_names) noexcept;
    [[nodiscard]] bool parse_func_body(std::string_view body,
                                       BodyParseState  &parse_state) noexcept;
    [[nodiscard]] bool parse_body_line(std::string_view line,
                                       BodyParseState  &parse_state) noexcept;
    [[nodiscard]] bool parse_block(std::string_view line,
                                   BodyParseState  &parse_state,
                                   bool             is_entry) noexcept;

    static void remove_whitespace(std::string_view &text) noexcept;
    [[nodiscard]] static std::string_view
        get_line(std::string_view text) noexcept;
    [[nodiscard]] static std::string_view
        parse_name(std::string_view &text) noexcept;

    void dump_debug() const noexcept;
    void print() const noexcept;
};

struct TestIRAdaptor {
    TestIR *ir;

    explicit TestIRAdaptor(TestIR *ir) : ir(ir) {}

    enum class IRValueRef : u32 {
    };
    enum class IRBlockRef : u32 {
    };
    enum class IRFuncRef : u32 {
    };

    static constexpr IRValueRef INVALID_VALUE_REF =
        static_cast<IRValueRef>(~0u);
    static constexpr IRBlockRef INVALID_BLOCK_REF =
        static_cast<IRBlockRef>(~0u);
    static constexpr IRFuncRef INVALID_FUNC_REF = static_cast<IRFuncRef>(~0u);

    static constexpr bool TPDE_PROVIDES_HIGHEST_VAL_IDX = true;
    u32                   highest_local_val_idx;

    static constexpr bool TPDE_LIVENESS_VISIT_ARGS = true;

    [[nodiscard]] u32 func_count() const noexcept {
        return static_cast<u32>(ir->functions.size());
    }

    auto funcs() const noexcept {
        struct Range {
            struct Iter {
                u32 val;

                Iter &operator++() {
                    ++val;
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    return other.val != val;
                }

                IRFuncRef operator*() const noexcept {
                    return static_cast<IRFuncRef>(val);
                }
            };

            u32 func_count;

            [[nodiscard]] Iter begin() const noexcept { return Iter{0}; }

            [[nodiscard]] Iter end() const noexcept { return Iter{func_count}; }
        };

        return Range{.func_count = static_cast<u32>(ir->functions.size())};
    }

    [[nodiscard]] std::string_view
        func_link_name(const IRFuncRef func) const noexcept {
        return ir->functions[static_cast<u32>(func)].name;
    }

    [[nodiscard]] bool func_extern(const IRFuncRef) const noexcept {
        return false;
    }

    [[nodiscard]] bool func_only_local(const IRFuncRef) const noexcept {
        return true;
    }

    [[nodiscard]] bool func_has_weak_linkage(const IRFuncRef) const noexcept {
        return false;
    }

    u32 cur_func;

    [[nodiscard]] bool cur_needs_unwind_info() const noexcept { return false; }

    [[nodiscard]] bool cur_is_vararg() const noexcept { return false; }

    [[nodiscard]] u32 cur_highest_val_idx() const noexcept {
        return highest_local_val_idx;
    }

    [[nodiscard]] IRFuncRef cur_personality_func() const noexcept {
        return INVALID_FUNC_REF;
    }

    [[nodiscard]] auto cur_args() const noexcept {
        struct Range {
            const u32 beg, last;

            struct Iter {
                u32 val;

                Iter &operator++() {
                    ++val;
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    return other.val != val;
                }

                IRValueRef operator*() const noexcept {
                    return static_cast<IRValueRef>(val);
                }
            };

            [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

            [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
        };

        const auto &func = ir->functions[cur_func];
        return Range{.beg = func.arg_begin_idx, .last = func.arg_end_idx};
    }

    [[nodiscard]] auto cur_static_allocas() const noexcept {
        struct Range {
            const TestIR *self;
            const u32     val_idx_beg, val_idx_end;

            struct Iter {
                const TestIR *self;
                u32           val, val_idx_end;

                Iter &operator++() noexcept {
                    if (val == val_idx_end) {
                        return *this;
                    }

                    while (val != val_idx_end) {
                        ++val;

                        if (self->values[val].type
                            == TestIR::Value::Type::alloca) {
                            break;
                        }
                    }
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    assert(other.self == self);
                    assert(other.val_idx_end == val_idx_end);
                    return other.val != val;
                }

                IRValueRef operator*() const noexcept {
                    return static_cast<IRValueRef>(val);
                }
            };

            [[nodiscard]] Iter begin() const noexcept {
                return Iter{.self        = self,
                            .val         = val_idx_beg,
                            .val_idx_end = val_idx_end};
            }

            [[nodiscard]] Iter end() const noexcept {
                return Iter{.self        = self,
                            .val         = val_idx_end,
                            .val_idx_end = val_idx_end};
            }
        };

        const auto &entry = ir->blocks[ir->functions[cur_func].block_begin_idx];
        assert(ir->functions[cur_func].block_begin_idx
               != ir->functions[cur_func].block_end_idx);

        u32 first_alloca = entry.inst_begin_idx;
        while (first_alloca != entry.inst_end_idx) {
            if (ir->values[first_alloca].type == TestIR::Value::Type::alloca) {
                break;
            }
            ++first_alloca;
        }
        return Range{ir, first_alloca, entry.inst_end_idx};
    }

    [[nodiscard]] IRBlockRef cur_entry_block() const noexcept {
        const auto &func = ir->functions[cur_func];
        assert(ir->functions[cur_func].block_begin_idx
               != ir->functions[cur_func].block_end_idx);

        return static_cast<IRBlockRef>(func.block_begin_idx);
    }

    [[nodiscard]] IRBlockRef
        block_sibling(const IRBlockRef block) const noexcept {
        if (ir->blocks[static_cast<u32>(block)].has_sibling) {
            return static_cast<IRBlockRef>(static_cast<u32>(block) + 1);
        } else {
            return INVALID_BLOCK_REF;
        }
    }

    [[nodiscard]] auto block_succs(const IRBlockRef block) const noexcept {
        struct Range {
            const u32 *beg, *last;

            struct Iter {
                const u32 *val;

                Iter &operator++() {
                    ++val;
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    return other.val != val;
                }

                IRBlockRef operator*() const noexcept {
                    return static_cast<IRBlockRef>(*val);
                }
            };

            [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

            [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
        };

        const auto &info = ir->blocks[static_cast<u32>(block)];
        const auto *data = ir->block_succs.data();
        return Range{data + info.succ_begin_idx, data + info.succ_end_idx};
    }

    [[nodiscard]] auto block_values(const IRBlockRef block) const noexcept {
        struct Range {
            const u32 beg, last;

            struct Iter {
                u32 val;

                Iter &operator++() {
                    ++val;
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    return other.val != val;
                }

                IRValueRef operator*() const noexcept {
                    return static_cast<IRValueRef>(val);
                }
            };

            [[nodiscard]] Iter begin() const noexcept { return Iter{beg}; }

            [[nodiscard]] Iter end() const noexcept { return Iter{last}; }
        };

        const auto &info = ir->blocks[static_cast<u32>(block)];
        return Range{info.inst_begin_idx, info.inst_end_idx};
    }

    [[nodiscard]] auto block_phis(const IRBlockRef block) const noexcept {
        struct Range {
            const TestIR *self;
            const u32     val_idx_beg, val_idx_end;

            struct Iter {
                const TestIR *self;
                u32           val, val_idx_end;

                Iter &operator++() noexcept {
                    if (val == val_idx_end) {
                        return *this;
                    }

                    while (val != val_idx_end) {
                        ++val;

                        if (self->values[val].type
                            == TestIR::Value::Type::phi) {
                            break;
                        } else {
                            val = val_idx_end;
                            break;
                        }
                    }
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    assert(other.self == self);
                    assert(other.val_idx_end == val_idx_end);
                    return other.val == val;
                }

                IRValueRef operator*() const noexcept {
                    return static_cast<IRValueRef>(val);
                }
            };

            [[nodiscard]] Iter begin() const noexcept {
                return Iter{.self        = self,
                            .val         = val_idx_beg,
                            .val_idx_end = val_idx_end};
            }

            [[nodiscard]] Iter end() const noexcept {
                return Iter{.self        = self,
                            .val         = val_idx_end,
                            .val_idx_end = val_idx_end};
            }
        };

        const auto &info = ir->blocks[static_cast<u32>(block)];
        if (info.inst_begin_idx == info.inst_end_idx
            || ir->values[info.inst_begin_idx].type
                   != TestIR::Value::Type::phi) {
            return Range{ir, info.inst_end_idx, info.inst_end_idx};
        } else {
            return Range{ir, info.inst_begin_idx, info.inst_end_idx};
        }
    }

    [[nodiscard]] u32 block_info(IRBlockRef block) const noexcept {
        return ir->blocks[static_cast<u32>(block)].block_info;
    }

    void block_set_info(IRBlockRef block, u32 value) noexcept {
        ir->blocks[static_cast<u32>(block)].block_info = value;
    }

    [[nodiscard]] u32 block_info2(IRBlockRef block) const noexcept {
        return ir->blocks[static_cast<u32>(block)].block_info2;
    }

    void block_set_info2(IRBlockRef block, u32 value) noexcept {
        ir->blocks[static_cast<u32>(block)].block_info2 = value;
    }

    [[nodiscard]] std::string_view
        block_fmt_ref(IRBlockRef block) const noexcept {
        return ir->blocks[static_cast<u32>(block)].name;
    }

    [[nodiscard]] u32 val_local_idx(IRValueRef val) {
        assert(static_cast<u32>(val) >= ir->functions[cur_func].arg_begin_idx);
        return static_cast<u32>(val) - ir->functions[cur_func].arg_begin_idx;
    }

    [[nodiscard]] auto val_operands(IRValueRef val) {
        struct Range {
            bool       is_phi;
            const u32 *beg, *last;

            struct Iter {
                bool       is_phi;
                const u32 *val;

                Iter &operator++() {
                    if (is_phi) {
                        val += 2;
                    } else {
                        ++val;
                    }
                    return *this;
                }

                bool operator!=(const Iter &other) const noexcept {
                    return other.val != val;
                }

                IRValueRef operator*() const noexcept {
                    return static_cast<IRValueRef>(*val);
                }
            };

            [[nodiscard]] Iter begin() const noexcept {
                return Iter{is_phi, beg};
            }

            [[nodiscard]] Iter end() const noexcept {
                return Iter{is_phi, last};
            }
        };

        const auto &info = ir->values[static_cast<u32>(val)];
        const auto *data = ir->value_operands.data();
        if (info.type == TestIR::Value::Type::phi) {
            return Range(
                true, data + info.op_begin_idx + 1, data + info.op_end_idx + 1);
        } else if (info.type == TestIR::Value::Type::normal) {
            return Range(
                false, data + info.op_begin_idx, data + info.op_end_idx);
        } else {
            return Range{false, nullptr, nullptr};
        }
    }

    [[nodiscard]] bool
        val_ignore_in_liveness_analysis(IRValueRef value) const noexcept {
        return ir->values[static_cast<u32>(value)].type
               == TestIR::Value::Type::alloca;
    }

    [[nodiscard]] bool val_is_arg(IRValueRef value) const noexcept {
        return ir->values[static_cast<u32>(value)].type
               == TestIR::Value::Type::arg;
    }

    [[nodiscard]] bool val_is_phi(IRValueRef value) const noexcept {
        return ir->values[static_cast<u32>(value)].type
               == TestIR::Value::Type::phi;
    }

    [[nodiscard]] auto val_as_phi(IRValueRef value) const noexcept {
        struct PHIRef {
            const u32 *op_begin, *op_end;

            [[nodiscard]] u32 incoming_count() const noexcept {
                return (op_end - op_begin) / 2;
            }

            [[nodiscard]] IRValueRef
                incoming_val_for_slot(const u32 slot) const noexcept {
                assert(slot < incoming_count());
                return static_cast<IRValueRef>(*(op_begin + slot * 2 + 1));
            }

            [[nodiscard]] IRBlockRef
                incoming_block_for_slot(const u32 slot) const noexcept {
                assert(slot < incoming_count());
                return static_cast<IRBlockRef>(*(op_begin + slot * 2));
            }

            [[nodiscard]] IRValueRef incoming_val_for_block(
                const IRBlockRef block_ref) const noexcept {
                const auto block = static_cast<u32>(block_ref);
                for (auto *op = op_begin; op < op_end; op += 2) {
                    if (*op == block) {
                        return static_cast<IRValueRef>(*(op + 1));
                    }
                }

                return INVALID_VALUE_REF;
            }
        };

        const auto val_idx = static_cast<u32>(value);
        assert(ir->values[val_idx].type == TestIR::Value::Type::phi);
        const auto &info = ir->values[val_idx];
        const auto *data = ir->value_operands.data();
        return PHIRef{data + info.op_begin_idx, data + info.op_end_idx};
    }

    [[nodiscard]] u32 val_alloca_size(IRValueRef value) const noexcept {
        const auto val_idx = static_cast<u32>(value);
        assert(ir->values[val_idx].type == TestIR::Value::Type::alloca);
        return ir->values[val_idx].alloca_size;
    }

    void start_compile() const noexcept {}

    void end_compile() const noexcept {}

    void switch_func(IRFuncRef func) noexcept {
        cur_func = static_cast<u32>(func);

        highest_local_val_idx = 0;
        const auto &info      = ir->functions[cur_func];
        assert(info.block_begin_idx != info.block_end_idx);

        highest_local_val_idx = ir->blocks[info.block_end_idx - 1].inst_end_idx
                                - info.arg_begin_idx;
        if (highest_local_val_idx > 0) {
            --highest_local_val_idx;
        }
    }

    void reset() { highest_local_val_idx = 0; }
};

} // namespace tpde::test
