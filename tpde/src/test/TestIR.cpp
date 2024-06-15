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
//     %<valName> = %<valName>, %<valName>, ...
//     jump %<blockName>, %<blockName>, ...
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

#include "TestIR.hpp"

bool tpde::test::TestIR::parse_ir(std::string_view text) noexcept {
    TPDE_LOG_TRACE("Parsing IR");

    values.clear();
    value_operands.clear();
    blocks.clear();
    block_succs.clear();
    functions.clear();

    while (!text.empty()) {
        if (!parse_func(text)) {
            TPDE_LOG_ERR("Failed to parse function");
            return false;
        }
    }

    TPDE_LOG_TRACE("Finished parsing IR");
    dump_trace();
}

bool tpde::test::TestIR::parse_func(std::string_view &text) noexcept {
    remove_whitespace(text);

    const auto name = parse_name(text);
    if (name.empty()) {
        TPDE_LOG_ERR("Failed to parse function name. At '{}'", get_line(text));
        return false;
    }

    const auto func_idx = functions.size();
    functions.push_back(Function{.name = std::string(name)});

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

    auto           args = text.substr(0, arg_end_off);
    BodyParseState parse_state;
    parse_state.func_idx = func_idx;

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
            .name = std::string(arg_name),
            .type = Value::Type::arg,
        });

        parse_state.value_map[arg_name] = arg_idx;
    }

    functions[func_idx].arg_end_idx = values.size();

    text.remove_prefix(arg_end_off + 1);

    remove_whitespace(text);

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

    text.remove_prefix(body_len);

    // TODO(ts): validate domination?
    return true;
}

bool tpde::test::TestIR::parse_func_body(std::string_view body,
                                         BodyParseState &parse_state) noexcept {
    auto       first    = true;
    const auto func_idx = parse_state.func_idx;

    while (!body.empty()) {
        remove_whitespace(body);
        auto line_len = body.find('\n');
        if (line_len == std::string::npos) {
            line_len = body.size();
        }
        auto line = body.substr(0, line_len);

        if (first) {
            // parse entry block
            if (!parse_block(line, parse_state)) {
                TPDE_LOG_ERR("Failed to parse entry block for function '{}'",
                             functions[func_idx].name);
                return false;
            }

            functions[func_idx].entry_block = parse_state.cur_block;
            first                           = false;
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

    return true;
}

bool tpde::test::TestIR::parse_body_line(std::string_view line,
                                         BodyParseState &parse_state) noexcept {
    assert(!line.empty());

    const auto func_idx = parse_state.func_idx;
    if (line[0] == '%') {
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
        if (line.empty() || line[0] != '=') {
            TPDE_LOG_ERR("Expected '=' after value name but got '{}'", line);
            return false;
        }

        line.remove_prefix(1);
        remove_whitespace(line);

        const u32 val_idx = values.size();
        values.push_back(
            Value{.name         = std::string(value_name),
                  .type         = Value::Type::normal,
                  .op_count     = 0,
                  .op_begin_idx = static_cast<u32>(value_operands.size())});

        parse_state.value_map[value_name] = val_idx;
        if (parse_state.pending_value_resolves.contains(value_name)) {
            for (const auto [val_idx, op_idx] : parse_state) {
                const auto idx = values[val_idx].op_begin_idx + op_idx;
                assert(idx < values[val_idx].op_end_idx);
                value_operands[idx] = val_idx;
            }
            parse_state.pending_value_resolves.erase(value_name);
        }

        if (line.starts_with("alloca")) {
            if (functions[func_idx].entry_block != parse_state.cur_block) {
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

            u32        size = 0;
            const auto res  = std::from_chars(
                line.data(), line.data() + line.size(), size, base);
            if (res.ec != std::errc{}) {
                TPDE_LOG_ERR("Failed to parse size for alloca '{}': '{}'",
                             value_name,
                             line);
                return false;
            }

            line.remove_prefix(res.ptr - line.data());

            values[val_idx].type        = Value::Type::alloca;
            values[val_idx].alloca_size = size;

        } else if (line.starts_with("phi")) {
            line.remove_prefix(3);
            remove_whitespace(line);

            values[val_idx].op_count = 0;
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
                        TPDE_LOG_ERR(
                            "Failed to parse operand for phi '{}'. Got "
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
                    TPDE_LOG_ERR(
                        "Failed to parse operand for phi '{}'. Expected "
                        "name but got '{}'",
                        value_name,
                        operand);
                    return false;
                }

                if (parse_state.block_map.contains(parsed_block_name)) {
                    value_operands.push_back(
                        parse_state.block_map[parsed_block_name]);
                } else {
                    value_operands.push_back(~0u);
                    parse_state.pending_block_resolves[parsed_block_name]
                        .push_back(PendingBlockEntry{
                            .type      = PendingBlockEntry::Type::value,
                            .value_idx = val_idx,
                            .op_idx    = values[val_idx].op_count * 2});
                }

                if (parse_state.value_map.contains(parsed_val_name)) {
                    value_operands.push_back(
                        parse_state.value_map[parsed_val_name]);
                } else {
                    value_operands.push_back(~0u);
                    parse_state.pending_value_resolves[parsed_val_name]
                        .emplace_back(val_idx,
                                      values[val_idx].op_count * 2 + 1);
                }

                ++values[val_idx].op_count;

                line.remove_prefix(operand.size() + 1);
            }
        } else {
            values[val_idx].op_count = 0;
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
                        TPDE_LOG_ERR(
                            "Failed to parse operand for value '{}'. Got "
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
                    TPDE_LOG_ERR(
                        "Failed to parse operand for value '{}'. Expected "
                        "name but got '{}'",
                        value_name,
                        line);
                    return false;
                }

                if (parse_state.value_map.contains(op_name)) {
                    value_operands.push_back(parse_state.value_map[op_name]);
                } else {
                    value_operands.push_back(~0u);
                    parse_state.pending_value_resolves[op_name].emplace_back(
                        val_idx, values[val_idx].op_count);
                }

                ++values[val_idx].op_count;
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
    } else if (line.starts_with("jump") || line.starts_with("terminate")) {
        blocks[parse_state.cur_block].inst_end_idx = values.size();
        parse_state.block_finished                 = true;

        const auto block_idx = parse_state.cur_block;
        if (line.starts_with("jump")) {
            line.remove_prefix(4);
            blocks[block_idx].succ_begin_idx = block_succs.size();
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
                    block_succs.push_back(parse_state.block_map[op_name]);
                } else {
                    block_succs.push_back(~0u);
                    parse_state.pending_block_resolves[op_name].push_back(
                        PendingBlockEntry{.type = PendingBlockEntry::Type::succ,
                                          .block_idx = block_idx,
                                          .succ_idx  = succ_count});
                }

                ++succ_count;
            }
        } else {
            line.remove_prefix(9);
        }

        remove_whitespace(line);
        if (!line.empty()) {
            TPDE_LOG_ERR("Got garbage after terminating instruction for block "
                         "'{}': '{}'",
                         blocks[parse_state.cur_block].name,
                         line);
            return false;
        }

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

        return parse_block(line, parse_state);
    }
}

bool tpde::test::TestIR::parse_block(std::string_view line,
                                     BodyParseState  &parse_state) noexcept {
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

    const u32 cur_block   = blocks.size();
    parse_state.cur_block = cur_block;
    blocks.push_back(Block{.name           = std::string(block_name),
                           .inst_begin_idx = static_cast<u32>(values.size()),
                           .inst_end_idx   = static_cast<u32>(values.size())});
    parse_state.block_map[block_name] = cur_block;
    parse_state.block_finished        = false;

    if (parse_state.pending_block_resolves.contains(block_name)) {
        // fill in pending blocks
        for (const auto &entry :
             parse_state.pending_block_resolves[block_name]) {
            if (entry.type == PendingBlockEntry::Type::succ) {
                const auto succ_idx =
                    blocks[entry.block_idx].succ_begin_idx + entry.succ_idx;
                assert(succ_idx < blocks[entry.block_idx].succ_end_idx);
                block_succs[succ_idx] = cur_block;
            } else {
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
    if (text.empty()) {
        return;
    }

    if (!std::isspace(text[0])) {
        return;
    }

    const auto len = std::find(text.begin(),
                               text.end(),
                               [](const auto c) { return !std::isspace(c); })
                     - text.begin();
    text.remove_prefix(len);

    // remove comments
    if (!text.empty() && text[0] == ';') {
        auto comment_len = text.find('\n');
        if (comment_len == std::string::npos) {
            comment_len = text.size();
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

    const auto len = std::find(text.begin(),
                               text.end(),
                               [](const auto c) { return !std::isalnum(c); })
                     - text.begin();
    const auto name = text.substr(0, len);
    text.remove_prefix(len);
    return name;
}

void tpde::test::TestIR::dump_debug() const noexcept {
    TPDE_LOG_DBG("Dumping IR");

    for (u32 i = 0; i < functions.size(); ++i) {
        const auto &func = functions[i];
        TPDE_LOG_DBG("Function {}: {}", i, func.name);
    }
}
