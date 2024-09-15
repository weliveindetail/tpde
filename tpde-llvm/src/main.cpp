// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#include <llvm/AsmParser/Parser.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>

#include "x64/LLVMCompilerX64.hpp"

#include <format>
#include <iostream>
#include <memory>
#include <stdio.h>

#include "tpde/base.hpp"

#define ARGS_NOEXCEPT
#include <args/args.hxx>

int main(int argc, char *argv[]) {
    args::ArgumentParser parser("TPDE for LLVM");
    args::HelpFlag       help(parser, "help", "Display help", {'h', "help"});

    args::ValueFlag<unsigned> log_level(
        parser,
        "log_level",
        "Set the log level to 0=NONE, 1=ERR, 2=WARN(default), 3=INFO, 4=DEBUG, "
        ">5=TRACE",
        {'l', "log-level"},
        2);
    args::Flag print_ir(parser, "print_ir", "Print LLVM-IR", {"print-ir"});

    args::Flag print_liveness(parser,
                              "print_liveness",
                              "Print the liveness information",
                              {"print-liveness"});

    args::Flag input_is_bitcode(
        parser, "bitcode", "Is the input LLVM-Bitcode?", {"bitcode"});

    args::ValueFlag<std::string> obj_out_path(
        parser,
        "obj_path",
        "Path where the output object file should be written",
        {'o', "obj-out"},
        args::Options::None);

    args::Positional<std::string> ir_path(
        parser, "ir_path", "Path to the input IR file");

    parser.ParseCLI(argc, argv);
    if (parser.GetError() == args::Error::Help) {
        std::cout << parser;
        return 0;
    }

    if (parser.GetError() != args::Error::None) {
        std::cerr << "Error parsing arguments: " << parser.GetErrorMsg()
                  << '\n';
        return 1;
    }

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

    std::unique_ptr<llvm::MemoryBuffer> bitcode_buf;
    if (ir_path) {
        auto bitcode = llvm::MemoryBuffer::getFile(ir_path.Get());
        if (!bitcode) {
            std::cerr << std::format("Failed to read bitcode file: '{}'\n",
                                     bitcode.getError().message());
            return 1;
        }

        bitcode_buf.swap(bitcode.get());
    } else {
        auto bitcode = llvm::MemoryBuffer::getSTDIN();
        if (!bitcode) {
            std::cerr << std::format("Failed to read bitcode file: '{}'\n",
                                     bitcode.getError().message());
            return 1;
        }

        bitcode_buf.swap(bitcode.get());
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto mod     = std::unique_ptr<llvm::Module>();

    if (input_is_bitcode) {
        if (auto E =
                llvm::parseBitcodeFile(*bitcode_buf, *context).moveInto(mod)) {
            std::cerr << std::format("Failed to parse bitcode: '{}'\n",
                                     llvm::toString(std::move(E)));
            return 1;
        }
    } else {
        auto diag = llvm::SMDiagnostic{};
        mod       = llvm::parseAssembly(*bitcode_buf, diag, *context);
        if (!mod) {
            std::string              buf;
            llvm::raw_string_ostream os{buf};

            diag.print(nullptr, os);
            std::cerr << "Failed to parse IR:\n";
            std::cerr << buf << '\n';
            return 1;
        }

        {
            std::string              buf;
            llvm::raw_string_ostream os{buf};
            if (llvm::verifyModule(*mod, &os)) {
                std::cerr << "Invalid LLVM module supplied:\n" << buf << '\n';
                return 1;
            }
        }
    }

    if (print_ir) {
        mod->print(llvm::outs(), nullptr);
    }

    if (obj_out_path) {
        if (!tpde_llvm::x64::compile_llvm(
                *context, *mod, obj_out_path.Get().c_str(), print_liveness)) {
            std::cerr << std::format("Failed to compile\n");
            return 1;
        }
    } else {
        std::vector<uint8_t> out_buf;
        if (!tpde_llvm::x64::compile_llvm(
                *context, *mod, out_buf, print_liveness)) {
            std::cerr << std::format("Failed to compile\n");
            return 1;
        }

        // very hacky
        fwrite(out_buf.data(), 1, out_buf.size(), stdout);
    }

    if (print_ir) {
        llvm::outs() << "\nIR after modification:\n\n";
        mod->print(llvm::outs(), nullptr);
    }

    return 0;
}
