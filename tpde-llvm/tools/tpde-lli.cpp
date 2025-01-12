// SPDX-License-Identifier: LicenseRef-Proprietary
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include "tpde-llvm/LLVMCompiler.hpp"

#include <dlfcn.h>
#include <iostream>
#include <memory>

#ifdef TPDE_LOGGING
  #include <spdlog/spdlog.h>
#endif

#define ARGS_NOEXCEPT
#include <args/args.hxx>

int main(int argc, char *argv[]) {
  args::ArgumentParser parser("TPDE LLI");
  args::HelpFlag help(parser, "help", "Display help", {'h', "help"});

  args::ValueFlag<unsigned> log_level(
      parser,
      "log_level",
      "Set the log level to 0=NONE, 1=ERR, 2=WARN(default), 3=INFO, 4=DEBUG, "
      ">5=TRACE",
      {'l', "log-level"},
      2);
  args::Flag print_ir(parser, "print_ir", "Print LLVM-IR", {"print-ir"});

  args::Positional<std::string> ir_path(
      parser, "ir_path", "Path to the input IR file", "-");

  parser.ParseCLI(argc, argv);
  if (parser.GetError() == args::Error::Help) {
    std::cout << parser;
    return 0;
  }

  if (parser.GetError() != args::Error::None) {
    std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << '\n';
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

  llvm::LLVMContext context;
  llvm::SMDiagnostic diag{};

  auto mod = llvm::parseIRFile(ir_path.Get(), diag, context);
  if (!mod) {
    diag.print(argv[0], llvm::errs());
    return 1;
  }

  llvm::Function *main_fn = mod->getFunction("main");
  if (!main_fn) {
    std::cerr << "module has no main function\n";
    return 1;
  }

  std::string triple_str = llvm::sys::getProcessTriple();
  llvm::Triple triple(triple_str);
  auto compiler = tpde_llvm::LLVMCompiler::create(triple);
  if (!compiler) {
    std::cerr << "Unknown architecture: " << triple_str << "\n";
    return 1;
  }

  auto mapper = compiler->compile_and_map(*mod, [](std::string_view name) {
    return ::dlsym(RTLD_DEFAULT, std::string(name).c_str());
  });
  void *main_addr = mapper.lookup_global(main_fn);
  if (!main_addr) {
    std::cerr << "JIT compilation failed\n";
    return 1;
  }

  return ((int (*)(int, char **))main_addr)(0, nullptr);
}
