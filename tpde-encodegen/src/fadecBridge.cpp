// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>

#include <llvm/CodeGen/MachineInstr.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/CodeGen/TargetRegisterInfo.h>
#include <llvm/CodeGen/TargetSubtargetInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCInstrDesc.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegister.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>

#include "fadecInfo.hpp"
#include "instInfo.hpp"
#include "llvmUtils.hpp"

namespace llvmUtils {
/// LLVM instruction prefixes that are also used by fadec
/// => if they are found, keep the prefix
const char *const importantPrefixes[] = {"LOCK_", "MMX_"};

/// Some instructions need a manual mapping. This is done here
llvm::StringMap<InstInfo> manualMappings{
    {"BT64rr",
     InstInfo{2,
     {{GPREG, 0}, {GPREG | IMM8, 1}},
     "BT64",
     false}}, //  BT64mr is NOT a variant => incorrect variant
    {"LEA64r",
     InstInfo{2,
     {{GPREG, 0}, {MEM, 1}},
     "LEA64rm",
     true} }, //  The operand type is UNKNOWN instead of OPERAND_MEMORY
                      // => mapping fails
};

/// Returns all fadec instructions that contain the provided string
std::vector<std::string_view> getAllMatches(const std::string &name) {
    std::vector<std::string_view> res{};
    for (const auto &instr : fadecUtils::functionNames) {
        if (instr.find(name) != std::string::npos) {
            res.push_back(instr);
        }
    }
    return res;
}

/// Checks if a fadec instruction with that exact name exists
int instrLookup(const std::string &name) {
    auto     it    = std::lower_bound(fadecUtils::functionNames.begin(),
                               fadecUtils::functionNames.end(),
                               name);
    unsigned index = std::distance(fadecUtils::functionNames.begin(), it);
    if (it != fadecUtils::functionNames.end()
        && fadecUtils::functionNames.at(index) == name) {
        return index;
    } else {
        return -1;
    }
}

/// Attaches fadec-style parameter characters (e.g. r for register, m for
/// memroy) to an instruction name
void attachParameterChars(std::string                &dest,
                          const llvm::MCInstrDesc    &instrDesc,
                          const llvm::MCRegisterInfo *regInfo,
                          bool                        includeImplicit) {
    if (includeImplicit) {
        for (const llvm::MCPhysReg &reg : instrDesc.implicit_defs()) {
            if (llvmUtils::hasRegSize(regInfo->getName(reg))) {
                dest.append("r");
            }
        }
    }
    for (unsigned i = 0; i < instrDesc.operands().size(); i++) {
        if (instrDesc.getOperandConstraint(i, llvm::MCOI::TIED_TO) != -1) {
            // If it's tied-def, we would be adding a register twice -- skip
            continue;
        }

        const llvm::MCOperandInfo &operandInfo = instrDesc.operands()[i];
        llvm::MCOI::OperandType    opType =
            static_cast<llvm::MCOI::OperandType>(operandInfo.OperandType);
        switch (opType) {
        case llvm::MCOI::OPERAND_REGISTER: dest.append("r"); break;
        case llvm::MCOI::OPERAND_IMMEDIATE: dest.append("i"); break;
        case llvm::MCOI::OPERAND_MEMORY:
            dest.append("m");
            i += 4;
            break;
        default: break;
        }
    }
    if (includeImplicit) {
        for (const llvm::MCPhysReg &reg : instrDesc.implicit_uses()) {
            bool isImplicitDef = false;
            for (const llvm::MCPhysReg &defReg : instrDesc.implicit_defs()) {
                if (defReg == reg) {
                    isImplicitDef = true;
                    break;
                }
            }
            if (llvmUtils::hasRegSize(regInfo->getName(reg))
                && !isImplicitDef) {
                dest.append("r");
            }
        }
    }
}

/// Receives a fadec name and checks how many matching instructions there are
/// Results are dumped to stdout
bool evaluateMatches(const std::string &fadecName) {
    std::cout << fadecName << ": ";
    if (instrLookup(fadecName) != -1) {
        std::cout << "Direct match!";
        std::cout << "\n";
        return true;
    } else {
        auto matches = getAllMatches(fadecName);
        if (matches.size() == 1) {
            std::cout << "Possible match: " << matches[0];
        } else if (matches.size() > 0) {
            std::cout << "Too many matches (" << matches.size() << ")";
        } else {
            std::cout << "No matches";
        }
        std::cout << "\n";
        return false;
    }
}

/// All gathered information that could (when combinated in different ways) be
/// part of a fadec function name
struct FadecName {
    std::string prefix;
    std::string mnemonic;
    std::string size;
    std::string suffix;
    std::string parameterLetters;
    std::string parameterLettersWithImplicitOps;
};

/// All possible ways to derive the fadec function name from the gathered
/// information
enum class DerivationStrategy {
    MNEMONIC_ONLY,
    MNEMONIC_SIZE,
    MNEMONIC_PARAMCHARS,
    FULL_WITHOUT_IMPLICIT,
    FULL,
};

std::string getFadecString(const FadecName   &fadecName,
                           DerivationStrategy strategy) {
    std::string result = fadecName.mnemonic;
    result.insert(0, fadecName.prefix);
    result.append(fadecName.suffix);

    switch (strategy) {
    case DerivationStrategy::MNEMONIC_ONLY: break;
    case DerivationStrategy::MNEMONIC_SIZE:
        result.append(fadecName.size);
        break;
    case DerivationStrategy::MNEMONIC_PARAMCHARS:
        result.append(fadecName.parameterLetters);
        break;
    case DerivationStrategy::FULL_WITHOUT_IMPLICIT:
        result.append(fadecName.size);
        result.append(fadecName.parameterLetters);
        break;
    case DerivationStrategy::FULL:
        result.append(fadecName.size);
        result.append(fadecName.parameterLettersWithImplicitOps);
        break;
    }
    return result;
}

bool deriveInstInfo(unsigned                     opcode,
                    const llvm::MachineFunction *machineFunction,
                    InstInfo                    &instInfo,
                    bool                         debug) {
    const llvm::MCContext         &mcContext = machineFunction->getContext();
    const llvm::LLVMTargetMachine &targetMachine = machineFunction->getTarget();
    const llvm::MCAsmInfo         *asmInfo       = mcContext.getAsmInfo();
    const llvm::MCRegisterInfo    *regInfo       = mcContext.getRegisterInfo();
    const llvm::TargetRegisterInfo *targetRegInfo =
        machineFunction->getSubtarget().getRegisterInfo();
    const llvm::MachineRegisterInfo &machineRegInfo =
        machineFunction->getRegInfo();
    const llvm::MCInstrInfo *instrInfo = targetMachine.getMCInstrInfo();
    std::string              instrName = instrInfo->getName(opcode).str();

    fadecUtils::FunctionInfo functionInfo;
    // Create an MCInstPrinter in order to get an instruction's mnemonic later
    auto                     instPrinter = std::unique_ptr<llvm::MCInstPrinter>(
        targetMachine.getTarget().createMCInstPrinter(
            mcContext.getTargetTriple(),
            /*Intel-Syntax*/ 1,
            *asmInfo,
            *instrInfo,
            *regInfo));

    llvm::StringRef          instName  = instrInfo->getName(opcode);
    const llvm::MCInstrDesc &instrDesc = instrInfo->get(opcode);

    if (manualMappings.contains(instrName)) {
        instInfo = manualMappings[instrName];
        return true;
    }

    // We will use this to store all information we will gather now
    FadecName fadecName{};

    llvm::MCInst inst{};
    inst.setOpcode(opcode);
    const char *mnemonic = instPrinter->getMnemonic(&inst).first;
    if (!mnemonic) {
        return false;
    }
    fadecName.mnemonic = mnemonic;

    // Sometimes we have a lonely Tab at the end of the mnemonic string
    auto foundTab = fadecName.mnemonic.find('\t');
    if (foundTab != std::string::npos) {
        fadecName.mnemonic = fadecName.mnemonic.substr(0, foundTab);
    }
    if (debug) {
        std::cout << "Mnemonic: " << fadecName.mnemonic << "\n";
    }
    // The function names are always in uppercase
    std::transform(fadecName.mnemonic.begin(),
                   fadecName.mnemonic.end(),
                   fadecName.mnemonic.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Check all important prefixes (such as LOCK_). If one of them exists,
    // re-attach it
    for (const char *prefix : importantPrefixes) {
        if (instName.starts_with(prefix)) {
            if (debug) {
                std::cout << "Keeping prefix: " << prefix << "\n";
            }
            fadecName.prefix = prefix;
            break;
        }
    }

    /*
    ** For MOVD and MOVQ, there is this annoying _G2X or _X2G suffix.
    ** So we need to check if it is oved into a GP or XMM register and adjust
    *the suffix based on that
    ** Edge case is the mamory operand as destination. In that case, _X2G is
    *used, as the first memory operand has the register class GR8
    ** That's not an issue though, as a _G2X variant with a memory destination
    *does not exist
    */
    if (fadecName.mnemonic == "MOVD" || fadecName.mnemonic == "VMOVD"
        || fadecName.mnemonic == "MOVQ" || fadecName.mnemonic == "VMOVQ") {
        std::string_view regClassName = regInfo->getRegClassName(
            &regInfo->getRegClass(instrDesc.operands()[0].RegClass));
        if (regClassName.starts_with("VR") || regClassName.starts_with("FR")) {
            fadecName.suffix = "_G2X";
        } else {
            fadecName.suffix = "_X2G";
        }
    }

    const char *TEMP_CC_SUFFIX = "NZ";
    for (int i = 0; i < instrDesc.NumOperands; i++) {
        if (instrDesc.operands()[i].OperandType
            == llvmUtils::MAYBE_OPERAND_COND_CODE) {
            /*
            ** This is a condition code operand
            ** The instruction name normally has the condition code in its name,
            *meaning that
            ** just searching for the mnemonic won't yield anything useful
            ** So, we temporarily set the suffix to the value of a condition
            *code
            ** Just to check if we can find the instruction
            */
            fadecName.suffix = TEMP_CC_SUFFIX;
            if (debug) {
                std::cout << "Condition code found, using temporary suffix for "
                             "finding fadec instr\n";
            }
        }
    }

    attachParameterChars(fadecName.parameterLetters, instrDesc, regInfo, false);
    attachParameterChars(
        fadecName.parameterLettersWithImplicitOps, instrDesc, regInfo, true);

    // Try to get the size specifier from any def or operand
    for (int i = 0; i < instrDesc.NumDefs + instrDesc.NumOperands; i++) {
        const llvm::MCOperandInfo &operandInfo = instrDesc.operands()[i];
        if (operandInfo.RegClass != -1) {
            const llvm::MCRegisterClass &regClass =
                regInfo->getRegClass(operandInfo.RegClass);
            fadecName.size = std::to_string(regClass.getSizeInBits()).data();
            break;
        }
    }
    // If that didn't work, get the size of an implicitly defined register (that
    // is not a flags-register)
    if (fadecName.size.empty()) {
        for (int i = 0; i < instrDesc.NumImplicitUses; i++) {
            const llvm::MCPhysReg &physReg = instrDesc.implicit_uses()[i];
            std::string_view       regName(regInfo->getName(physReg));
            if (hasRegSize(regName)) {
                fadecName.size = std::to_string(targetRegInfo->getRegSizeInBits(
                                                    physReg, machineRegInfo))
                                     .data();
                break;
            }
        }
    }

    int index = instrLookup(instrName);
    if (index != -1) {
        // LLVM instruction name matches directly
        functionInfo = fadecUtils::functionInfos[index];
        goto variantMatching;
    }
    for (int i = static_cast<int>(DerivationStrategy::FULL);
         i >= static_cast<int>(DerivationStrategy::MNEMONIC_ONLY);
         i--) {
        DerivationStrategy strategy  = static_cast<DerivationStrategy>(i);
        std::string        instrName = getFadecString(fadecName, strategy);
        if (debug) {
            evaluateMatches(instrName);
        }
        index = instrLookup(instrName);
        if (index != -1) {
            functionInfo = fadecUtils::functionInfos[index];
            goto variantMatching;
        }
    }
    // Couldn't find instruction, *sad tpde-asmgen noises*
    return false;

variantMatching:
    // We jumped here if we found an instruction, yay!
    // Now, we need to go through all parameters and check which variants we can
    // match
    instInfo.commutable = instrDesc.isCommutable();
    instInfo.isFullName = functionInfo.numVariants == 0;
    instInfo.fadecName  = functionInfo.fadecName;

    /*
    ** If we used a temporary suffix for finding an instruction when it uses a
    *condition code,
    ** remove it now and write CC at the instruction name end.
    ** This will signal to us later that we need to replace that part with the
    *condition code letters
    ** The nice thing is that there are no Fadec functions that have CC in their
    *name
    */
    if (fadecName.suffix == TEMP_CC_SUFFIX) {
        instInfo.fadecName.replace(instInfo.fadecName.find(TEMP_CC_SUFFIX),
                                   strlen(TEMP_CC_SUFFIX),
                                   "CC");
    }

    const auto &params = functionInfo.params;
    instInfo.numOps    = functionInfo.numParams;
    for (OpSupports &op : instInfo.ops) {
        op.reset();
    }

    /// Goes through the instruction's parameters and assigns the first one
    /// matching the provided opType to the LLVM operand index
    auto takeFirst =
        [&instInfo, &params, &functionInfo, &instName, &instrDesc, &regInfo](
            OpType opType, int index) {
            for (unsigned i = 0; i < functionInfo.numParams; i++) {
                // The parameter must not be set yet
                if (instInfo.ops[i].isNOP()) {
                    if (params[i] == opType
                        || (params[i] >= IMM64 && params[i] <= IMM8
                            && opType == IMM8)) {
                        instInfo.ops[i] =
                            OpSupports(static_cast<uint16_t>(params[i]), index);

                        // Now, set opSize correctly
                        if (isRegOpType(params[i])) {
                            const llvm::MCOperandInfo &operandInfo =
                                instrDesc.operands()[i];
                            if (operandInfo.RegClass != -1) {
                                const llvm::MCRegisterClass &regClass =
                                    regInfo->getRegClass(operandInfo.RegClass);
                                instInfo.ops[i].opSize =
                                    regClass.getSizeInBits();
                            }
                        } else if (params[i] == MEM
                                   && (instName.starts_with("MOVZX")
                                       || instName.starts_with("MOVSX"))) {
                            /*
                            ** In LLVM and Fadec, the size of a memory access is
                            *in the
                            ** instruction name for extension movs.
                            ** This seems to be the only place to get that
                            *information from
                            */
                            auto memSizeStr =
                                instName.substr(instName.find('m') + 1);
                            std::from_chars(memSizeStr.begin(),
                                            memSizeStr.end(),
                                            instInfo.ops[i].opSize);
                        } else {
                            instInfo.ops[i].opSize = 64;
                        }
                        return;
                    }
                }
            }
        };

    // First, get the first instruction (the *actual* instruction), and match
    // all parameters
    /// Receives an array of registers and attempts to match all important
    /// registers to REG parameters
    auto matchFromRegArray =
        [&params, &regInfo, &instInfo, &functionInfo, &debug](
            llvm::ArrayRef<llvm::MCPhysReg> regs, unsigned startIndex) {
            for (unsigned i = 0; i < regs.size(); i++) {
                const llvm::MCRegister &reg{regs[i]};
                std::string_view        regName = regInfo->getName(reg);
                std::string_view        regClassName{};
                for (const llvm::MCRegisterClass &regClass :
                     regInfo->regclasses()) {
                    if (regClass.contains(reg)) {
                        regClassName = regInfo->getRegClassName(&regClass);
                    }
                }

                if (hasRegSize(regName)) {
                    bool used = false;
                    for (unsigned j = 0; j < functionInfo.numParams; j++) {
                        if (instInfo.ops[j].isNOP()
                            && ((params[j] == GPREG
                                 && regClassName.starts_with("GR"))
                                || params[j] == XMMREG)) {
                            instInfo.ops[j] =
                                OpSupports(static_cast<uint16_t>(params[j]),
                                           startIndex + i);
                            used = true;
                            break;
                        }
                    }
                    if (!used && debug) {
                        std::cout
                            << "Possibly unhandled physical reg: " << regName
                            << " (" << regClassName << ")\n";
                    }
                }
            }
        };

    for (int i = 0; i < instrDesc.NumOperands; i++) {
        auto op      = instrDesc.operands()[i];
        int  tiedIdx = instrDesc.getOperandConstraint(i, llvm::MCOI::TIED_TO);
        if (tiedIdx != -1) {
            /*
            ** This operand is tied to a definition
            ** That means that we already assigned said tied parameter, but
            *actually wanted this one
            ** Example: ADD32rr
            **   [reg] = [reg (tied-def 0)], [reg]
            **  If we get to the tied-def register, we realise that the first
            *register that
            **  we assigned was actually not a new definition.
            */
            for (int j = 0; j < instInfo.numOps; j++) {
                OpSupports &opSupports = instInfo.ops[j];
                if (!opSupports.isNOP() && opSupports.getOpIndex() == tiedIdx) {
                    // Change the index
                    opSupports.opIndex = i;
                }
            }
            continue;
        }

        switch (static_cast<llvm::MCOI::OperandType>(op.OperandType)) {
        case llvm::MCOI::OPERAND_REGISTER: {
            std::string_view regClass =
                regInfo->getRegClassName(&regInfo->getRegClass(op.RegClass));
            if (regClass.starts_with("GR")) {
                takeFirst(GPREG, i);
            } else {
                takeFirst(XMMREG, i);
            }
            break;
        }
        case llvm::MCOI::OPERAND_IMMEDIATE: takeFirst(IMM8, i); break;
        case llvm::MCOI::OPERAND_MEMORY:
            takeFirst(MEM, i);
            i += 4;
            break;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
        case llvmUtils::MAYBE_OPERAND_COND_CODE: break;
#pragma GCC diagnostic pop
        default:
            if (debug) {
                std::cout << "Unknown operand type: " << op.OperandType << "\n";
            }
            return false;
        }
    }
    matchFromRegArray(instrDesc.implicit_defs(), instrDesc.NumOperands);
    matchFromRegArray(instrDesc.implicit_uses(),
                      instrDesc.NumOperands + instrDesc.NumImplicitDefs);

    // Check if all parameters were used
    for (int i = 0; i < instInfo.numOps; i++) {
        if (instInfo.ops[i].isNOP()) {
            if (debug) {
                std::cout << "Operand " << i << " is not set correctly!\n";
                instInfo.print(std::cout);
            }
            return false;
        }
    }

    // Now go through all variants and try to integrate those as well
    for (unsigned i = 0; i < functionInfo.numVariants; i++) {
        unsigned variantIndex = functionInfo.variants[i];
        if (debug) {
            std::cout << "Trying variant "
                      << fadecUtils::functionNames[variantIndex].str() << ": ";
        }
        const auto &variantParams =
            fadecUtils::functionInfos[variantIndex].params;

        bool       success      = true;
        // Create a local copy of the current bitmap which we modify first
        // before applying the changes
        OpSupports newBitmap[4] = {
            instInfo.ops[0], instInfo.ops[1], instInfo.ops[2], instInfo.ops[3]};

        for (unsigned paramNo = 0;
             paramNo < fadecUtils::functionInfos[variantIndex].numParams;
             paramNo++) {
            OpType variantType  = variantParams[paramNo];
            OpType originalType = params[paramNo];

            // The types are different
            if (variantType != originalType) {
                if (!isRegOpType(originalType)) {
                    // We only accept variants for REG types
                    success = false;
                    if (debug) {
                        std::cout << "Fail, variant for original type that is "
                                     "not REG\n";
                    }
                    break;
                }

                int llvmIndex = instInfo.ops[paramNo].opIndex;
                if (llvmIndex >= instrDesc.NumOperands) {
                    // This is a physical register, we cannot change that
                    assert(originalType & REG);
                    if (debug) {
                        std::cout << "Fail, variant for physical register\n";
                    }
                    success = false;
                    break;
                }
                // const auto &llvmOp = instrDesc.operands()[llvmIndex];

                int tiedIdx = instrDesc.getOperandConstraint(
                    llvmIndex, llvm::MCOI::TIED_TO);
                if (tiedIdx != -1 || llvmIndex < instrDesc.NumDefs) {
                    /*
                    ** This is a tied-def operand or a definition
                    ** This MUST be a register and may not be a memory operand
                    *or an immediate
                    */
                    assert(originalType & REG);
                    if (debug) {
                        std::cout << "Fail, variant for tied-def operand\n";
                    }
                    success = false;
                    break;
                } else {
                    // Should(TM) be fine
                    newBitmap[paramNo].addOpType(variantType);
                }
            }
        }

        // If no errors occurred, update the bitmaps
        if (success) {
            std::copy(&newBitmap[0], &newBitmap[0] + 4, instInfo.ops);
            if (debug) {
                std::cout << "Success\n";
            }
        }
    }
    return true;
}

} // namespace llvmUtils
