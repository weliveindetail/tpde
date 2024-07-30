// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>

#include "x64/LLVMCompilerX64.hpp"

#include <format>
#include <iostream>
#include <memory>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        return 1;
    }

    const char *bitcode_path = argv[1];
    const char *output_path  = argv[2];
    bool        print_ir     = false;
    if (const char *env_p = std::getenv("TPDE_LLVM_PRINT_IR");
        env_p && strcmp(env_p, "true") == 0) {
        print_ir = true;
    }

    auto bitcode = llvm::MemoryBuffer::getFile(bitcode_path);
    if (!bitcode) {
        std::cerr << std::format("Failed to read bitcode file: '{}'\n",
                                 bitcode.getError().message());
        return 1;
    }

    std::unique_ptr<llvm::MemoryBuffer> bitcode_buf;
    bitcode_buf.swap(bitcode.get());

    auto context = std::make_unique<llvm::LLVMContext>();

    auto mod_res = llvm::parseBitcodeFile(*bitcode_buf, *context);
    if (auto E = mod_res.takeError()) {
        std::cerr << std::format("Failed to parse bitcode: '{}'\n",
                                 llvm::toString(std::move(E)));
        return 1;
    }

    std::unique_ptr<llvm::Module> mod;
    mod.swap(*mod_res);

    if (print_ir) {
        mod->print(llvm::outs(), nullptr);
    }

    if (!tpde_llvm::x64::compile_llvm(*context, *mod, output_path)) {
        std::cerr << std::format("Failed to compile\n");
        return 1;
    }

    if (print_ir) {
        llvm::outs() << "\nIR after modification:\n\n";
        mod->print(llvm::outs(), nullptr);
    }

    return 0;
}
