// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

// For testing of the analyzer components, we need a small IR that we can
// parse from text files

// Quick specification of the format:
// All identifiers are alphanumeric starting with a letter
// Comments are done using ; at the beginning of a line
//
// Function and block names don't have a prefix when they are defined, value
// names and func/block names when they are used are prefixed with %
//
// clang-format off
// <funcName>(%<valName>, %<valName>, ...) {
// <blockName>:
//     %<valName> = alloca <size>
// ; No operands
//     %<valName> =
// ; Force fixed assignment if there is space
//     %<valName>! =
//     %<valName> = [operation] %<valName>, %<valName>, ...
//     jump %<blockName>, %<blockName>, ...
//     <or>
//     br %<blockName>
//     <or>
//     condbr %<valName>, %<trueTarget>, %<falseTarget>
//
// <blockName>:
//     %<valName> = phi [%<blockName>, %<valName>], [%<blockName>, %<valName>], ...
//     terminate
// }
//
// <funcName>...

// clang-format on

#include <cctype>
#include <charconv>
#include <format>

#include "TestIR.hpp"

bool tpde::test::TestIR::parse_ir(std::string_view text) noexcept {
    TPDE_LOG_TRACE("Parsing IR");

    values.clear();
    value_operands.clear();
    blocks.clear();
    block_succs.clear();
    functions.clear();

    std::unordered_set<std::string_view> func_names;
    std::unordered_map<std::string_view, std::vector<u32>>
        pending_call_resolves;

    while (!text.empty()) {
        if (!parse_func(text, func_names, pending_call_resolves)) {
            TPDE_LOG_ERR("Failed to parse function");
            return false;
        }
    }


    TPDE_LOG_TRACE("Finished parsing IR");
    dump_debug();

    return true;
}

bool tpde::test::TestIR::parse_func(
    std::string_view                     &text,
    std::unordered_set<std::string_view> &func_names,
    std::unordered_map<std::string_view, std::vector<u32>>
        &pending_call_resolves) noexcept {
    TPDE_LOG_TRACE("Parsing function");
    remove_whitespace(text);

    if (text.empty()) {
        TPDE_LOG_TRACE("Found EOF");
        return true;
    }

    const auto name = parse_name(text);
    if (name.empty()) {
        TPDE_LOG_ERR("Failed to parse function name. At '{}'", get_line(text));
        return false;
    }

    if (func_names.contains(name)) {
        TPDE_LOG_ERR(
            "Failed to parse function. Function with name '{}' already exists",
            name);
        return false;
    }

    TPDE_LOG_TRACE("Got func with name '{}'", name);

    const auto func_idx = functions.size();
    functions.push_back(Function{.name = std::string(name)});
    func_names.insert(name);

    remove_whitespace(text);
    if (text.empty() || text[0] != '(') {
        TPDE_LOG_ERR("Failed to parse function. Expected open paren for args "
                     "but got '{}'",
                     get_line(text));
        return false;
    }

    text.remove_prefix(1);
    const auto arg_end_off = text.find(')');
    if (arg_end_off == std::string::npos) {
        TPDE_LOG_ERR("Failed to parse function. Expected closing paren but "
                     "found none at '{}'",
                     get_line(text));
        return false;
    }

    TPDE_LOG_TRACE("Parsing func args");
    auto           args = text.substr(0, arg_end_off);
    BodyParseState parse_state;
    parse_state.func_idx              = func_idx;
    parse_state.pending_call_resolves = &pending_call_resolves;

    functions[func_idx].arg_begin_idx = values.size();
    auto had_arg                      = false;
    while (!args.empty()) {
        remove_whitespace(args);
        if (args.empty()) {
            break;
        }

        if (args[0] == ',') {
            if (had_arg) {
                args.remove_prefix(1);
                remove_whitespace(args);
            } else {
                TPDE_LOG_ERR("Failed to parse argument for function '{}'. Got "
                             "unexpected ',' for first argument at '{}'",
                             name,
                             get_line(args));
                return false;
            }
        }
        had_arg = true;

        if (args.empty() || args[0] != '%') {
            TPDE_LOG_ERR("Failed to parse argument for function '{}'. Expected "
                         "'%', got '{}'",
                         name,
                         get_line(args));
            return false;
        }
        args.remove_prefix(1);

        const auto arg_name = parse_name(args);
        if (arg_name.empty()) {
            TPDE_LOG_ERR("Failed to parse argument for function '{}'. Expected "
                         "name but got '{}'",
                         name,
                         get_line(args));
            return false;
        }

        if (parse_state.value_map.contains(arg_name)) {
            TPDE_LOG_ERR("Failed to parse argument for function '{}'. Got "
                         "duplicated argument name '{}' at '{}'",
                         name,
                         arg_name,
                         get_line(args));
            return false;
        }

        const auto arg_idx = values.size();
        values.push_back(Value{
            .name     = std::string(arg_name),
            .type     = Value::Type::arg,
            .op_count = 0,
        });
        TPDE_LOG_TRACE(
            "Got argument with name '{}' and idx {}", arg_name, arg_idx);

        remove_whitespace(args);
        if (!args.empty() && args[0] == '!') {
            TPDE_LOG_TRACE("Force fixed assignment for value {}", arg_idx);
            values[arg_idx].force_fixed_assignment = true;
            args.remove_prefix(1);
        }

        parse_state.value_map[arg_name] = arg_idx;
    }

    TPDE_LOG_TRACE("Finished parsing func args");
    functions[func_idx].arg_end_idx = values.size();

    text.remove_prefix(arg_end_off + 1);

    remove_whitespace(text);

    if (pending_call_resolves.contains(name)) {
        for (auto &val_idx : pending_call_resolves[name]) {
            if (values[val_idx].op_count
                != (functions[func_idx].arg_end_idx
                    - functions[func_idx].arg_begin_idx)) {
                TPDE_LOG_ERR("Call '{}' to function {} has incorrect number of "
                             "arguments",
                             values[val_idx].name,
                             functions[func_idx].name);
                return false;
            }

            values[val_idx].call_func_idx = func_idx;
        }
        pending_call_resolves.erase(name);
    }

    if (!text.empty() && text.starts_with("local")) {
        functions[func_idx].local_only = true;
        text.remove_prefix(5);
        remove_whitespace(text);
        TPDE_LOG_TRACE("Function is local");
    } else if (!text.empty() && text.starts_with("!")) {
        functions[func_idx].declaration = true;
        text.remove_prefix(1);
        TPDE_LOG_TRACE("Function is extern");
        TPDE_LOG_TRACE("Finished parsing function '{}'", name);
        return true;
    }

    if (text.empty() || text[0] != '{') {
        TPDE_LOG_ERR("Failed to parse function '{}'. Expected opening brace "
                     "for body but got '{}'",
                     name,
                     get_line(text));
        return false;
    }

    text.remove_prefix(1);

    const auto body_len = text.find('}');
    if (body_len == std::string::npos) {
        TPDE_LOG_ERR("Failed to parse function '{}'. Expected closing brace "
                     "for body but that was not found.",
                     name);
        return false;
    }

    const auto body_text = text.substr(0, body_len);
    if (!parse_func_body(body_text, parse_state)) {
        TPDE_LOG_ERR("Failed to parse body of function '{}'", name);
        return false;
    }

    text.remove_prefix(body_len + 1);

    if (!parse_state.pending_block_resolves.empty()) {
        TPDE_LOG_ERR("Got unresolved block references for '{}'",
                     parse_state.pending_block_resolves.begin()->first);
        return false;
    }

    if (!parse_state.pending_value_resolves.empty()) {
        TPDE_LOG_TRACE("Got unresolved value references for '{}'",
                       parse_state.pending_value_resolves.begin()->first);
        return false;
    }

    TPDE_LOG_TRACE("Finished parsing function '{}'", name);
    // TODO(ts): validate domination?
    return true;
}

bool tpde::test::TestIR::parse_func_body(std::string_view body,
                                         BodyParseState &parse_state) noexcept {
    auto       first    = true;
    const auto func_idx = parse_state.func_idx;
    TPDE_LOG_TRACE("Parsing func body");

    while (!body.empty()) {
        remove_whitespace(body);
        auto line_len = body.find('\n');
        if (line_len == std::string::npos) {
            line_len = body.size();
        }
        auto line = body.substr(0, line_len);

        if (first) {
            TPDE_LOG_TRACE("Parsing line '{}'", line);
            // parse entry block
            if (!parse_block(line, parse_state, true)) {
                TPDE_LOG_ERR("Failed to parse entry block for function '{}'",
                             functions[func_idx].name);
                return false;
            }

            TPDE_LOG_TRACE("Got entry block '{}'",
                           blocks[parse_state.cur_block].name);
            functions[func_idx].block_begin_idx = parse_state.cur_block;
            first                               = false;
        } else {
            if (!parse_body_line(line, parse_state)) {
                TPDE_LOG_ERR("Failed to parse body line '{}'", line);
                return false;
            }
        }

        body.remove_prefix(line_len);
    }

    if (!parse_state.block_finished) {
        TPDE_LOG_ERR(
            "Block '{}' was not finished with a terminating instruction",
            blocks[parse_state.cur_block].name);
        return false;
    }

    functions[func_idx].block_end_idx = blocks.size();

    TPDE_LOG_TRACE("Finished parsing func body");

    return true;
}

bool tpde::test::TestIR::parse_body_line(std::string_view line,
                                         BodyParseState &parse_state) noexcept {
    if (line.empty()) {
        return true;
    }
    TPDE_LOG_TRACE("Parsing line '{}'", line);

    if (line[0] == '%') {
        return parse_value_line(line, parse_state);
    } else if (line.starts_with("jump") || line.starts_with("terminate")
               || line.starts_with("br") || line.starts_with("condbr")) {
        if (parse_state.block_finished) {
            TPDE_LOG_ERR("Trying to add instruction after terminating "
                         "instruction for block '{}'",
                         blocks[parse_state.cur_block].name);
            return false;
        }

        const auto block_idx             = parse_state.cur_block;
        blocks[block_idx].succ_begin_idx = block_succs.size();
        const auto block_name = std::string_view{blocks[block_idx].name};

        if (line.starts_with("jump")) {
            TPDE_LOG_TRACE("Got jump in block {}", block_idx);

            line.remove_prefix(4);
            const auto block_name = std::string_view{blocks[block_idx].name};

            auto had_arg    = false;
            u32  succ_count = 0;
            while (!line.empty()) {
                remove_whitespace(line);
                if (line.empty()) {
                    break;
                }

                if (line[0] == ',') {
                    if (had_arg) {
                        line.remove_prefix(1);
                        remove_whitespace(line);
                    } else {
                        TPDE_LOG_ERR("Failed to parse operand for jump in "
                                     "block '{}'. Got "
                                     "unexpected ',' for first operand at '{}'",
                                     block_name,
                                     line);
                        return false;
                    }
                }
                had_arg = true;

                if (line.empty() || line[0] != '%') {
                    TPDE_LOG_ERR(
                        "Failed to parse operand for jump in block '{}'. "
                        "Expected '%', got '{}'",
                        block_name,
                        line);
                    return false;
                }
                line.remove_prefix(1);

                const auto op_name = parse_name(line);
                if (op_name.empty()) {
                    TPDE_LOG_ERR("Failed to parse operand for jump in block "
                                 "'{}'. Expected "
                                 "name but got '{}'",
                                 block_name,
                                 line);
                    return false;
                }

                if (parse_state.block_map.contains(op_name)) {
                    TPDE_LOG_TRACE("Got jump to block '{}' with idx {}",
                                   op_name,
                                   parse_state.block_map[op_name]);
                    block_succs.push_back(parse_state.block_map[op_name]);
                } else {
                    block_succs.push_back(~0u);
                    parse_state.pending_block_resolves[op_name].push_back(
                        PendingBlockEntry{.type = PendingBlockEntry::Type::succ,
                                          .block_idx = block_idx,
                                          .succ_idx  = succ_count});
                    TPDE_LOG_TRACE("Got jump to block '{}' at {}+{}",
                                   op_name,
                                   block_idx,
                                   succ_count);
                }

                ++succ_count;
            }

        } else if (line.starts_with("terminate")) {
            TPDE_LOG_TRACE("Got terminating instruction for block {}",
                           block_idx);
            line.remove_prefix(9);
            const u32 val_idx = values.size();
            values.push_back(
                Value{.name         = std::string(),
                      .type         = Value::Type::ret,
                      .op_count     = 0,
                      .op_begin_idx = static_cast<u32>(value_operands.size())});
            TPDE_LOG_TRACE("Got return with idx {}", val_idx);

            remove_whitespace(line);
            if (!line.empty()) {
                if (line[0] != '%') {
                    TPDE_LOG_ERR("Expected value after terminate, got {}",
                                 line);
                    return false;
                }

                line.remove_prefix(1);
                const auto op_name = parse_name(line);
                if (op_name.empty()) {
                    TPDE_LOG_ERR(
                        "Failed to parse operand for terminate. Expected "
                        "name but got '{}'",
                        line);
                    return false;
                }

                if (parse_state.value_map.contains(op_name)) {
                    value_operands.push_back(parse_state.value_map[op_name]);
                    TPDE_LOG_TRACE("Got terminate op '{}' with idx {}",
                                   op_name,
                                   parse_state.value_map[op_name]);
                } else {
                    TPDE_LOG_TRACE(
                        "Got terminate op '{}' with unknown idx at {}+{}",
                        op_name,
                        val_idx,
                        values[val_idx].op_count);
                    value_operands.push_back(~0u);
                    parse_state.pending_value_resolves[op_name].emplace_back(
                        val_idx, values[val_idx].op_count);
                }

                ++values[val_idx].op_count;
            }

            values[val_idx].op_end_idx =
                static_cast<u32>(value_operands.size());
        } else if (line.starts_with("br")) {
            line.remove_prefix(2);
            remove_whitespace(line);

            if (line.empty() || line[0] != '%') {
                TPDE_LOG_ERR("Expected block name after br but got '{}'", line);
                return false;
            }

            line.remove_prefix(1);
            const auto op_name = parse_name(line);
            if (op_name.empty()) {
                TPDE_LOG_ERR("Failed to parse operand for br in block "
                             "'{}'. Expected "
                             "name but got '{}'",
                             block_name,
                             line);
                return false;
            }

            const u32 val_idx = values.size();
            values.push_back(
                Value{.name         = std::string(),
                      .type         = Value::Type::br,
                      .op_count     = 0,
                      .op_begin_idx = static_cast<u32>(value_operands.size())});
            TPDE_LOG_TRACE("Got br with idx {}", val_idx);

            if (parse_state.block_map.contains(op_name)) {
                TPDE_LOG_TRACE("Got br to block '{}' with idx {}",
                               op_name,
                               parse_state.block_map[op_name]);
                block_succs.push_back(parse_state.block_map[op_name]);
                value_operands.push_back(parse_state.block_map[op_name]);
            } else {
                block_succs.push_back(~0u);
                value_operands.push_back(~0u);
                parse_state.pending_block_resolves[op_name].push_back(
                    PendingBlockEntry{.type = PendingBlockEntry::Type::succ,
                                      .block_idx = block_idx,
                                      .succ_idx  = 0});

                parse_state.pending_block_resolves[op_name].push_back(
                    PendingBlockEntry{.type = PendingBlockEntry::Type::value,
                                      .value_idx = val_idx,
                                      .op_idx    = 0});
                TPDE_LOG_TRACE(
                    "Got br to block '{}' at {}", op_name, block_idx);
            }

            values[val_idx].op_end_idx =
                static_cast<u32>(value_operands.size());
            values[val_idx].op_count = 1;
        } else {
            assert(line.starts_with("condbr"));
            line.remove_prefix(6);
            remove_whitespace(line);

            if (line.empty() || line[0] != '%') {
                TPDE_LOG_ERR("Expected value name after condbr but got '{}'",
                             line);
                return false;
            }
            line.remove_prefix(1);

            const auto val_name = parse_name(line);
            if (val_name.empty()) {
                TPDE_LOG_ERR("Expected value name after condbr but got '{}'",
                             line);
                return false;
            }

            std::string_view block_names[2];

            for (u32 i = 0; i < 2; ++i) {
                remove_whitespace(line);
                if (line.empty() || line[0] != ',') {
                    TPDE_LOG_ERR(
                        "Expected two block names after condbr but got '{}'",
                        line);
                    return false;
                }
                line.remove_prefix(1);
                remove_whitespace(line);
                if (line.empty() || line[0] != '%') {
                    TPDE_LOG_ERR(
                        "Expected two block names after condbr but got '{}'",
                        line);
                    return false;
                }
                line.remove_prefix(1);

                const auto block_name = parse_name(line);
                if (block_name.empty()) {
                    TPDE_LOG_ERR("Invalid block name. Got '{}'", line);
                    return false;
                }

                block_names[i] = block_name;
            }

            const u32 val_idx = values.size();
            values.push_back(
                Value{.name         = std::string(),
                      .type         = Value::Type::condbr,
                      .op_count     = 0,
                      .op_begin_idx = static_cast<u32>(value_operands.size())});
            TPDE_LOG_TRACE("Got condbr with idx {}", val_idx);

            if (parse_state.value_map.contains(val_name)) {
                TPDE_LOG_TRACE("Got condbr value operand with idx {}",
                               parse_state.value_map[val_name]);
                value_operands.push_back(parse_state.value_map[val_name]);
            } else {
                TPDE_LOG_TRACE("Got condbr value operand with name '{}'",
                               val_name);
                value_operands.push_back(~0u);
                parse_state.pending_value_resolves[val_name].push_back(
                    std::make_pair(val_idx, 0));
            }

            for (u32 i = 0; i < 2; ++i) {
                auto name = block_names[i];
                if (parse_state.block_map.contains(name)) {
                    TPDE_LOG_TRACE("Got condbr block operand at {} with idx {}",
                                   i,
                                   parse_state.block_map[name]);
                    value_operands.push_back(parse_state.block_map[name]);
                    block_succs.push_back(parse_state.block_map[name]);
                } else {
                    TPDE_LOG_TRACE(
                        "Got condbr block operand at {} with name '{}'",
                        i,
                        name);
                    value_operands.push_back(~0u);
                    block_succs.push_back(~0u);
                    parse_state.pending_block_resolves[name].push_back(
                        PendingBlockEntry{.type =
                                              PendingBlockEntry::Type::value,
                                          .value_idx = val_idx,
                                          .op_idx    = 1 + i});
                    parse_state.pending_block_resolves[name].push_back(
                        PendingBlockEntry{.type = PendingBlockEntry::Type::succ,
                                          .block_idx = block_idx,
                                          .succ_idx  = i});
                }
            }

            values[val_idx].op_end_idx =
                static_cast<u32>(value_operands.size());
            values[val_idx].op_count = 3;
        }

        blocks[block_idx].succ_end_idx = block_succs.size();
        remove_whitespace(line);
        if (!line.empty()) {
            TPDE_LOG_ERR("Got garbage after terminating instruction for block "
                         "'{}': '{}'",
                         blocks[parse_state.cur_block].name,
                         line);
            return false;
        }

        blocks[parse_state.cur_block].inst_end_idx = values.size();
        parse_state.block_finished                 = true;

        return true;
    } else {
        // block name
        if (!parse_state.block_finished) {
            TPDE_LOG_ERR("Block was not finished "
                         "but encountered non-value line '{}'",
                         line);
            return false;
        }

        blocks[parse_state.cur_block].inst_end_idx = values.size();

        return parse_block(line, parse_state, false);
    }
}

bool tpde::test::TestIR::parse_value_line(
    std::string_view line, BodyParseState &parse_state) noexcept {
    if (parse_state.block_finished) {
        TPDE_LOG_ERR("Trying to add instruction after terminating "
                     "instruction for block '{}'",
                     blocks[parse_state.cur_block].name);
        return false;
    }

    // value
    line.remove_prefix(1);

    const auto value_name = parse_name(line);
    if (value_name.empty()) {
        TPDE_LOG_ERR("Failed to parse value name. Got '{}'", line);
        return false;
    }

    if (parse_state.value_map.contains(value_name)) {
        TPDE_LOG_ERR("Got duplicate value definition for '{}'", value_name);
        return false;
    }

    remove_whitespace(line);

    auto force_fixed_assignment = false;
    if (!line.empty() && line[0] == '!') {
        force_fixed_assignment = true;
        line.remove_prefix(1);
        remove_whitespace(line);
    }

    if (line.empty() || line[0] != '=') {
        TPDE_LOG_ERR("Expected '=' after value name but got '{}'", line);
        return false;
    }

    line.remove_prefix(1);
    remove_whitespace(line);

    const u32 val_idx = values.size();
    values.push_back(
        Value{.name                   = std::string(value_name),
              .type                   = Value::Type::normal,
              .force_fixed_assignment = force_fixed_assignment,
              .op_count               = 0,
              .op_begin_idx = static_cast<u32>(value_operands.size())});
    TPDE_LOG_TRACE(
        "Got value definition for '{}' with idx {}", value_name, val_idx);

    parse_state.value_map[value_name] = val_idx;
    if (parse_state.pending_value_resolves.contains(value_name)) {
        for (const auto &[target_val_idx, op_idx] :
             parse_state.pending_value_resolves[value_name]) {
            const auto idx = values[target_val_idx].op_begin_idx + op_idx;
            assert(idx < values[target_val_idx].op_end_idx);
            TPDE_LOG_TRACE(
                "  Fixing pending resolve at {}+{}", target_val_idx, op_idx);
            value_operands[idx] = val_idx;
        }
        parse_state.pending_value_resolves.erase(value_name);
    }

    if (line.starts_with("alloca")) {
        parse_state.block_finished_phis = true;
        if (functions[parse_state.func_idx].block_begin_idx
            != parse_state.cur_block) {
            TPDE_LOG_ERR("Encountered alloca '{}' in non-entry block '{}'",
                         value_name,
                         blocks[parse_state.cur_block].name);
            return false;
        }

        line.remove_prefix(6);
        remove_whitespace(line);

        int base = 10;
        if (line.starts_with("0x")) {
            base = 16;
            line.remove_prefix(2);
        }

        u32        size = 0, align = 8;
        const auto res =
            std::from_chars(line.data(), line.data() + line.size(), size, base);
        if (res.ec != std::errc{}) {
            TPDE_LOG_ERR(
                "Failed to parse size for alloca '{}': '{}'", value_name, line);
            return false;
        }

        line.remove_prefix(res.ptr - line.data());
        remove_whitespace(line);
        if (line.starts_with(',')) {
            line.remove_prefix(1);
            remove_whitespace(line);
            if (line.starts_with("align ")) {
                line.remove_prefix(6);
                remove_whitespace(line);

                base = 10;
                if (line.starts_with("0x")) {
                    base = 16;
                    line.remove_prefix(2);
                }

                const auto res = std::from_chars(
                    line.data(), line.data() + line.size(), align, base);
                if (res.ec != std::errc{}) {
                    TPDE_LOG_ERR("Failed to parse align for alloca '{}': '{}'",
                                 value_name,
                                 line);
                    return false;
                }

                line.remove_prefix(res.ptr - line.data());

                if ((align & (align - 1)) != 0 || align > 16) {
                    TPDE_LOG_ERR("Alignment must be power of two and not "
                                 "bigger than 16 but is {}",
                                 align);
                    return false;
                }

            } else {
                TPDE_LOG_ERR("Expected align after alloca but got '{}'", line);
                return false;
            }
        }


        TPDE_LOG_TRACE("Got alloca with size 0x{:X} and align", size, align);
        values[val_idx].type         = Value::Type::alloca;
        values[val_idx].alloca_size  = size;
        values[val_idx].alloca_align = align;

    } else if (line.starts_with("phi")) {
        if (parse_state.block_finished_phis) {
            TPDE_LOG_ERR("Got phi '{}' in block after non-phi instruction",
                         value_name);
            return false;
        }

        line.remove_prefix(3);
        remove_whitespace(line);

        TPDE_LOG_TRACE("Got phi");
        values[val_idx].op_count = 0;
        values[val_idx].type     = Value::Type::phi;
        auto had_arg             = false;
        while (!line.empty()) {
            remove_whitespace(line);
            if (line.empty()) {
                break;
            }

            if (line[0] == ',') {
                if (had_arg) {
                    line.remove_prefix(1);
                    remove_whitespace(line);
                } else {
                    TPDE_LOG_ERR("Failed to parse operand for phi '{}'. Got "
                                 "unexpected ',' for first operand at '{}'",
                                 value_name,
                                 line);
                    return false;
                }
            }
            had_arg = true;

            if (line.empty() || line[0] != '[') {
                TPDE_LOG_ERR("Failed to parse operand for phi '{}'. "
                             "Expected '[', got '{}'",
                             value_name,
                             line);
                return false;
            }
            line.remove_prefix(1);

            const auto operand   = line.substr(0, line.find(']'));
            const auto split_pos = operand.find(',');
            if (split_pos == std::string::npos) {
                TPDE_LOG_ERR("Failed to parse operand for phi '{}'. "
                             "Expected comma in operand but got '{}'",
                             value_name,
                             operand);
                return false;
            }

            auto block_name        = operand.substr(0, split_pos);
            auto incoming_val_name = operand.substr(split_pos + 1);
            remove_whitespace(block_name);
            remove_whitespace(incoming_val_name);

            if (block_name.empty() || incoming_val_name.empty()
                || block_name[0] != '%' || incoming_val_name[0] != '%') {
                TPDE_LOG_ERR(
                    "Failed to parse operand for phi '{}'. Block or value "
                    "name not prefixed with '%'. Got '{}'",
                    value_name,
                    operand);
                return false;
            }

            block_name.remove_prefix(1);
            incoming_val_name.remove_prefix(1);

            const auto parsed_block_name = parse_name(block_name);
            const auto parsed_val_name   = parse_name(incoming_val_name);

            if (parsed_block_name.empty() || parsed_val_name.empty()) {
                TPDE_LOG_ERR("Failed to parse operand for phi '{}'. Expected "
                             "name but got '{}'",
                             value_name,
                             operand);
                return false;
            }

            TPDE_LOG_TRACE("Got phi argument '{}' from block '{}'",
                           parsed_val_name,
                           parsed_block_name);

            if (parse_state.block_map.contains(parsed_block_name)) {
                TPDE_LOG_TRACE("  Block '{}' has idx {}",
                               parsed_block_name,
                               parse_state.block_map[parsed_block_name]);
                value_operands.push_back(
                    parse_state.block_map[parsed_block_name]);
            } else {
                value_operands.push_back(~0u);
                parse_state.pending_block_resolves[parsed_block_name].push_back(
                    PendingBlockEntry{.type = PendingBlockEntry::Type::value,
                                      .value_idx = val_idx,
                                      .op_idx = values[val_idx].op_count * 2});
            }

            if (parse_state.value_map.contains(parsed_val_name)) {
                TPDE_LOG_TRACE("  Value '{}' has idx {}",
                               parsed_val_name,
                               parse_state.value_map[parsed_val_name]);
                value_operands.push_back(
                    parse_state.value_map[parsed_val_name]);
            } else {
                value_operands.push_back(~0u);
                parse_state.pending_value_resolves[parsed_val_name]
                    .emplace_back(val_idx, values[val_idx].op_count * 2 + 1);
            }

            ++values[val_idx].op_count;

            line.remove_prefix(operand.size() + 1);
        }
    } else {
        parse_state.block_finished_phis = true;

        values[val_idx].op_count = 0;
        values[val_idx].op       = Value::Op::none;

        remove_whitespace(line);

        u32              required_op_count = ~0u;
        std::string_view op                = {};
        if (!line.empty()) {
            if (line.starts_with("add ")) {
                op = line.substr(0, 3);
                line.remove_prefix(4);
                values[val_idx].op = Value::Op::add;
                required_op_count  = 2;
            } else if (line.starts_with("sub ")) {
                op = line.substr(0, 3);
                line.remove_prefix(4);
                values[val_idx].op = Value::Op::sub;
                required_op_count  = 2;
            } else if (line.starts_with("call ")) {
                op = line.substr(0, 4);
                line.remove_prefix(5);
                values[val_idx].type                     = Value::Type::call;
                functions[parse_state.func_idx].has_call = true;
            }
        }
        if (values[val_idx].op != Value::Op::none) {
            TPDE_LOG_TRACE("  Value {} has op {}", val_idx, op);
        }

        if (values[val_idx].type == Value::Type::call) {
            remove_whitespace(line);
            if (line.empty() || line[0] != '%') {
                TPDE_LOG_ERR("Expected function name for call but got EOL");
                return false;
            }

            line.remove_prefix(1);
            const auto func_name = parse_name(line);
            if (func_name.empty()) {
                TPDE_LOG_ERR(
                    "Failed to parse function name for call '{}'. Expected "
                    "name but got '{}'",
                    value_name,
                    line);
                return false;
            }

            if (auto it = std::find_if(
                    functions.begin(),
                    functions.end(),
                    [&](const auto &func) { return func.name == func_name; });
                it != functions.end()) {
                values[val_idx].call_func_idx =
                    static_cast<u32>(it - functions.begin());
                TPDE_LOG_TRACE("Got call to func '{}' with idx {}",
                               func_name,
                               values[val_idx].call_func_idx);

                const auto &func  = functions[values[val_idx].call_func_idx];
                required_op_count = func.arg_end_idx - func.arg_begin_idx;
            } else {
                TPDE_LOG_TRACE("Got call to func '{}' with unknown idx at {}",
                               func_name,
                               val_idx);
                values[val_idx].call_func_idx = ~0u;
                (*parse_state.pending_call_resolves)[func_name].emplace_back(
                    val_idx);
            }

            remove_whitespace(line);
            if (!line.empty() && line[0] == ',') {
                line.remove_prefix(1);
            }
        }

        auto had_arg = false;
        while (!line.empty()) {
            remove_whitespace(line);
            if (line.empty()) {
                break;
            }

            if (line[0] == ',') {
                if (had_arg) {
                    line.remove_prefix(1);
                    remove_whitespace(line);
                } else {
                    TPDE_LOG_ERR("Failed to parse operand for value '{}'. Got "
                                 "unexpected ',' for first operand at '{}'",
                                 value_name,
                                 line);
                    return false;
                }
            }
            had_arg = true;

            if (line.empty() || line[0] != '%') {
                TPDE_LOG_ERR("Failed to parse operand for value '{}'. "
                             "Expected '%', got '{}'",
                             value_name,
                             line);
                return false;
            }
            line.remove_prefix(1);

            const auto op_name = parse_name(line);
            if (op_name.empty()) {
                TPDE_LOG_ERR("Failed to parse operand for value '{}'. Expected "
                             "name but got '{}'",
                             value_name,
                             line);
                return false;
            }

            if (parse_state.value_map.contains(op_name)) {
                value_operands.push_back(parse_state.value_map[op_name]);
                TPDE_LOG_TRACE("Got value op '{}' with idx {}",
                               op_name,
                               parse_state.value_map[op_name]);
            } else {
                TPDE_LOG_TRACE("Got value op '{}' with unknown idx at {}+{}",
                               op_name,
                               val_idx,
                               values[val_idx].op_count);
                value_operands.push_back(~0u);
                parse_state.pending_value_resolves[op_name].emplace_back(
                    val_idx, values[val_idx].op_count);
            }

            ++values[val_idx].op_count;
        }

        if (required_op_count != ~0u
            && required_op_count != values[val_idx].op_count) {
            TPDE_LOG_ERR(
                "Invalid op count {} for op {}", values[val_idx].op_count, op);
            return false;
        }
    }

    values[val_idx].op_end_idx = static_cast<u32>(value_operands.size());

    remove_whitespace(line);
    if (!line.empty()) {
        TPDE_LOG_ERR(
            "Got garbage after parsing value '{}': '{}'", value_name, line);
        return false;
    }

    return true;
}

bool tpde::test::TestIR::parse_block(std::string_view line,
                                     BodyParseState  &parse_state,
                                     const bool       is_entry) noexcept {
    const auto func_idx   = parse_state.func_idx;
    const auto block_name = parse_name(line);
    if (block_name.empty()) {
        TPDE_LOG_ERR("Failed to parse block for function '{}'. "
                     "Expected block name but got '{}'",
                     functions[func_idx].name,
                     line);
        return false;
    }

    if (line.empty() || line[0] != ':') {
        TPDE_LOG_ERR("Failed to parse block for function '{}'. "
                     "Expected ':' after block name but got '{}'",
                     functions[func_idx].name,
                     line);
        return false;
    }

    line.remove_prefix(1);
    remove_whitespace(line);
    if (!line.empty()) {
        TPDE_LOG_ERR("Failed to parse block for function '{}'. "
                     "Got garbage after '{}:'",
                     functions[func_idx].name,
                     block_name);
        return false;
    }

    if (parse_state.block_map.contains(block_name)) {
        TPDE_LOG_ERR("Failed to parse block for function '{}'. Block name "
                     "'{}' was already used.",
                     functions[func_idx].name,
                     block_name);
        return false;
    }

    if (!is_entry) {
        blocks[parse_state.cur_block].has_sibling = true;
    }

    const u32 cur_block   = blocks.size();
    parse_state.cur_block = cur_block;
    blocks.push_back(Block{.name           = std::string(block_name),
                           .inst_begin_idx = static_cast<u32>(values.size()),
                           .inst_end_idx   = static_cast<u32>(values.size())});
    parse_state.block_map[block_name] = cur_block;
    parse_state.block_finished        = false;
    parse_state.block_finished_phis   = false;

    TPDE_LOG_TRACE(
        "Starting new block '{}' with idx {}", block_name, cur_block);

    if (parse_state.pending_block_resolves.contains(block_name)) {
        // fill in pending blocks
        for (const auto &entry :
             parse_state.pending_block_resolves[block_name]) {
            if (entry.type == PendingBlockEntry::Type::succ) {
                TPDE_LOG_TRACE("Filling pending succ resolve at {}+{}",
                               entry.block_idx,
                               entry.succ_idx);
                const auto succ_idx =
                    blocks[entry.block_idx].succ_begin_idx + entry.succ_idx;
                assert(succ_idx < blocks[entry.block_idx].succ_end_idx);
                block_succs[succ_idx] = cur_block;
            } else {
                TPDE_LOG_TRACE("Filling pending operand resolve at {}+{}",
                               entry.value_idx,
                               entry.op_idx);
                const auto op_idx =
                    values[entry.value_idx].op_begin_idx + entry.op_idx;
                assert(op_idx < values[entry.value_idx].op_end_idx);
                value_operands[op_idx] = cur_block;
            }
        }
        parse_state.pending_block_resolves.erase(block_name);
    }

    return true;
}

void tpde::test::TestIR::remove_whitespace(std::string_view &text) noexcept {
    // remove comments
    while (!text.empty() && text[0] == ';') {
        auto comment_len = text.find('\n');
        if (comment_len == std::string::npos) {
            comment_len = text.size() - 1;
        }
        text = text.substr(comment_len + 1);
    }

    if (text.empty()) {
        return;
    }

    if (!std::isspace(text[0])) {
        return;
    }

    const auto len = std::find_if(text.begin(),
                                  text.end(),
                                  [](const auto c) { return !std::isspace(c); })
                     - text.begin();
    text.remove_prefix(len);

    // remove comments
    if (!text.empty() && text[0] == ';') {
        auto comment_len = text.find('\n');
        if (comment_len == std::string::npos) {
            comment_len = text.size() - 1;
        }
        text = text.substr(comment_len + 1);
        remove_whitespace(text);
    }
}

std::string_view
    tpde::test::TestIR::get_line(const std::string_view text) noexcept {
    const auto off = text.find_first_of('\n');
    if (off == std::string::npos) {
        return text;
    }

    return text.substr(0, off);
}

std::string_view
    tpde::test::TestIR::parse_name(std::string_view &text) noexcept {
    if (text.empty()) {
        TPDE_LOG_ERR("Tried to parse name but found empty string");
        return {};
    }

    if (!std::isalpha(text[0])) {
        TPDE_LOG_ERR(
            "Tried to parse name but found non-alphabetical character '{}'",
            text[0]);
        return {};
    }

    const auto len =
        std::find_if(text.begin(),
                     text.end(),
                     [](const auto c) { return !std::isalnum(c) && c != '_'; })
        - text.begin();
    const auto name = text.substr(0, len);
    text.remove_prefix(len);
    return name;
}

void tpde::test::TestIR::dump_debug() const noexcept {
    TPDE_LOG_DBG("Dumping IR");

    for (u32 i = 0; i < functions.size(); ++i) {
        const auto &func = functions[i];
        if (func.declaration) {
            TPDE_LOG_DBG("Extern function {}: {}", i, func.name);
        } else if (func.local_only) {
            TPDE_LOG_DBG("Local function {}: {}", i, func.name);
        } else {
            TPDE_LOG_DBG("Function {}: {}", i, func.name);
        }

        TPDE_LOG_TRACE("  Arg {}->{}", func.arg_begin_idx, func.arg_end_idx);
        for (auto arg_idx = func.arg_begin_idx; arg_idx < func.arg_end_idx;
             ++arg_idx) {
            assert(values[arg_idx].type == Value::Type::arg);
            TPDE_LOG_DBG("  Arg {}: {}", arg_idx, values[arg_idx].name);
        }

        if (func.declaration) {
            continue;
        }

        TPDE_LOG_TRACE(
            "  Block {}->{}", func.block_begin_idx, func.block_end_idx);
        for (auto block_idx = func.block_begin_idx;
             block_idx < func.block_end_idx;
             ++block_idx) {
            const auto &block = blocks[block_idx];
            TPDE_LOG_DBG("  Block {}: {}", block_idx, block.name);
            TPDE_LOG_TRACE(
                "    Succ {}->{}", block.succ_begin_idx, block.succ_end_idx);
            for (auto succ = block.succ_begin_idx; succ < block.succ_end_idx;
                 ++succ) {
                const auto succ_idx = block_succs[succ];
                TPDE_LOG_DBG(
                    "    Succ {}: {}", succ_idx, blocks[succ_idx].name);
            }

            TPDE_LOG_TRACE(
                "    Inst {}->{}", block.inst_begin_idx, block.inst_end_idx);
            for (auto val_idx = block.inst_begin_idx;
                 val_idx < block.inst_end_idx;
                 ++val_idx) {
                const auto &val = values[val_idx];

                switch (val.type) {
                    using enum Value::Type;
                case normal: {
                    if (val.op == Value::Op::none) {
                        TPDE_LOG_DBG("    Value {}: {}", val_idx, val.name);
                    } else {
                        TPDE_LOG_DBG("    Value {} ({}): {}",
                                     val_idx,
                                     Value::OP_NAMES[static_cast<u32>(val.op)],
                                     val.name);
                    }
                    for (auto op = val.op_begin_idx; op < val.op_end_idx;
                         ++op) {
                        const auto op_idx = value_operands[op];
                        TPDE_LOG_DBG(
                            "      Op {}: {}", op_idx, values[op_idx].name);
                    }
                    break;
                }
                case arg: {
                    assert(0);
                    exit(1);
                }
                case alloca: {
                    TPDE_LOG_DBG(
                        "    Alloca {} with size 0x{:X} and align {}: {}",
                        val_idx,
                        values[val_idx].alloca_size,
                        values[val_idx].alloca_align,
                        values[val_idx].name);
                    break;
                }
                case phi: {
                    TPDE_LOG_DBG("    PHI {}", val_idx);
                    for (u32 op = 0; op < val.op_count; ++op) {
                        const auto block_idx =
                            value_operands[val.op_begin_idx + op * 2];
                        const auto inc_idx =
                            value_operands[val.op_begin_idx + op * 2 + 1];
                        TPDE_LOG_DBG("      {} from {}: {} from {}",
                                     inc_idx,
                                     block_idx,
                                     values[inc_idx].name,
                                     blocks[block_idx].name);
                    }
                    break;
                }
                case ret: {
                    TPDE_LOG_DBG("    Ret {}", val_idx);
                    for (auto op = val.op_begin_idx; op < val.op_end_idx;
                         ++op) {
                        const auto op_idx = value_operands[op];
                        TPDE_LOG_DBG(
                            "      Op {}: {}", op_idx, values[op_idx].name);
                    }
                    break;
                }
                case br: {
                    TPDE_LOG_DBG("    Br {} to {}({})",
                                 val_idx,
                                 value_operands[val.op_begin_idx],
                                 blocks[value_operands[val.op_begin_idx]].name);
                    break;
                }
                case condbr: {
                    TPDE_LOG_DBG("    Condbr {}", val_idx);

                    auto *ops = &value_operands[val.op_begin_idx];
                    TPDE_LOG_DBG(
                        "      Value: {} ({})", ops[0], values[ops[0]].name);
                    TPDE_LOG_DBG(
                        "      TrueSucc: {} ({})", ops[1], blocks[ops[1]].name);
                    TPDE_LOG_DBG("      FalseSucc: {} ({})",
                                 ops[2],
                                 blocks[ops[2]].name);
                    break;
                }
                case call: {
                    TPDE_LOG_DBG("    Call {}: {} to {}: {}",
                                 val_idx,
                                 val.name,
                                 val.call_func_idx,
                                 functions[val.call_func_idx].name);

                    for (auto op = val.op_begin_idx; op < val.op_end_idx;
                         ++op) {
                        const auto op_idx = value_operands[op];
                        TPDE_LOG_DBG(
                            "      Op {}: {}", op_idx, values[op_idx].name);
                    }
                    break;
                }
                }
            }
        }
    }
}

void tpde::test::TestIR::print() const noexcept {
    fmt::println("Printing IR");

    for (u32 i = 0; i < functions.size(); ++i) {
        const auto &func = functions[i];
        if (func.declaration) {
            fmt::println("Extern function {}", func.name);
        } else if (func.local_only) {
            fmt::println("Local function {}", func.name);
        } else {
            fmt::println("Function {}", func.name);
        }

        for (auto arg_idx = func.arg_begin_idx; arg_idx < func.arg_end_idx;
             ++arg_idx) {
            assert(values[arg_idx].type == Value::Type::arg);
            fmt::println("  Argument {}", values[arg_idx].name);
        }

        if (func.declaration) {
            continue;
        }

        for (auto block_idx = func.block_begin_idx;
             block_idx < func.block_end_idx;
             ++block_idx) {
            const auto &block = blocks[block_idx];
            fmt::println("  Block {}", block.name);
            for (auto succ = block.succ_begin_idx; succ < block.succ_end_idx;
                 ++succ) {
                const auto succ_idx = block_succs[succ];
                fmt::println("    Succ {}", blocks[succ_idx].name);
            }

            for (auto val_idx = block.inst_begin_idx;
                 val_idx < block.inst_end_idx;
                 ++val_idx) {
                const auto &val = values[val_idx];

                switch (val.type) {
                    using enum Value::Type;
                case normal: {
                    if (val.op == Value::Op::none) {
                        fmt::println("    Value {}", val.name);
                    } else {
                        fmt::println("    Value {} ({})",
                                     val.name,
                                     Value::OP_NAMES[static_cast<u32>(val.op)]);
                    }
                    for (auto op = val.op_begin_idx; op < val.op_end_idx;
                         ++op) {
                        const auto op_idx = value_operands[op];
                        fmt::println("      Op {}", values[op_idx].name);
                    }
                    break;
                }
                case arg: {
                    assert(0);
                    exit(1);
                }
                case alloca: {
                    fmt::println("    Alloca with size 0x{:X} and align {}: {}",
                                 values[val_idx].alloca_size,
                                 values[val_idx].alloca_align,
                                 values[val_idx].name);
                    break;
                }
                case phi: {
                    fmt::println("    PHI {}", values[val_idx].name);
                    for (u32 op = 0; op < val.op_count; ++op) {
                        const auto block_idx =
                            value_operands[val.op_begin_idx + op * 2];
                        const auto inc_idx =
                            value_operands[val.op_begin_idx + op * 2 + 1];
                        fmt::println("      {} from {}",
                                     values[inc_idx].name,
                                     blocks[block_idx].name);
                    }
                    break;
                }
                case ret: {
                    fmt::println("    Ret");
                    for (auto op = val.op_begin_idx; op < val.op_end_idx;
                         ++op) {
                        const auto op_idx = value_operands[op];
                        fmt::println("      Op {}", values[op_idx].name);
                    }
                    break;
                }
                case br: {
                    fmt::println("    Br to {}",
                                 blocks[value_operands[val.op_begin_idx]].name);
                    break;
                }
                case condbr: {
                    fmt::println("    Condbr");
                    auto *ops = &value_operands[val.op_begin_idx];
                    fmt::println("      Value: {}", values[ops[0]].name);
                    fmt::println("      TrueSucc: {}", blocks[ops[1]].name);
                    fmt::println("      FalseSucc: {}", blocks[ops[2]].name);
                    break;
                }
                case call: {
                    fmt::println("    Call {} to {}",
                                 val.name,
                                 functions[val.call_func_idx].name);

                    for (auto op = val.op_begin_idx; op < val.op_end_idx;
                         ++op) {
                        const auto op_idx = value_operands[op];
                        fmt::println("      Op {}", values[op_idx].name);
                    }
                    break;
                }
                }
            }
        }
    }
}
