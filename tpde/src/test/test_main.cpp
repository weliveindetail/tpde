// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <cstdio>
#include <fstream>
#include <iostream>

#include <args/args.hxx>

#include "TestIR.hpp"

int main(int argc, char *argv[]) {
    args::ArgumentParser parser("Testing utility for TPDE");
    args::HelpFlag       help(parser, "help", "Display help", {'h', "help"});

    args::Flag print_ir(
        parser, "print_ir", "Print the IR after parsing", {"print-ir"});

    args::Positional<std::string> ir_path(parser,
                                          "ir_path",
                                          "Path to the input IR file",
                                          args::Options::Required);

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    } catch (args::ValidationError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }


// TODO(ts): make this configurable
#ifdef TPDE_LOGGING
    spdlog::set_level(spdlog::level::trace);
#endif

    const auto file_path = args::get(ir_path);
    auto       file      = std::ifstream{file_path, std::ios::ate};
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open file '%s'\n", file_path.c_str());
        return 1;
    }

    const auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string buf;
    buf.resize(file_size);

    file.read(buf.data(), file_size);

    tpde::test::TestIR ir{};
    if (!ir.parse_ir(buf)) {
        fprintf(stderr, "Failed to parse IR\n");
        return 1;
    }

    if (print_ir) {
        ir.print();
        return 0;
    }
}
