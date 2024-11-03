// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>

#include "arm64/LLVMCompilerArm64.hpp"
#include "x64/LLVMCompilerX64.hpp"

#include <fstream>
#include <iostream>
#include <memory>

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

    args::ValueFlag<std::string> target(parser,
                                        "target",
                                        "Target architecture",
                                        {"target"},
                                        args::Options::None);

    args::ValueFlag<std::string> obj_out_path(
        parser,
        "obj_path",
        "Path where the output object file should be written",
        {'o', "obj-out"},
        args::Options::None);

    args::Positional<std::string> ir_path(
        parser, "ir_path", "Path to the input IR file", "-");

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

    llvm::LLVMContext  context;
    llvm::SMDiagnostic diag{};

    auto mod = llvm::parseIRFile(ir_path.Get(), diag, context);
    if (!mod) {
        diag.print(argv[0], llvm::errs());
        return 1;
    }

    if (print_ir) {
        mod->print(llvm::outs(), nullptr);
    }

    using CompileFn = bool(llvm::Module &, std::vector<uint8_t> &, bool);
    CompileFn *compile_fn;
    if (!target) {
        // TODO: extract architecture from module
        std::cerr << "Unspecified architecture, assuming x86_64\n";
        compile_fn = tpde_llvm::x64::compile_llvm;
#if defined(TPDE_LLVM_X64)
    } else if (target.Get() == "x86_64") {
        compile_fn = tpde_llvm::x64::compile_llvm;
#endif
#if defined(TPDE_LLVM_ARM64)
    } else if (target.Get() == "aarch64") {
        compile_fn = tpde_llvm::arm64::compile_llvm;
#endif
    } else {
        std::cerr << "Unknown architecture\n";
        return 1;
    }

    std::vector<uint8_t> buf;
    if (!compile_fn(*mod, buf, print_liveness)) {
        std::cerr << "Failed to compile\n";
        return 1;
    }

    if (!obj_out_path || obj_out_path.Get() == "-") {
        std::cout.write(reinterpret_cast<const char *>(buf.data()), buf.size());
    } else {
        std::ofstream out{obj_out_path.Get().c_str(), std::ios::binary};
        out.write(reinterpret_cast<const char *>(buf.data()), buf.size());
    }

    if (print_ir) {
        llvm::outs() << "\nIR after modification:\n\n";
        mod->print(llvm::outs(), nullptr);
    }

    return 0;
}
