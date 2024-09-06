// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once

#include "instInfo.hpp"
#include <llvm/CodeGen/MachineFunction.h>

namespace llvmUtils {

/// Attempts to automatically derive the fadec function and parameter layout
/// from the LLVM MachineInstr that is provided opcode: The opcode of the
/// MachineInstr machineFunction: For access to the MachineRegisterInfo and
/// TargetSubtargetInfo If successful, true is returned, and instInfo is
/// populated with the necessary information to continue. Otherwise, false is
/// returned, and the contents of instInfo are undefined
bool deriveInstInfo(unsigned                     opcode,
                    const llvm::MachineFunction *machineFunction,
                    InstInfo                    &instInfo,
                    bool                         debug = false);

} // namespace llvmUtils
