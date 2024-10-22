// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <format>
#include <fstream>
#include <iostream>

#include <llvm/AsmParser/Parser.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include "arm64/EncCompilerTemplate.hpp"
#include "encode_gen.hpp"
#include "x64/EncCompilerTemplate.hpp"

#include <llvm/Support/SourceMgr.h>

namespace tpde_encgen {
llvm::cl::OptionCategory tpde_category("Code Generation Options");
llvm::cl::opt<std::string>
    output_filename("o",
                    llvm::cl::desc("Specify output filename"),
                    llvm::cl::value_desc("filename"),
                    llvm::cl::init("/dev/stdout"),
                    llvm::cl::cat(tpde_category));
llvm::cl::opt<bool>
    input_is_bitcode("is-bitcode", llvm::cl::Optional, llvm::cl::init(true));
llvm::cl::list<std::string> input_filename(llvm::cl::Positional,
                                           llvm::cl::ZeroOrMore,
                                           llvm::cl::desc("<bitcode file>"),
                                           llvm::cl::cat(tpde_category));
llvm::cl::opt<bool>
    dumpIR("dump-ir",
           llvm::cl::desc("Dump llvm-IR and Machine-IR to stdout"),
           llvm::cl::cat(tpde_category));
} // namespace tpde_encgen

int main(const int argc, char *argv[]) {
    using namespace tpde_encgen;

    llvm::cl::HideUnrelatedOptions({&tpde_category});
    llvm::cl::ParseCommandLineOptions(argc, argv);

    // TODO(ts): just replace the argument parsing with args since it flies
    // around anyways?

    // Modify our input file option so that it is no longer
    // required If we wouldn't do this, the option parsing below would fail,
    // since llvm still parses our own options registered above
    input_filename.setNumOccurrencesFlag(llvm::cl::Optional);

    // Set command line options for llvm, so that it stops before the phi
    // elimination
    {
        llvm::SmallVector<const char *> opts;
        llvm::BumpPtrAllocator          allocator;
        llvm::StringSaver               saver(allocator);
        opts.push_back(saver.save("").data());
        llvm::cl::TokenizeGNUCommandLine(
            "-stop-after=unpack-mi-bundles", saver, opts);
        llvm::cl::ParseCommandLineOptions(opts.size(), opts.data());
    }

    auto  context = std::make_unique<llvm::LLVMContext>();
    auto  modules = std::vector<std::unique_ptr<llvm::Module>>{};

    // open and parse a bitcode file into an llvm-module
    const auto parse_mod =
        [&modules,
         &context](std::unique_ptr<llvm::MemoryBuffer> &&bitcode_buf) {
            auto mod = std::unique_ptr<llvm::Module>{};
            if (input_is_bitcode) {
                if (auto E = llvm::parseBitcodeFile(*bitcode_buf, *context)
                                 .moveInto(mod)) {
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
                        std::cerr << "Invalid LLVM module supplied:\n"
                                  << buf << '\n';
                        return 1;
                    }
                }
            }
            modules.push_back(std::move(mod));
            return 0;
        };

    std::unique_ptr<llvm::MemoryBuffer> bitcode_buf;
    if (!input_filename.empty()) {
        for (auto &file : input_filename) {
            auto bitcode = llvm::MemoryBuffer::getFile(file);
            if (!bitcode) {
                std::cerr << std::format("Failed to read bitcode file: '{}'\n",
                                         bitcode.getError().message());
                return 1;
            }

            bitcode_buf.swap(bitcode.get());
            const auto res = parse_mod(std::move(bitcode_buf));
            if (res) {
                return res;
            }
        }
    } else {
        auto bitcode = llvm::MemoryBuffer::getSTDIN();
        if (!bitcode) {
            std::cerr << std::format("Failed to read bitcode file: '{}'\n",
                                     bitcode.getError().message());
            return 1;
        }

        bitcode_buf.swap(bitcode.get());
        parse_mod(std::move(bitcode_buf));
    }

    // TODO verify these options
    llvm::CodeModel::Model cm{llvm::CodeModel::Small};
    llvm::Reloc::Model     rm{llvm::Reloc::Static};
    const int              opt_level{3};

    assert(!modules.empty());
    llvm::StringRef triple = modules[0]->getTargetTriple();
    llvm::Triple    the_triple(triple);

    // Required so that our target triple is actually found
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();


    std::string         error;
    const llvm::Target *target =
        llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        std::cerr << std::format("could not get target: {}\n", error);
        return 1;
    }

    llvm::TargetOptions                  target_options{};
    std::unique_ptr<llvm::TargetMachine> tm{target->createTargetMachine(
        /*TT=*/triple,
        /*CPU=*/"",
        /*Features=*/"",
        /*Options=*/target_options,
        /*RelocModel=*/rm,
        /*CodeModel=*/cm,
#if LLVM_VERSION_MAJOR < 18
        /*OptLevel=*/static_cast<llvm::CodeGenOpt::Level>(unsigned(optLevel)),
#else
        /*OptLevel=*/
        llvm::CodeGenOpt::getLevel(opt_level).value_or(
            llvm::CodeGenOptLevel::Default),
#endif
        /*JIT=*/true)};


    auto *target_machine = static_cast<llvm::LLVMTargetMachine *>(tm.get());
    // If we use a unique_ptr, we get a Segfault when destructing it. So, we
    // leave it at that. (Memory leaks anyone? :eyes:)
    auto *MMIWP = new llvm::MachineModuleInfoWrapperPass{target_machine};

    // Now, we basically do what is done in addPassesToGenerateCode
    // (https://llvm.org/doxygen/LLVMTargetMachine_8cpp_source.html#l00112)
    auto  pass_manager = std::make_unique<llvm::legacy::PassManager>();
    auto *pass_config  = target_machine->createPassConfig(*pass_manager);
    // Set PassConfig options provided by TargetMachine.
    pass_config->setDisableVerify(false);
    pass_manager->add(pass_config);
    pass_manager->add(MMIWP);

    if (pass_config->addISelPasses()) {
        std::cerr << "Failed to add ISel Passes\n";
        return 1;
    }
    pass_config->addMachinePasses();
    pass_config->setInitialized();

    /*
    ** Set up our output file
    */
    const auto output_view = std::string_view{output_filename};
    auto       output_file = std::ofstream{output_filename};
    if (!output_view.ends_with(".h") && !output_view.ends_with(".hpp")) {
        std::cerr << "WARN: output file extension is not a C++ header file (.h "
                     "or .hpp)\n";
    }

    // separate the declarations from the encode implementations
    std::string decl_lines{}, impl_lines{}, sym_lines{};

    const auto compile_mod = [&](llvm::Module &mod) {
        // TODO(ts): switch all functions to regcall so that the code does not
        // try to spill for the calling convention when it wouldn't have to in
        // the generated code
        const auto regcall_prefix = std::string_view{"__regcall3__"};
        for (llvm::Function &f : mod) {
            const auto func_name = f.getName().str();
            if (func_name.starts_with(regcall_prefix)) {
                f.setName(func_name.substr(regcall_prefix.size()));
            } else {
                // AE: we can surely live without this warning.
                // // TODO(ts): sometimes you seem to need it (e.g. when passing
                // // struct {char a,b,c;} since that seems to get treated
                // // differently) so add something to suppress that warning
                // std::cerr << std::format(
                //     "WARN: function {} does not seem to use the regcall "
                //     "calling "
                //     "convention "
                //     "(name not prefixed with \"__regcall3__\"), though "
                //     "its use is highly recommended.\n",
                //     func_name);
            }
        }

        // TODO(ts): add ability to set CPU features here?

        if (dumpIR) {
            mod.print(llvm::outs(), nullptr);
        }

        // Everything is set up, now run all passes
        pass_manager->run(mod);

        // Get the MVachineModule and the MachineFunction for our input
        llvm::MachineModuleInfo &MMI = MMIWP->getMMI();


        // For every input function, query the resulting MachineFunction
        for (llvm::Function &fn : mod) {
            std::string              tmp{};
            llvm::raw_string_ostream os{tmp};
            fn.printAsOperand(os);
            std::cerr << std::format(
                "Handling function {}: {}\n", fn.getName().str(), tmp);

            if (fn.isIntrinsic() || fn.isDeclaration()) {
                std::cerr << std::format(
                    "WARN: Intrinsic function {} was skipped\n",
                    fn.getName().str());
                continue;
            }
            llvm::MachineFunction *machine_func =
                &MMI.getOrCreateMachineFunction(fn);
            if (dumpIR) {
                machine_func->print(llvm::outs(), nullptr);
            }

            // auto live_intervals = std::make_unique<llvm::LiveIntervals>();
            // live_intervals->runOnMachineFunction(*machine_func);

            if (!create_encode_function(machine_func,
                                        fn.getName(),
                                        decl_lines,
                                        sym_lines,
                                        impl_lines)) {
                std::cerr << std::format(
                    "Failed to generate code for function {}\n",
                    fn.getName().str());
                return 1;
            }
        }

        return 0;
    };

    for (auto &mod : modules) {
        const auto res = compile_mod(*mod);
        if (res) {
            return res;
        }
    }

    // TODO(ae): make code nicer
    switch (the_triple.getArch()) {
    case llvm::Triple::x86_64:
        output_file << x64::ENCODER_TEMPLATE_BEGIN << '\n';
        output_file << decl_lines << '\n';
        output_file << '\n' << sym_lines << '\n';
        output_file << x64::ENCODER_TEMPLATE_END << '\n';
        output_file << x64::ENCODER_IMPL_TEMPLATE_BEGIN << '\n';
        output_file << impl_lines << '\n';
        output_file << x64::ENCODER_IMPL_TEMPLATE_END;
        break;
    case llvm::Triple::aarch64:
        output_file << arm64::ENCODER_TEMPLATE_BEGIN << '\n';
        output_file << decl_lines << '\n';
        output_file << '\n' << sym_lines << '\n';
        output_file << arm64::ENCODER_TEMPLATE_END << '\n';
        output_file << arm64::ENCODER_IMPL_TEMPLATE_BEGIN << '\n';
        output_file << impl_lines << '\n';
        output_file << arm64::ENCODER_IMPL_TEMPLATE_END;
        break;
    default: assert(false && "unsupported target"); abort();
    }

    // delete tm;

    return 0;
}
