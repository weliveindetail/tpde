// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <string>
#include <llvm/CodeGen/Register.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/CodeGen/TargetRegisterInfo.h>

namespace llvmUtils {
///Maps the condition code number to the condition code string (e.g. 8 => S, 1 => NO)
extern std::array<const char*, 16> condCodes;

//FIXME We can't get the enum value as it is in a private header (X86InstrOperands.td)
const unsigned MAYBE_OPERAND_COND_CODE = 14;

/// Returns the operand index (starting from zero) if the provided register is used in a return statement, -1 otherwise
int getReturnedRegisterIndex(const llvm::Register& reg, const llvm::MachineFunction* machineFunction);

/// Returns true if it is allowed to query the register's size
bool hasRegSize(std::string_view regName);

/// Returns the register's size in Bytes
unsigned getRegSize(llvm::Register reg, const llvm::MachineFunction* machineFunction);

/// Determines if this is a GP register or not (i.e. an XMM-register)
bool isGPReg(llvm::Register reg, const llvm::MachineFunction* machineFunction);

/// Takes an llvm Register and returns the enum string in the form of "Derived::RegisterFile::AX + {index}"
/// If withRegName=true, the string is "Derived::RegisterFile::{regName}"
std::string getRegEnumString(llvm::Register& reg, const llvm::TargetRegisterInfo* targetRegisterInfo, bool withRegName=true);
std::string getRegEnumString(unsigned regID, bool withRegName=true);

/// Takes an llvm Register, and returns the enum name used in tpde/x64/Assembler.hpp
/// Examples:
///  rax => AX
///  r9d => r9
///  cl => CX
///  dx => DX
std::string getRegEnumName(unsigned regID);

}
