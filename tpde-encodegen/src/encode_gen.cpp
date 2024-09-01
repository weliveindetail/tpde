// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <format>
#include <iostream>
#include <string>
#include <unordered_set>

#include <llvm/CodeGen/MachineRegisterInfo.h>

#include "encode_gen.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>

namespace {

struct EncodingTarget {
    llvm::MachineFunction *func;

    explicit EncodingTarget(llvm::MachineFunction *func) : func(func) {}

    virtual ~EncodingTarget() = default;

    virtual unsigned         reg_id_from_mc_reg(llvm::MCRegister) = 0;
    virtual std::string_view reg_name_lower(unsigned id)          = 0;
};

struct EncodingTargetX64 : EncodingTarget {
    explicit EncodingTargetX64(llvm::MachineFunction *func)
        : EncodingTarget(func) {}

    bool reg_is_gp(const llvm::Register reg) const {
        const auto *target_reg_info = func->getSubtarget().getRegisterInfo();

        if (!reg.isPhysical()) {
            assert(0);
            std::cerr << "ERROR: encountered non-physical register\n";
            exit(1);
        }
        return target_reg_info->isGeneralPurposeRegister(*func, reg);
    }

    unsigned reg_id_from_mc_reg(const llvm::MCRegister reg) override {
        return func->getSubtarget().getRegisterInfo()->getEncodingValue(reg)
               + (reg_is_gp(reg) ? 0 : 0x20);
    }

    std::string_view reg_name_lower(unsigned id) override {
        std::string_view gp_names[] = {
            "ax",
            "cx",
            "dx",
            "bx",
            "sp",
            "bp",
            "si",
            "di",
            "r8",
            "r9",
            "r10",
            "r11",
            "r12",
            "r13",
            "r14",
            "r15",
        };
        std::string_view xmm_names[] = {
            "xmm0",  "xmm1",  "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",
            "xmm7",  "xmm8",  "xmm9",  "xmm10", "xmm11", "xmm12", "xmm13",
            "xmm14", "xmm15", "xmm16", "xmm17", "xmm18", "xmm19", "xmm20",
            "xmm21", "xmm22", "xmm23", "xmm24", "xmm25", "xmm26", "xmm27",
            "xmm28", "xmm29", "xmm30", "xmm31",
        };

        if (id < 0x10) {
            return gp_names[id];
        } else if (id >= 0x20 && id <= 0x40) {
            return xmm_names[id - 0x20];
        } else {
            assert(0);
            std::cerr << std::format("Unknown register {}\n", id);
            exit(1);
        }
    }
};

struct ValueInfo {
    enum TYPE {
        SCRATCHREG,
        ASM_OPERAND,
        REG_ALIAS
    };

    TYPE                                                ty;
    std::string                                         operand_name;
    unsigned                                            alias_reg_id;
    llvm::MachineRegisterInfo::reg_instr_nodbg_iterator cur_def_use_it;
};

struct GenerationState {
    llvm::MachineFunction              *func;
    EncodingTarget                     *enc_target;
    std::vector<std::string>            param_names{};
    std::unordered_set<unsigned>        used_regs{};
    llvm::DenseMap<unsigned, ValueInfo> value_map{};
};

bool generate_inst(std::string &buf, GenerationState &state) {
    (void)buf;
    (void)state;
    return true;
}

} // namespace

namespace tpde_encgen {
bool create_encode_function(llvm::MachineFunction *func,
                            std::string_view       name,
                            std::string           &decl_lines,
                            std::string           &impl_lines) {
    (void)func;
    (void)name;
    (void)decl_lines;
    (void)impl_lines;

    std::string write_buf{};

    {
        std::string              tmp;
        llvm::raw_string_ostream os(tmp);
        func->print(os);

        auto view = std::string_view{tmp};
        while (!view.empty()) {
            write_buf += "    // ";

            const auto pos = view.find('\n');
            if (pos == std::string_view::npos) {
                write_buf += view;
            } else {
                write_buf += view.substr(0, pos);
                view       = view.substr(pos + 1);
            }
            write_buf += '\n';
        }
        write_buf += '\n';
    }

    // TODO(ts): parameterize
    auto            enc_target = std::make_unique<EncodingTargetX64>(func);
    GenerationState state{.func = func, .enc_target = enc_target.get()};

    auto &llvm_func = func->getFunction();
    for (llvm::Argument &arg : llvm_func.args()) {
        if (arg.hasName() && !arg.getName().empty()) {
            if (std::find(state.param_names.begin(),
                          state.param_names.end(),
                          arg.getName())
                != state.param_names.end()) {
                std::cerr << std::format(
                    "ERROR: argument '{}' already exists in function '{}'!\n",
                    arg.getName().str(),
                    llvm_func.getName().str());
                return false;
            }
            state.param_names.push_back(arg.getName().str());
        } else {
            auto name = std::format("param_{}", state.param_names.size());
            if (std::find(
                    state.param_names.begin(), state.param_names.end(), name)
                != state.param_names.end()) {
                std::cerr << std::format(
                    "ERROR: argument '{}' already exists in function '{}'!\n",
                    name,
                    llvm_func.getName().str());
                return false;
            }
            state.param_names.push_back(std::move(name));
        }
    }


    auto &mach_reg_info = func->getRegInfo();
    // const auto &target_reg_info = func->getSubtarget().getRegisterInfo();


    // map inputs
    {
        unsigned idx = 0;
        for (auto it  = mach_reg_info.livein_begin(),
                  end = mach_reg_info.livein_end();
             it != end;
             ++it) {
            const auto reg_id = enc_target->reg_id_from_mc_reg(it->first);
            assert(!state.used_regs.contains(reg_id));
            state.used_regs.insert(reg_id);
            state.value_map[reg_id] =
                ValueInfo{.ty           = ValueInfo::ASM_OPERAND,
                          .operand_name = state.param_names[idx++],
                          .cur_def_use_it =
                              mach_reg_info.reg_instr_nodbg_begin(it->first)};

            std::format_to(std::back_inserter(write_buf),
                           "    // Mapping {} to {}\n",
                           enc_target->reg_name_lower(reg_id),
                           state.value_map[reg_id].operand_name);
        }
    }

    std::string write_buf_inner{};
    {
        for (auto bb_it = func->begin(); bb_it != func->end(); ++bb_it) {
            for (auto inst_it = bb_it->begin(); inst_it != bb_it->end();
                 ++inst_it) {
                llvm::MachineInstr *inst = &(*inst_it);
                if (inst->isDebugInstr()) {
                    continue;
                }


                std::string              tmp;
                llvm::raw_string_ostream os(tmp);
                inst->print(os, true, false, true, true);
                write_buf_inner += "\n\n    // ";
                write_buf_inner += tmp;
                write_buf_inner += "\n";
                continue;

                if (inst->isTerminator()) {
                    // assert(0);
                    // TODO(ts): special handling for return and branches
                }

                if (!generate_inst(write_buf_inner, state)) {
                    return false;
                }
            }
        }
    }


    // finish up function header
    {
        std::format_to(
            std::back_inserter(impl_lines),
            "template <typename Adaptor,\n"
            "          typename Derived,\n"
            "          template <typename, typename, typename>\n"
            "          class BaseTy>\n"
            "void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_{}(",
            name);
    }

    auto first = true;
    for (auto &param : state.param_names) {
        if (!first) {
            std::format_to(std::back_inserter(impl_lines), ", ");
        } else {
            first = false;
        }
        std::format_to(std::back_inserter(impl_lines), "AsmOperand {}", param);
    }
    impl_lines += ") {\n";

    impl_lines += write_buf;
    impl_lines += write_buf_inner;

    impl_lines += "\n}\n\n";

    return true;
}
} // namespace tpde_encgen
