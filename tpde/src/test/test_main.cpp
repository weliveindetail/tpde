// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <cstdio>
#include <fstream>
#include <iostream>

#include <args/args.hxx>

#include "TestIR.hpp"
#include "TestIRCompiler.hpp"
#include "TestIRCompilerA64.hpp"
#include "tpde/Analyzer.hpp"
#include "tpde/CompilerBase.hpp"

enum class Arch {
    x64,
    a64,
};

int main(int argc, char *argv[]) {
    using namespace tpde;

    args::ArgumentParser parser("Testing utility for TPDE");
    args::HelpFlag       help(parser, "help", "Display help", {'h', "help"});
    args::ValueFlag<unsigned> log_level(
        parser,
        "log_level",
        "Set the log level to 0=NONE, 1=ERR, 2=WARN(default), 3=INFO, 4=DEBUG, "
        ">5=TRACE",
        {'l', "log-level"},
        2);

    args::Flag print_ir(
        parser, "print_ir", "Print the IR after parsing", {"print-ir"});

    args::Flag print_rpo(
        parser, "print_rpo", "Print the block RPO", {"print-rpo"});

    args::Flag print_layout(parser,
                            "print_layout",
                            "Print the finished block layout",
                            {"print-layout"});

    args::Flag print_loops(
        parser, "print_loops", "Print the loops", {"print-loops"});

    args::Flag print_liveness(parser,
                              "print_liveness",
                              "Print the liveness information",
                              {"print-liveness"});

    args::Flag no_fixed_assignments(
        parser,
        "no_fixed_assignments",
        "Prevent fixed assignments from occuring unless they are forced",
        {"no-fixed-assignments"});

    std::unordered_map<std::string_view, RunTestUntil> run_map{
        {    "full",          RunTestUntil::full},
        {      "ir",    RunTestUntil::ir_parsing},
        {     "rpo",           RunTestUntil::rpo},
        {  "layout",  RunTestUntil::block_layout},
        {"analyzer", RunTestUntil::only_analyzer},
    };
    args::MapFlag<std::string_view, RunTestUntil> run_until(
        parser,
        "run_until",
        "Run the test only to a certain step in the pipeline",
        {"run-until"},
        run_map,
        RunTestUntil::full);

    std::unordered_map<std::string_view, Arch> arch_map{
        {"x64", Arch::x64},
        {"a64", Arch::a64}
    };
    args::MapFlag<std::string_view, Arch> arch(
        parser,
        "arch",
        "Which architecture to compile to",
        {"arch"},
        arch_map,
        Arch::x64);

    args::ValueFlag<std::string> obj_out_path(
        parser,
        "obj_path",
        "Path where the output object file should be written",
        {'o', "obj-out"});

    args::Positional<std::string> ir_path(
        parser, "ir_path", "Path to the input IR file");

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
    {
        spdlog::level::level_enum level = spdlog::level::off;
        switch (log_level.Get()) {
        case 0: level = spdlog::level::off; break;
        case 1: level = spdlog::level::err; break;
        case 2: level = spdlog::level::warn; break;
        case 3: level = spdlog::level::info; break;
        case 4: level = spdlog::level::debug; break;
        default:
            assert(level >= 5);
            level = spdlog::level::trace;
            break;
        }

        spdlog::set_level(level);
    }
#endif

    std::string buf;

    if (ir_path) {
        std::cout << "GOT IR FILE\n";
        const auto file_path = args::get(ir_path);
        auto       file      = std::ifstream{file_path, std::ios::ate};
        if (!file.is_open()) {
            fprintf(stderr, "Failed to open file '%s'\n", file_path.c_str());
            return 1;
        }

        const auto file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        buf.resize(file_size);

        file.read(buf.data(), file_size);
    } else {
        // Read from stdin
        std::string line{};
        while (std::getline(std::cin, line)) {
            if (std::cin.eof()) {
                break;
            }
            buf += line;
            buf += '\n';
        }
    }

    test::TestIR ir{};
    if (!ir.parse_ir(buf)) {
        fprintf(stderr, "Failed to parse IR\n");
        return 1;
    }

    if (print_ir) {
        ir.print();
        return 0;
    }

    if (run_until.Get() == RunTestUntil::ir_parsing) {
        return 0;
    }

    if (static_cast<u32>(run_until.Get())
        <= static_cast<u32>(RunTestUntil::only_analyzer)) {
        test::TestIRAdaptor adaptor{&ir};

        Analyzer<test::TestIRAdaptor> analyzer{&adaptor};
        analyzer.test_run_until          = run_until.Get();
        analyzer.test_print_rpo          = print_rpo;
        analyzer.test_print_block_layout = print_layout;
        analyzer.test_print_loops        = print_loops;
        analyzer.test_print_liveness     = print_liveness;

        for (auto func : adaptor.funcs()) {
            adaptor.switch_func(func);
            analyzer.switch_func(func);

            analyzer.reset();
        }

        return 0;
    }

    // TODO(ts): multiple arch select
    if (arch.Get() == Arch::x64) {
        test::TestIRAdaptor     adaptor{&ir};
        test::TestIRCompilerX64 compiler{&adaptor, no_fixed_assignments};

        compiler.analyzer.test_run_until          = run_until.Get();
        compiler.analyzer.test_print_rpo          = print_rpo;
        compiler.analyzer.test_print_block_layout = print_layout;
        compiler.analyzer.test_print_loops        = print_loops;
        compiler.analyzer.test_print_liveness     = print_liveness;

        if (!compiler.compile()) {
            TPDE_LOG_ERR("Failed to compile IR");
            return 1;
        }

        if (obj_out_path) {
            const std::vector<u8> data = compiler.assembler.build_object_file();
            std::ofstream out_file{obj_out_path.Get(), std::ios::binary};
            if (!out_file.is_open()) {
                TPDE_LOG_ERR("Failed to open output file");
                return 1;
            }
            out_file.write(reinterpret_cast<const char *>(data.data()),
                           data.size());
        }
    } else {
        assert(arch.Get() == Arch::a64);
        if (!test::compile_ir_arm64(&ir,
                                    run_until.Get(),
                                    print_rpo,
                                    print_layout,
                                    print_loops,
                                    print_liveness,
                                    no_fixed_assignments.Get(),
                                    obj_out_path.Get())) {
            TPDE_LOG_ERR("Failed to compiler IR");
            return 1;
        }
    }


    return 0;
}
