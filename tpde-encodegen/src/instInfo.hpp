// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <cstdint>
#include <llvm/MC/MCInstrDesc.h>
#include <string>

/// Used to specify supported operand types. Is also used for bitmaps, hence the
/// values
enum OpType {
    NO_OP  = 0,
    GPREG  = 1 << 0,
    XMMREG = 1 << 1,
    IMM64  = 1 << 2,
    IMM32  = 1 << 3,
    IMM16  = 1 << 4,
    IMM8   = 1 << 5,
    MEM    = 1 << 6,
    ADDR   = 1 << 7,
};

/// Has all bits set that correspond to register opTypes
extern const uint8_t REG;
/// Has all bits set that correspond to immediate opTypes
extern const uint8_t IMM;

/// Returns true if the provided opType is a register
bool isRegOpType(OpType opType);

/// Returns true if the provided opType is an immediate
bool isImmOpType(OpType opType);

/// A struct that manages what an operand supports
/// This is done with a bitmap (see OpType enum for details)
struct OpSupports {
    OpSupports(uint16_t supportMap) : supportMap(supportMap) {};
    OpSupports(uint16_t supportMap, uint8_t opIndex)
        : opIndex(opIndex), supportMap(supportMap) {};
    OpSupports() {};

    /// Set supportMap to NO_OP and clear opIndex and iterator
    void reset();
    void addOpType(OpType opType);

    bool supportsReg();
    bool supportsImm();
    bool supportsMem();
    bool isPhysReg(const llvm::MCInstrDesc &instrDesc);
    bool isNOP();

    uint8_t getNumCases();
    uint8_t getOpIndex();

    /// An iterator-like function that returns the next supported
    /// Operand at each call.
    OpType getNextOpType();

    OpType getRegOpType();

    void print(std::ostream &out);

    /// The operand index in our LLVM MachineInstr
    uint8_t opIndex = 69;
    /// The operand size. For memory operands, this is only set correctly in
    /// MOVSX/MOVZX instructions. Otherwise (and for immediates), this value
    /// should not be used.
    uint8_t opSize;

  private:
    /// A bitmap storing the supported operands
    uint16_t supportMap;
    /// To remember what the last call to getNextOpType returned
    uint8_t  iterator = 0;
};

struct InstInfo {
    uint8_t     numOps;
    OpSupports  ops[4] = {OpSupports()};
    std::string fadecName;
    bool        isFullName;         // If false, only the basename
    bool        commutable = false; // TODO this is never used

    void print(std::ostream &out);
};

/**
 * Returns a string explaining why the instruction is unsupported.
 * If no explanation is available (e.g. instruction is supported, or no reason
 *provided), nullptr is returned
 **/
const char *getUnsupportedReason(std::string              instName,
                                 const llvm::MCInstrDesc &instrDesc);
