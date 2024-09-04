// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <format>
#include <iostream>
#include <string>
#include <unordered_set>

#include <llvm/CodeGen/LivePhysRegs.h>
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
    virtual unsigned         reg_bank(unsigned id)                = 0;
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

    unsigned reg_bank(const unsigned id) override {
        if (id < 0x10) {
            return 0;
        } else if (id >= 0x20 && id <= 0x40) {
            return 1;
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

    TYPE                         ty;
    std::string                  operand_name;
    unsigned                     alias_reg_id;
    // llvm::MachineRegisterInfo::reg_instr_nodbg_iterator cur_def_use_it;
    // registers aliased to this value
    std::unordered_set<unsigned> aliased_regs = {};

    bool is_dead           = false;
    // scratch register state
    bool scratch_allocated = false, scratch_destroyed = false;
};

struct GenerationState {
    llvm::MachineFunction              *func;
    EncodingTarget                     *enc_target;
    std::vector<std::string>            param_names{};
    std::unordered_set<unsigned>        used_regs{};
    llvm::DenseMap<unsigned, ValueInfo> value_map{};
    int                                 num_ret_regs = -1;
    std::vector<unsigned>               return_regs  = {};
};

bool handle_move(std::string        &buf,
                 GenerationState    &state,
                 llvm::MachineInstr *inst);
bool handle_terminator(std::string        &buf,
                       GenerationState    &state,
                       llvm::MachineInstr *inst);

bool generate_inst(std::string        &buf,
                   GenerationState    &state,
                   llvm::MachineInstr *inst) {
    (void)buf;
    (void)state;

    // check if this is a move
    if (inst->isMoveReg()) {
        if (!inst->hasImplicitDef()) {
            // move without side-effects, e.g. zero-extension
            return handle_move(buf, state, inst);
        }
    }

    if (inst->isTerminator()) {
        return handle_terminator(buf, state, inst);
    }

    if (inst->isPseudo()) {
        if (inst->isKill()) {
            // TODO(ts): special handling
            assert(0);
        } else {
            std::string              buf{};
            llvm::raw_string_ostream os(buf);
            inst->print(os);
            std::cerr << std::format(
                "ERROR: Encountered unknown instruction '{}'\n", buf);
            assert(0);
            return false;
        }
    }

    // TODO(ts): lookup instruction desc

    // TODO(ts): check preferred encodings

    // TODO(ts): generate operation

    // TODO(ts): check killed operands

    return true;
}

bool handle_move(std::string        &buf,
                 GenerationState    &state,
                 llvm::MachineInstr *inst) {
    assert(inst->getNumExplicitOperands() == 2);
    const auto *src_op = &*inst->all_uses().begin();
    assert(src_op->getType() == llvm::MachineOperand::MO_Register);
    const auto src_reg = src_op->getReg();
    assert(src_reg.isPhysical());

    assert(inst->getNumExplicitDefs() == 1);
    const auto dst_reg = inst->defs().begin()->getReg();
    assert(dst_reg.isPhysical());

    const auto src_id   = state.enc_target->reg_id_from_mc_reg(src_reg);
    const auto dst_id   = state.enc_target->reg_id_from_mc_reg(dst_reg);
    const auto src_name = state.enc_target->reg_name_lower(src_id);
    const auto dst_name = state.enc_target->reg_name_lower(dst_id);

    assert(state.value_map.contains(src_id));
    if (state.value_map[src_id].is_dead) {
        std::cerr << std::format("ERROR: dead register {} used in move\n",
                                 src_name);
        return false;
    }

    // TODO(ts): src_id == dst_id?
    if (src_reg == dst_reg) {
        // this is a no-op
        buf += std::format("    // no-op\n");
        return true;
    }

    // This is a register move without any intended side-effects,
    // e.g. zero-extension so we can alias it for now
    buf += std::format("    // aliasing {} to {}\n", dst_name, src_name);
    state.value_map[src_id].aliased_regs.insert(dst_id);

    // TODO(ts): cannot do this when we are outside of the entry-block
    if (src_op->isKill()) {
#if 0
                // no need to alias, we can simply move the register
                // TODO(ts): we cannot do this if dst_id is used as a fixed register somewhere
                // as we free up the allocation then

                // TODO(ts): do we want this as the optimizer might not optimize out the move
                buf += std::format("    // source {} is killed so move to {}\n", src_name, dst_name);
                buf += std::format("    scratch_{} = std::move(scratch_{})\n", dst_name, src_name);

                state.value_map[src_id].was_destroyed = true;
                state.value_map[src_id].is_dead = true;
#else
        buf += std::format("    // source {} is killed and marked as "
                           "dead but not yet destroyed\n",
                           src_name);
        state.value_map[src_id].is_dead = true;
#endif
    }

    auto &dst_info        = state.value_map[dst_id];
    dst_info.ty           = ValueInfo::REG_ALIAS;
    dst_info.alias_reg_id = src_id;
    dst_info.is_dead      = false;
    return true;
}

static unsigned reg_size_bytes(llvm::MachineFunction *func,
                               llvm::Register         reg) {
    const auto &target_reg_info = func->getSubtarget().getRegisterInfo();
    const auto &mach_reg_info   = func->getRegInfo();
    return target_reg_info->getRegSizeInBits(reg, mach_reg_info) / 8;
}

bool handle_terminator(std::string        &buf,
                       GenerationState    &state,
                       llvm::MachineInstr *inst) {
    if (inst->isReturn()) {
        // record the number of return regs
        {
            unsigned              num_ret_regs = 0;
            std::vector<unsigned> ret_regs     = {};
            for (const auto &mach_op : inst->explicit_uses()) {
                assert(mach_op.getType() == llvm::MachineOperand::MO_Register);
                const auto reg = mach_op.getReg();
                assert(reg.isPhysical());
                const auto reg_id = state.enc_target->reg_id_from_mc_reg(reg);
                if (state.num_ret_regs == -1) {
                    ret_regs.push_back(reg_id);
                } else {
                    if (state.return_regs.size() <= num_ret_regs
                        || state.return_regs[num_ret_regs] != reg_id) {
                        std::cerr << std::format(
                            "ERROR: Found mismatching return with register "
                            "{}({})",
                            reg_id,
                            state.enc_target->reg_name_lower(reg_id));
                        return false;
                    }
                }
                ++num_ret_regs;
            }
            if (state.num_ret_regs == -1) {
                state.num_ret_regs = static_cast<int>(num_ret_regs);
                state.return_regs  = std::move(ret_regs);
            }
        }

        // if there is only one block, we can return from the encoding function
        // otherwise we need to buffer the return until the end
        if (state.func->size() == 1) {
            for (int idx = 0; idx < state.num_ret_regs; ++idx) {
                const auto reg = state.return_regs[idx];
                buf += std::format("    // returning reg {} as result_{}\n",
                                   state.enc_target->reg_name_lower(reg),
                                   idx);
                assert(state.value_map.contains(reg));
                auto  cur_reg = reg;
                auto *info    = &state.value_map[reg];
                while (info->ty == ValueInfo::REG_ALIAS) {
                    buf += std::format(
                        "    // {} is an alias for {}\n",
                        state.enc_target->reg_name_lower(cur_reg),
                        state.enc_target->reg_name_lower(info->alias_reg_id));
                    cur_reg = info->alias_reg_id;
                    info    = &state.value_map[cur_reg];
                }

                if (info->ty == ValueInfo::ASM_OPERAND) {
                    buf +=
                        std::format("    // {} is an alias for {}\n",
                                    state.enc_target->reg_name_lower(cur_reg),
                                    info->operand_name);

                    unsigned tmp_idx = idx;
                    for (auto &op : inst->explicit_uses()) {
                        if (tmp_idx > 0) {
                            --tmp_idx;
                            continue;
                        }

                        buf += std::format(
                            "    {}.try_salvage_or_materialize(this, "
                            "result_{}, {}, {});\n",
                            info->operand_name,
                            idx,
                            state.enc_target->reg_bank(cur_reg),
                            reg_size_bytes(state.func, op.getReg()));
                    }
                } else {
                    assert(info->ty == ValueInfo::SCRATCHREG);
                    buf +=
                        std::format("    result_{} = std::move(scratch_{});\n",
                                    idx,
                                    state.enc_target->reg_name_lower(cur_reg));
                    // no more housekeeping needed
                }
            }
            return true;
        } else {
            // we need to move into the result scratch regs (they should be
            // allocated by now)
            return false;
        }
    }

    std::string              tmp;
    llvm::raw_string_ostream os(tmp);
    inst->print(os);

    std::cerr << std::format("ERROR: Encountered unknown terminator {}\n", tmp);
    return false;
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

    // update dead/kill flags since they might not be very accurate anymore
    // NOTE: we assume that the MBB liveins are accurate though
    for (auto &MBB : *func) {
        llvm::recomputeLivenessFlags(MBB);
    }

    // write MachineIR as comment at the start of the function
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

// TODO(ts): this can't work if a IR value corresponds to multiple registers
// so we should parse debug info but that does not always exist
#if 0
    // try to get the names for the IR arguments
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
#endif


    auto &mach_reg_info = func->getRegInfo();
    // const auto &target_reg_info = func->getSubtarget().getRegisterInfo();


    // map inputs
    {
        unsigned idx = 0;
        for (auto it  = mach_reg_info.livein_begin(),
                  end = mach_reg_info.livein_end();
             it != end;
             ++it) {
            auto name = std::format("param_{}", state.param_names.size());
            state.param_names.push_back(std::move(name));

            const auto reg_id = enc_target->reg_id_from_mc_reg(it->first);
            assert(!state.used_regs.contains(reg_id));
            state.used_regs.insert(reg_id);
            state.value_map[reg_id] = ValueInfo{
                .ty           = ValueInfo::ASM_OPERAND,
                .operand_name = state.param_names[idx++],
                // .cur_def_use_it =
                //    mach_reg_info.reg_instr_nodbg_begin(it->first)
            };

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

                if (!generate_inst(write_buf_inner, state, inst)) {
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

    if (state.num_ret_regs == -1) {
        // TODO(ts): might mean no return
        std::cerr
            << "ERROR: number of return registers not set at end of function\n";
        return false;
    }

    for (int i = 0; i < state.num_ret_regs; ++i) {
        if (!first) {
            std::format_to(std::back_inserter(impl_lines), ", ");
        } else {
            first = false;
        }
        std::format_to(
            std::back_inserter(impl_lines), "ScratchReg &result_{}", i);
    }

    impl_lines += ") {\n";

    impl_lines += write_buf;
    impl_lines += write_buf_inner;

    impl_lines += "\n}\n\n";

    return true;
}
} // namespace tpde_encgen
