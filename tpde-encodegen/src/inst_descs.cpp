// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "inst_descs.hpp"
#include "fadecBridge.hpp"

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

    for (unsigned idx = 0; idx < info.numOps; ++idx) {
        auto &op = info.ops[idx];

        // for now just take what's in the MachineInstr
        if (llvm_desc.operands()[op.opIndex].OperandType
            == llvm::MCOI::OPERAND_REGISTER) {
            assert(op.supportsReg());
            if (!info.isFullName) {
                desc.name_fadec += 'r';
            }
            desc.op_types[idx] = InstDesc::OP_REG;
            continue;
        }

        if (llvm_desc.operands()[op.opIndex].OperandType
            == llvm::MCOI::OPERAND_MEMORY) {
            assert(op.supportsMem());
            if (!info.isFullName) {
                desc.name_fadec += 'm';
            }
            desc.op_types[idx] = InstDesc::OP_MEM;
            continue;
        }

        if (llvm_desc.operands()[op.opIndex].OperandType
            == llvm::MCOI::OPERAND_IMMEDIATE) {
            assert(op.supportsImm());
            if (!info.isFullName) {
                desc.name_fadec += 'i';
            }
            desc.op_types[idx] = InstDesc::OP_IMM;
            continue;
        }

        assert(0);
        return false;
    }

    return true;
}
