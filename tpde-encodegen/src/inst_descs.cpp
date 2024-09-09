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

    // MOVSX/MOVZX require special handling, because they also state the
    // parameter sizes
    bool is_mov_with_extension = info.fadecName.starts_with("MOVSX")
                                 || info.fadecName.starts_with("MOVZX");

    for (unsigned idx = 0; idx < info.numOps; ++idx) {
        auto &op = info.ops[idx];

        desc.operands[idx].llvm_idx = op.opIndex;
        // for now just take what's in the MachineInstr
        if (llvm_desc.operands()[op.opIndex].OperandType
            == llvm::MCOI::OPERAND_REGISTER) {
            assert(op.supportsReg());
            if (!info.isFullName) {
                desc.name_fadec += 'r';
                if (is_mov_with_extension) {
                    desc.name_fadec += std::format("{}", op.opSize);
                }
            }
            desc.operands[idx].type = InstDesc::OP_REG;
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
