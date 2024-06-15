// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/base.hpp"

namespace tpde::test {

struct TestIR {
    struct Value {
        enum class Type : u8 {
            normal,
            arg,
            alloca,
            phi,
            jump,
            terminate,
        };
        std::string name;
        u32         local_idx;
        Type        type;

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
    };

    struct Function {
        std::string name;
        u32         entry_block;
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
        std::unordered_map<std::string_view, u32> value_map;
        std::unordered_map<std::string_view, u32> block_map;
        std::unordered_map<std::string_view, std::vector<std::pair<u32, u32>>>
            pending_value_resolves;
        std::unordered_map<std::string_view, std::vector<PendingBlockEntry>>
            pending_block_resolves;
    };

    [[nodiscard]] bool parse_ir(std::string_view text) noexcept;
    [[nodiscard]] bool parse_func(std::string_view &text) noexcept;
    [[nodiscard]] bool parse_func_body(std::string_view body,
                                       BodyParseState  &parse_state) noexcept;
    [[nodiscard]] bool parse_body_line(std::string_view line,
                                       BodyParseState  &parse_state) noexcept;
    [[nodiscard]] bool parse_block(std::string_view line,
                                   BodyParseState  &parse_state) noexcept;

    static void remove_whitespace(std::string_view &text) noexcept;
    static [[nodiscard]] std::string_view
        get_line(std::string_view text) noexcept;
    static [[nodiscard]] std::string_view
        parse_name(std::string_view &text) noexcept;

    void dump_debug() const noexcept;


    // Implementation of IRAdaptor
};

} // namespace tpde::test
