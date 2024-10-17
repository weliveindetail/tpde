// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "llvmUtils.hpp"

#include <cctype>
#include <format>
#include <iostream>

#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/CodeGen/TargetInstrInfo.h>
#include <llvm/MC/MCInstrInfo.h>

namespace llvmUtils {
std::array<const char *, 16> condCodes = {"O",
                                          "NO",
                                          "C",
                                          "NC",
                                          "Z",
                                          "NZ",
                                          "BE",
                                          "A",
                                          "S",
                                          "NS",
                                          "P",
                                          "NP",
                                          "L",
                                          "GE",
                                          "LE",
                                          "G"};

int getReturnedRegisterIndex(const llvm::Register        &reg,
                             const llvm::MachineFunction *machineFunction) {
    for (llvm::MachineInstr &instr :
         machineFunction->getRegInfo().use_instructions(reg)) {
        llvm::StringRef instName =
            machineFunction->getSubtarget().getInstrInfo()->getName(
                instr.getOpcode());
        if (instName == "RET64") {
            for (unsigned i = 0; i < instr.getNumOperands(); i++) {
                if (reg == instr.getOperand(i).getReg()) {
                    return i;
                }
            }
        }
    }
    return -1;
}

bool hasRegSize(std::string_view regName) {
    if (regName == "EFLAGS" || regName == "MXCSR" || regName == "FPCW"
        || regName == "SSP") {
        return false;
    }
    return true;
}

unsigned getRegSize(llvm::Register               reg,
                    const llvm::MachineFunction *machineFunction) {
    return machineFunction->getSubtarget().getRegisterInfo()->getRegSizeInBits(
               reg, machineFunction->getRegInfo())
           / 8;
}

bool isGPReg(llvm::Register reg, const llvm::MachineFunction *machineFunction) {
    // const llvm::MachineRegisterInfo &machineRegInfo =
    //     machineFunction->getRegInfo();
    const llvm::TargetRegisterInfo *targetRegInfo =
        machineFunction->getSubtarget().getRegisterInfo();

    return targetRegInfo->isGeneralPurposeRegister(*machineFunction, reg);
}

std::string getRegEnumString(unsigned regID, bool withRegName) {
    if (withRegName) {
        return std::format("Derived::RegisterFile::{}", getRegEnumName(regID));
    } else {
        return std::format("Derived::RegisterFile::AL + {}", regID);
    }
}

std::string getRegEnumString(llvm::Register                 &reg,
                             const llvm::TargetRegisterInfo *targetRegisterInfo,
                             bool                            withRegName) {
    return getRegEnumString(targetRegisterInfo->getEncodingValue(reg.asMCReg()),
                            withRegName);
}

std::array<std::string, 16>  regEnumNames{"AX",
                                         "CX",
                                         "DX",
                                         "BX",
                                         "SP",
                                         "BP",
                                         "SI",
                                         "DI",
                                         "R8",
                                         "R9",
                                         "R10",
                                         "R11",
                                         "R12",
                                         "R13",
                                         "R14",
                                         "R15"};
std::array<const char *, 16> xmmRegEnumNames{"XMM0",
                                             "XMM1",
                                             "XMM2",
                                             "XMM3",
                                             "XMM4",
                                             "XMM5",
                                             "XMM6",
                                             "XMM7",
                                             "XMM8",
                                             "XMM9",
                                             "XMM10",
                                             "XMM11",
                                             "XMM12",
                                             "XMM13",
                                             "XMM14",
                                             "XMM15"};

std::string getRegEnumName(unsigned regID) {
    if (regID < 0x10) {
        return regEnumNames[regID];
    } else if (regID >= 0x20 && regID < 0x30) {
        return xmmRegEnumNames[regID - 0x20];
    } else {
        assert(false && "Invalid regID");
        return "???";
    }
}

} // namespace llvmUtils
