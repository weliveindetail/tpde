// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <format>

#include "fadecBridge.hpp"
#include "inst_descs.hpp"

#include <llvm/MC/MCInstrInfo.h>
#include <llvm/Target/TargetMachine.h>

using namespace tpde_encgen;

// TODO(ts): add manual replacements for SSE_MOVQ_X2Grr -> MOV64rm,
// SSE_MOVQ_G2Xrr -> MOV64rm, etc
bool tpde_encgen::get_inst_def(llvm::MachineInstr &inst, InstDesc &desc) {
    InstInfo info;
    if (!llvmUtils::deriveInstInfo(
            inst.getOpcode(), inst.getMF(), info, true)) {
        return false;
    }

    desc.commutable = info.commutable;
    desc.name_fadec = info.fadecName;

    const llvm::MCInstrDesc &llvm_desc =
        inst.getMF()->getTarget().getMCInstrInfo()->get(inst.getOpcode());

    const auto add_op_ty_name = [&desc](const std::string_view str) {
        desc.name_fadec += str;
        for (auto &replacement : desc.preferred_encodings) {
            replacement.replacement->name_fadec += str;
        }
    };

    // MOVSX/MOVZX require special handling, because they also state the
    // parameter sizes
    bool is_mov_with_extension = info.fadecName.starts_with("MOVSX")
                                 || info.fadecName.starts_with("MOVZX");

    const auto imm_cond = [](const OpType ty) {
        switch (ty) {
        case IMM64: return InstDesc::PreferredEncoding::COND_IMM64;
        case IMM32: return InstDesc::PreferredEncoding::COND_IMM32;
        case IMM16: return InstDesc::PreferredEncoding::COND_IMM16;
        case IMM8: return InstDesc::PreferredEncoding::COND_IMM8;
        default: assert(0); exit(1);
        }
    };

    const auto create_imm_replacement = [&desc, &inst, &info, &imm_cond](
                                            const std::string &old_fadec_name,
                                            const unsigned     idx,
                                            OpSupports        &op) {
        if (!inst.getOperand(op.opIndex).isDef()) {
            if (op.supportsImm()) {
                assert(!info.isFullName);
                auto replacement = std::make_shared<InstDesc>(desc);
                replacement->operands[idx].type  = InstDesc::OP_IMM;
                replacement->name_fadec          = old_fadec_name;
                replacement->name_fadec         += 'i';
                desc.preferred_encodings.push_back(InstDesc::PreferredEncoding{
                    .target = InstDesc::PreferredEncoding::TARGET_USE,
                    .target_def_use_idx = op.opIndex,
                    .cond               = imm_cond(op.getLargestImmOpType()),
                    .replacement        = std::move(replacement)});
            }
        }
    };

    const auto create_mem_replacement = [&desc,
                                         &inst,
                                         &info,
                                         is_mov_with_extension](
                                            const std::string &old_fadec_name,
                                            const unsigned     idx,
                                            OpSupports        &op) {
        if (!inst.getOperand(op.opIndex).isDef()) {
            if (op.supportsMem()) {
                assert(!info.isFullName);
                auto replacement = std::make_shared<InstDesc>(desc);
                replacement->operands[idx].type  = InstDesc::OP_MEM;
                replacement->name_fadec          = old_fadec_name;
                replacement->name_fadec         += 'm';
                if (is_mov_with_extension) {
                    replacement->name_fadec += std::format("{}", op.opSize);
                }
                desc.preferred_encodings.push_back(InstDesc::PreferredEncoding{
                    .target = InstDesc::PreferredEncoding::TARGET_USE,
                    .target_def_use_idx = op.opIndex,
                    .cond               = InstDesc::PreferredEncoding::COND_MEM,
                    .replacement        = std::move(replacement)});
            }
        }
    };

    for (auto &op : desc.operands) {
        op.type = InstDesc::OP_NONE;
    }

    for (unsigned idx = 0; idx < info.numOps; ++idx) {
        auto &op = info.ops[idx];

        desc.operands[idx].llvm_idx = op.opIndex;

        const auto old_fadec_name = desc.name_fadec;

        auto force_is_reg = false;
        if (op.opIndex > inst.getNumExplicitOperands()) {
            // this operand is an implicit operand
            assert(inst.getOperand(op.opIndex).isReg());
            if (!inst.getOperand(op.opIndex).isImplicit()) {
                assert(0);
                return false;
            }
            force_is_reg = true;
        }

        // for now just take what's in the MachineInstr
        if (force_is_reg
            || llvm_desc.operands()[op.opIndex].OperandType
                   == llvm::MCOI::OPERAND_REGISTER) {
            assert(op.supportsReg());

            if (!info.isFullName) {
                add_op_ty_name("r");
                if (is_mov_with_extension) {
                    add_op_ty_name(std::format("{}", op.opSize));
                }
            }
            desc.operands[idx].type = InstDesc::OP_REG;

            create_imm_replacement(old_fadec_name, idx, op);
            create_mem_replacement(old_fadec_name, idx, op);

            continue;
        }

        if (llvm_desc.operands()[op.opIndex].OperandType
            == llvm::MCOI::OPERAND_MEMORY) {
            assert(op.supportsMem());
            if (!info.isFullName) {
                desc.name_fadec += 'm';
                if (is_mov_with_extension) {
                    desc.name_fadec += std::format("{}", op.opSize);
                }
            }
            desc.operands[idx].type = InstDesc::OP_MEM;

            create_imm_replacement(old_fadec_name, idx, op);
            continue;
        }

        if (llvm_desc.operands()[op.opIndex].OperandType
            == llvm::MCOI::OPERAND_IMMEDIATE) {
            assert(op.supportsImm());
            if (!info.isFullName) {
                desc.name_fadec += 'i';
                assert(!is_mov_with_extension);
            }
            desc.operands[idx].type = InstDesc::OP_IMM;
            continue;
        }

        assert(0);
        return false;
    }

    return true;
}
