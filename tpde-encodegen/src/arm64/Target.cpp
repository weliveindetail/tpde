// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#include "arm64/Target.hpp"

#include <format>
#include <string>
#include <string_view>

#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineInstr.h>
#include <llvm/CodeGen/MachineOperand.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/CodeGen/TargetRegisterInfo.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/Target/TargetMachine.h>


using namespace std::literals;

namespace tpde_encgen::arm64 {

void EncodingTargetArm64::get_inst_candidates(
    llvm::MachineInstr &mi, llvm::SmallVectorImpl<MICandidate> &candidates) {
    const llvm::LLVMTargetMachine &TM   = mi.getMF()->getTarget();
    const llvm::MCInstrInfo       &MCII = *TM.getMCInstrInfo();
    const llvm::MCInstrDesc       &MCID = MCII.get(mi.getOpcode());
    (void)MCID;

    llvm::StringRef Name = MCII.getName(mi.getOpcode());
    llvm::dbgs() << "get for " << Name << "\n";

    // From Disarm
    const auto imm_logical = [](bool sf, uint64_t Nimmsimmr) {
        unsigned N      = Nimmsimmr >> 12;
        unsigned immr   = Nimmsimmr >> 6 & 0x3f;
        unsigned imms   = Nimmsimmr & 0x3f;
        unsigned len    = 31 - __builtin_clz((N << 6) | (~imms & 0x3f));
        unsigned levels = (1 << len) - 1;
        unsigned s      = imms & levels;
        unsigned r      = immr & levels;
        unsigned esize  = 1 << len;
        uint64_t welem  = ((uint64_t)1 << (s + 1)) - 1;
        // ROR(welem, r) as bits(esize)
        welem           = (welem >> r) | (welem << (esize - r));
        if (esize < 64) {
            welem &= ((uint64_t)1 << esize) - 1;
        }
        // Replicate(ROR(welem, r))
        uint64_t wmask = 0;
        for (unsigned i = 0; i < (!sf ? 32 : 64); i += esize) {
            wmask |= welem << i;
        }
        return wmask;
    };

    const auto handle_default = [&](std::string_view mnem,
                                    std::string      extra_ops = "") {
        candidates.emplace_back([mnem, extra_ops](
                                    std::string                        &buf,
                                    const llvm::MachineInstr           &mi,
                                    llvm::SmallVectorImpl<std::string> &ops) {
            const auto &tri = *mi.getMF()->getRegInfo().getTargetRegisterInfo();
            std::format_to(std::back_inserter(buf), "        ASMD({}", mnem);
            unsigned reg_idx = 0;
            for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n; i++) {
                const auto &op = mi.getOperand(i);
                if (op.isReg()) {
                    if (op.isTied() && op.isUse()) {
                        continue;
                    }
                    llvm::StringRef name = tri.getName(op.getReg());
                    if (name == "WZR" || name == "XZR") { // zero register
                        buf += ", DA_ZR";
                    } else {
                        std::format_to(
                            std::back_inserter(buf), ", {}", ops[reg_idx++]);
                    }
                } else if (op.isImm()) {
                    std::format_to(
                        std::back_inserter(buf), ", {:#x}", op.getImm());
                } else {
                    assert(false);
                }
            }
            std::format_to(std::back_inserter(buf), "{});\n", extra_ops);
        });
    };
    const auto handle_noimm = [&](std::string_view mnem,
                                  std::string      extra_ops = "") {
        candidates.emplace_back([mnem, extra_ops](
                                    std::string                        &buf,
                                    const llvm::MachineInstr           &mi,
                                    llvm::SmallVectorImpl<std::string> &ops) {
            const auto &tri = *mi.getMF()->getRegInfo().getTargetRegisterInfo();
            std::format_to(std::back_inserter(buf), "        ASMD({}", mnem);
            unsigned reg_idx = 0;
            for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n; i++) {
                const auto &op = mi.getOperand(i);
                if (op.isReg()) {
                    if (op.isTied() && op.isUse()) {
                        continue;
                    }
                    llvm::StringRef name = tri.getName(op.getReg());
                    if (name == "WZR" || name == "XZR") { // zero register
                        buf += ", DA_ZR";
                    } else {
                        std::format_to(
                            std::back_inserter(buf), ", {}", ops[reg_idx++]);
                    }
                } else if (op.isImm()) {
                } else {
                    assert(false);
                }
            }
            std::format_to(std::back_inserter(buf), "{});\n", extra_ops);
        });
    };

    if (Name == "MOVZWi") {
        unsigned imm   = mi.getOperand(1).getImm();
        unsigned shift = mi.getOperand(2).getImm();
        handle_noimm("MOVZw_shift",
                     std::format(", {:#x}, {}", imm, shift / 16));
    } else if (Name == "MOVZXi") {
        unsigned imm   = mi.getOperand(1).getImm();
        unsigned shift = mi.getOperand(2).getImm();
        handle_noimm("MOVZx_shift",
                     std::format(", {:#x}, {}", imm, shift / 16));
    } else if (Name == "MOVKWi") {
        unsigned imm   = mi.getOperand(1).getImm();
        unsigned shift = mi.getOperand(2).getImm();
        handle_noimm("MOVKw_shift",
                     std::format(", {:#x}, {}", imm, shift / 16));
    } else if (Name == "MOVKXi") {
        unsigned imm   = mi.getOperand(1).getImm();
        unsigned shift = mi.getOperand(2).getImm();
        handle_noimm("MOVKx_shift",
                     std::format(", {:#x}, {}", imm, shift / 16));
    } else if (Name == "MOVNWi") {
        unsigned imm   = mi.getOperand(1).getImm();
        unsigned shift = mi.getOperand(2).getImm();
        handle_noimm("MOVNw_shift",
                     std::format(", {:#x}, {}", imm, shift / 16));
    } else if (Name == "MOVNXi") {
        unsigned imm   = mi.getOperand(1).getImm();
        unsigned shift = mi.getOperand(2).getImm();
        handle_noimm("MOVNx_shift",
                     std::format(", {:#x}, {}", imm, shift / 16));
    } else if (Name == "LDRBBui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRBwu");
    } else if (Name == "LDRHHui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRHwu");
    } else if (Name == "LDRWui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRwu");
    } else if (Name == "LDRXui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRxu");
    } else if (Name == "PRFMui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("PRFMu");
    } else if (Name == "LDRBui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRbu");
    } else if (Name == "LDRHui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRhu");
    } else if (Name == "LDRSui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRsu");
    } else if (Name == "LDRDui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRdu");
    } else if (Name == "LDRQui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRqu");
    } else if (Name == "STRBBui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRBwu");
    } else if (Name == "STRHHui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRHwu");
    } else if (Name == "STRWui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRwu");
    } else if (Name == "STRXui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRxu");
    } else if (Name == "STRBui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRbu");
    } else if (Name == "STRHui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRhu");
    } else if (Name == "STRSui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRsu");
    } else if (Name == "STRDui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRdu");
    } else if (Name == "STRQui") {
        // TODO: Handle expr with base+off, merge offsets
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRqu");
    } else if (Name == "LDPWi") {
        // TODO: Handle expr with base+off, merge offsets
        handle_default("LDPw");
    } else if (Name == "LDPXi") {
        // TODO: Handle expr with base+off, merge offsets
        handle_default("LDPx");
    } else if (Name == "STPWi") {
        // TODO: Handle expr with base+off, merge offsets
        handle_default("STPw");
    } else if (Name == "STPXi") {
        // TODO: Handle expr with base+off, merge offsets
        handle_default("STPx");
    } else if (Name == "UBFMWri") {
        // TODO: fold zero-extend with load
        handle_default("UBFMw");
    } else if (Name == "UBFMXri") {
        // TODO: fold zero-extend with load
        handle_default("UBFMx");
    } else if (Name == "SBFMWri") {
        // TODO: fold sign-extend with load
        handle_default("SBFMw");
    } else if (Name == "SBFMXri") {
        // TODO: fold sign-extend with load
        handle_default("SBFMx");
    } else if (Name == "BFMWri") {
        handle_default("BFMw");
    } else if (Name == "BFMXri") {
        handle_default("BFMx");
    } else if (Name == "LSLVWr") {
        // TODO: handle shift immediate
        handle_default("LSLVw");
    } else if (Name == "LSLVXr") {
        // TODO: handle shift immediate
        handle_default("LSLVx");
    } else if (Name == "LSRVWr") {
        // TODO: handle shift immediate
        handle_default("LSRVw");
    } else if (Name == "LSRVXr") {
        // TODO: handle shift immediate
        handle_default("LSRVx");
    } else if (Name == "ASRVWr") {
        // TODO: handle shift immediate
        handle_default("ASRVw");
    } else if (Name == "ASRVXr") {
        // TODO: handle shift immediate
        handle_default("ASRVx");
    } else if (Name == "RORVWr") {
        // TODO: handle shift immediate
        handle_default("RORVw");
    } else if (Name == "RORVXr") {
        // TODO: handle shift immediate
        handle_default("RORVx");
    } else if (Name == "ADDWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDwi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "ADDXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "ADDSWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDSwi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "ADDSXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDSxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "SUBWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBwi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "SUBXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "SUBSWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBSwi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "SUBSXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBSxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "ADDWrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDw_lsl", "ADDw_lsr", "ADDw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDXrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDx_lsl", "ADDx_lsr", "ADDx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDSWrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDSw_lsl", "ADDSw_lsr", "ADDSw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDSXrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDSx_lsl", "ADDSx_lsr", "ADDSx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBWrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBw_lsl", "SUBw_lsr", "SUBw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBXrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBx_lsl", "SUBx_lsr", "SUBx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBSWrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBSw_lsl", "SUBSw_lsr", "SUBSw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBSXrs") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBSx_lsl", "SUBSx_lsr", "SUBSx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDWrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"ADDw_uxtb",
                                              "ADDw_uxth",
                                              "ADDw_uxtw",
                                              "ADDw_uxtx",
                                              "ADDw_sxtb",
                                              "ADDw_sxth",
                                              "ADDw_sxtw",
                                              "ADDw_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDXrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"ADDx_uxtb",
                                              "ADDx_uxth",
                                              "ADDx_uxtw",
                                              "ADDx_uxtx",
                                              "ADDx_sxtb",
                                              "ADDx_sxth",
                                              "ADDx_sxtw",
                                              "ADDx_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDSWrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"ADDSw_uxtb",
                                              "ADDSw_uxth",
                                              "ADDSw_uxtw",
                                              "ADDSw_uxtx",
                                              "ADDSw_sxtb",
                                              "ADDSw_sxth",
                                              "ADDSw_sxtw",
                                              "ADDSw_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDSXrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"ADDSx_uxtb",
                                              "ADDSx_uxth",
                                              "ADDSx_uxtw",
                                              "ADDSx_uxtx",
                                              "ADDSx_sxtb",
                                              "ADDSx_sxth",
                                              "ADDSx_sxtw",
                                              "ADDSx_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBWrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"SUBw_uxtb",
                                              "SUBw_uxth",
                                              "SUBw_uxtw",
                                              "SUBw_uxtx",
                                              "SUBw_sxtb",
                                              "SUBw_sxth",
                                              "SUBw_sxtw",
                                              "SUBw_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBXrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"SUBx_uxtb",
                                              "SUBx_uxth",
                                              "SUBx_uxtw",
                                              "SUBx_uxtx",
                                              "SUBx_sxtb",
                                              "SUBx_sxth",
                                              "SUBx_sxtw",
                                              "SUBx_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBSWrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"SUBSw_uxtb",
                                              "SUBSw_uxth",
                                              "SUBSw_uxtw",
                                              "SUBSw_uxtx",
                                              "SUBSw_sxtb",
                                              "SUBSw_sxth",
                                              "SUBSw_sxtw",
                                              "SUBSw_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBSXrx") {
        // TODO: Handle arithmetic immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 8> mnems{"SUBSx_uxtb",
                                              "SUBSx_uxth",
                                              "SUBSx_uxtw",
                                              "SUBSx_uxtx",
                                              "SUBSx_sxtb",
                                              "SUBSx_sxth",
                                              "SUBSx_sxtw",
                                              "SUBSx_sxtx"};
        unsigned                        imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADCWr") {
        handle_default("ADCw");
    } else if (Name == "ADCXr") {
        handle_default("ADCx");
    } else if (Name == "ADCSWr") {
        handle_default("ADCSw");
    } else if (Name == "ADCSXr") {
        handle_default("ADCSx");
    } else if (Name == "SBCWr") {
        handle_default("SBCw");
    } else if (Name == "SBCXr") {
        handle_default("SBCx");
    } else if (Name == "SBCSWr") {
        handle_default("SBCSw");
    } else if (Name == "SBCSXr") {
        handle_default("SBCSx");
    } else if (Name == "MADDWrrr") {
        handle_default("MADDw");
    } else if (Name == "MADDXrrr") {
        handle_default("MADDx");
    } else if (Name == "MSUBWrrr") {
        handle_default("MSUBw");
    } else if (Name == "MSUBXrrr") {
        handle_default("MSUBx");
    } else if (Name == "UMADDLrrr") {
        handle_default("UMADDL");
    } else if (Name == "UMSUBLrrr") {
        handle_default("UMSUBL");
    } else if (Name == "SMADDLrrr") {
        handle_default("SMADDL");
    } else if (Name == "SMSUBLrrr") {
        handle_default("SMSUBL");
    } else if (Name == "UMULHrr") {
        handle_default("UMULH");
    } else if (Name == "SMULHrr") {
        handle_default("SMULH");
    } else if (Name == "UDIVWr") {
        handle_default("UDIVw");
    } else if (Name == "UDIVXr") {
        handle_default("UDIVx");
    } else if (Name == "SDIVWr") {
        handle_default("SDIVw");
    } else if (Name == "SDIVXr") {
        handle_default("SDIVx");
    } else if (Name == "CSELWr") {
        handle_default("CSELw");
    } else if (Name == "CSELXr") {
        handle_default("CSELx");
    } else if (Name == "CSINCWr") {
        handle_default("CSINCw");
    } else if (Name == "CSINCXr") {
        handle_default("CSINCx");
    } else if (Name == "CSINVWr") {
        handle_default("CSINVw");
    } else if (Name == "CSINVXr") {
        handle_default("CSINVx");
    } else if (Name == "CSNEGWr") {
        handle_default("CSNEGw");
    } else if (Name == "CSNEGXr") {
        handle_default("CSNEGx");
    } else if (Name == "ANDWri") {
        uint64_t imm = imm_logical(0, mi.getOperand(2).getImm());
        handle_noimm("ANDwi", std::format(", {:#x}", imm));
    } else if (Name == "ANDXri") {
        uint64_t imm = imm_logical(1, mi.getOperand(2).getImm());
        handle_noimm("ANDxi", std::format(", {:#x}", imm));
    } else if (Name == "ORRWri") {
        uint64_t imm = imm_logical(0, mi.getOperand(2).getImm());
        handle_noimm("ORRwi", std::format(", {:#x}", imm));
    } else if (Name == "ORRXri") {
        uint64_t imm = imm_logical(1, mi.getOperand(2).getImm());
        handle_noimm("ORRxi", std::format(", {:#x}", imm));
    } else if (Name == "EORWri") {
        uint64_t imm = imm_logical(0, mi.getOperand(2).getImm());
        handle_noimm("EORwi", std::format(", {:#x}", imm));
    } else if (Name == "EORXri") {
        uint64_t imm = imm_logical(1, mi.getOperand(2).getImm());
        handle_noimm("EORxi", std::format(", {:#x}", imm));
    } else if (Name == "ANDSWri") {
        uint64_t imm = imm_logical(0, mi.getOperand(2).getImm());
        handle_noimm("ANDSwi", std::format(", {:#x}", imm));
    } else if (Name == "ANDSXri") {
        uint64_t imm = imm_logical(1, mi.getOperand(2).getImm());
        handle_noimm("ANDSxi", std::format(", {:#x}", imm));
    } else if (Name == "RBITWr") {
        handle_default("RBITw");
    } else if (Name == "RBITXr") {
        handle_default("RBITx");
    } else if (Name == "CLZWr") {
        handle_default("CLZw");
    } else if (Name == "CLZXr") {
        handle_default("CLZx");
    } else if (Name == "CLSWr") {
        handle_default("CLSw");
    } else if (Name == "CLSXr") {
        handle_default("CLSx");
    } else if (Name == "ANDWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ANDw_lsl", "ANDw_lsr", "ANDw_asr", "ANDw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ANDXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ANDx_lsl", "ANDx_lsr", "ANDx_asr", "ANDx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ORRWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ORRw_lsl", "ORRw_lsr", "ORRw_asr", "ORRw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ORRXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ORRx_lsl", "ORRx_lsr", "ORRx_asr", "ORRx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "EORWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "EORw_lsl", "EORw_lsr", "EORw_asr", "EORw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "EORXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "EORx_lsl", "EORx_lsr", "EORx_asr", "EORx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ANDSWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ANDSw_lsl", "ANDSw_lsr", "ANDSw_asr", "ANDSw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ANDSXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ANDSx_lsl", "ANDSx_lsr", "ANDSx_asr", "ANDSx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "BICWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "BICw_lsl", "BICw_lsr", "BICw_asr", "BICw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "BICXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "BICx_lsl", "BICx_lsr", "BICx_asr", "BICx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ORNWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ORNw_lsl", "ORNw_lsr", "ORNw_asr", "ORNw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ORNXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ORNx_lsl", "ORNx_lsr", "ORNx_asr", "ORNx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "EONWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "EONw_lsl", "EONw_lsr", "EONw_asr", "EONw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "EONXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "EONx_lsl", "EONx_lsr", "EONx_asr", "EONx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "BICSWrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "BICSw_lsl", "BICSw_lsr", "BICSw_asr", "BICSw_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "BICSXrs") {
        // TODO: Handle logical immediates
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "BICSx_lsl", "BICSx_lsr", "BICSx_asr", "BICSx_ror"};
        unsigned imm = mi.getOperand(3).getImm();
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));

    } else if (Name == "CCMPWr") {
        handle_default("CCMPw");
    } else if (Name == "CCMPXr") {
        handle_default("CCMPx");
    } else if (Name == "CCMPWi") {
        handle_default("CCMPwi");
    } else if (Name == "CCMPXi") {
        handle_default("CCMPxi");

    } else if (Name == "FCSELSrrr") {
        handle_default("FCSELs");
    } else if (Name == "FCSELDrrr") {
        handle_default("FCSELd");
    } else if (Name == "FCMPSrr") {
        handle_default("FCMPs");
    } else if (Name == "FCMPDrr") {
        handle_default("FCMPd");
    } else if (Name == "FADDSrr") {
        handle_default("FADDs");
    } else if (Name == "FADDDrr") {
        handle_default("FADDd");
    } else if (Name == "FSUBSrr") {
        handle_default("FSUBs");
    } else if (Name == "FSUBDrr") {
        handle_default("FSUBd");
    } else if (Name == "FMULSrr") {
        handle_default("FMULs");
    } else if (Name == "FMULDrr") {
        handle_default("FMULd");
    } else if (Name == "FDIVSrr") {
        handle_default("FDIVs");
    } else if (Name == "FDIVDrr") {
        handle_default("FDIVd");
    } else if (Name == "FMINSrr") {
        handle_default("FMINs");
    } else if (Name == "FMINDrr") {
        handle_default("FMINd");
    } else if (Name == "FMAXSrr") {
        handle_default("FMAXs");
    } else if (Name == "FMAXDrr") {
        handle_default("FMAXd");
    } else if (Name == "FMOVSr") {
        handle_default("FMOVs");
    } else if (Name == "FMOVDr") {
        handle_default("FMOVd");
    } else if (Name == "FABSSr") {
        handle_default("FABSs");
    } else if (Name == "FABSDr") {
        handle_default("FABSd");
    } else if (Name == "FNEGSr") {
        handle_default("FNEGs");
    } else if (Name == "FNEGDr") {
        handle_default("FNEGd");
    } else if (Name == "FSQRTSr") {
        handle_default("FSQRTs");
    } else if (Name == "FSQRTDr") {
        handle_default("FSQRTd");
    } else if (Name == "FMADDSrrr") {
        handle_default("FMADDs");
    } else if (Name == "FMADDDrrr") {
        handle_default("FMADDd");
    } else if (Name == "FMSUBSrrr") {
        handle_default("FMSUBs");
    } else if (Name == "FMSUBDrrr") {
        handle_default("FMSUBd");
    } else if (Name == "FNMADDSrrr") {
        handle_default("FNMADDs");
    } else if (Name == "FNMADDDrrr") {
        handle_default("FNMADDd");
    } else if (Name == "FNMSUBSrrr") {
        handle_default("FNMSUBs");
    } else if (Name == "FNMSUBDrrr") {
        handle_default("FNMSUBd");

    } else if (Name == "FCVTSDr") {
        handle_default("FCVTsd");
    } else if (Name == "FCVTDSr") {
        handle_default("FCVTds");
    } else if (Name == "FCVTZSUWSr") {
        handle_default("FCVTZSws"); // TODO: correct?
    } else if (Name == "FCVTZUUWSr") {
        handle_default("FCVTZUws"); // TODO: correct?
    } else if (Name == "FCVTZSUWDr") {
        handle_default("FCVTZSwd"); // TODO: correct?
    } else if (Name == "FCVTZUUWDr") {
        handle_default("FCVTZUwd"); // TODO: correct?
    } else if (Name == "FCVTZSUXSr") {
        handle_default("FCVTZSxs"); // TODO: correct?
    } else if (Name == "FCVTZUUXSr") {
        handle_default("FCVTZUxs"); // TODO: correct?
    } else if (Name == "FCVTZSUXDr") {
        handle_default("FCVTZSxd"); // TODO: correct?
    } else if (Name == "FCVTZUUXDr") {
        handle_default("FCVTZUxd"); // TODO: correct?
    } else if (Name == "SCVTFUWSri") {
        handle_default("SCVTFsw"); // TODO: correct?
    } else if (Name == "UCVTFUWSri") {
        handle_default("UCVTFsw"); // TODO: correct?
    } else if (Name == "SCVTFUWDri") {
        handle_default("SCVTFdw"); // TODO: correct?
    } else if (Name == "UCVTFUWDri") {
        handle_default("UCVTFdw"); // TODO: correct?
    } else if (Name == "SCVTFUXSri") {
        handle_default("SCVTFsx"); // TODO: correct?
    } else if (Name == "UCVTFUXSri") {
        handle_default("UCVTFsx"); // TODO: correct?
    } else if (Name == "SCVTFUXDri") {
        handle_default("SCVTFdx"); // TODO: correct?
    } else if (Name == "UCVTFUXDri") {
        handle_default("UCVTFdx"); // TODO: correct?

    } else if (Name == "FMOVSWr") {
        handle_default("FMOVsw");
    } else if (Name == "FMOVDXr") {
        handle_default("FMOVdx");
    } else if (Name == "FMOVWSr") {
        handle_default("FMOVws");
    } else if (Name == "FMOVXDr") {
        handle_default("FMOVxd");

    } else if (Name == "CASB") {
        handle_default("CASB");
    } else if (Name == "CASH") {
        handle_default("CASH");
    } else if (Name == "CASW") {
        handle_default("CASw");
    } else if (Name == "CASX") {
        handle_default("CASx");
    } else if (Name == "CASAB") {
        handle_default("CASAB");
    } else if (Name == "CASAH") {
        handle_default("CASAH");
    } else if (Name == "CASAW") {
        handle_default("CASAw");
    } else if (Name == "CASAX") {
        handle_default("CASAx");
    } else if (Name == "CASLB") {
        handle_default("CASLB");
    } else if (Name == "CASLH") {
        handle_default("CASLH");
    } else if (Name == "CASLW") {
        handle_default("CASLw");
    } else if (Name == "CASLX") {
        handle_default("CASLx");
    } else if (Name == "CASALB") {
        handle_default("CASALB");
    } else if (Name == "CASALH") {
        handle_default("CASALH");
    } else if (Name == "CASALW") {
        handle_default("CASALw");
    } else if (Name == "CASALX") {
        handle_default("CASALx");
    } else if (Name == "SWPB") {
        handle_default("SWPB");
    } else if (Name == "SWPH") {
        handle_default("SWPH");
    } else if (Name == "SWPW") {
        handle_default("SWPw");
    } else if (Name == "SWPX") {
        handle_default("SWPx");
    } else if (Name == "SWPAB") {
        handle_default("SWPAB");
    } else if (Name == "SWPAH") {
        handle_default("SWPAH");
    } else if (Name == "SWPAW") {
        handle_default("SWPAw");
    } else if (Name == "SWPAX") {
        handle_default("SWPAx");
    } else if (Name == "SWPLB") {
        handle_default("SWPLB");
    } else if (Name == "SWPLH") {
        handle_default("SWPLH");
    } else if (Name == "SWPLW") {
        handle_default("SWPLw");
    } else if (Name == "SWPLX") {
        handle_default("SWPLx");
    } else if (Name == "SWPALB") {
        handle_default("SWPALB");
    } else if (Name == "SWPALH") {
        handle_default("SWPALH");
    } else if (Name == "SWPALW") {
        handle_default("SWPALw");
    } else if (Name == "SWPALX") {
        handle_default("SWPALx");

        // Good that Arm no describes their architecture as "RISC".
    } else if (Name == "LDADDB") {
        handle_default("LDADDB");
    } else if (Name == "LDADDH") {
        handle_default("LDADDH");
    } else if (Name == "LDADDW") {
        handle_default("LDADDw");
    } else if (Name == "LDADDX") {
        handle_default("LDADDx");
    } else if (Name == "LDADDAB") {
        handle_default("LDADDAB");
    } else if (Name == "LDADDAH") {
        handle_default("LDADDAH");
    } else if (Name == "LDADDAW") {
        handle_default("LDADDAw");
    } else if (Name == "LDADDAX") {
        handle_default("LDADDAx");
    } else if (Name == "LDADDLB") {
        handle_default("LDADDLB");
    } else if (Name == "LDADDLH") {
        handle_default("LDADDLH");
    } else if (Name == "LDADDLW") {
        handle_default("LDADDLw");
    } else if (Name == "LDADDLX") {
        handle_default("LDADDLx");
    } else if (Name == "LDADDALB") {
        handle_default("LDADDALB");
    } else if (Name == "LDADDALH") {
        handle_default("LDADDALH");
    } else if (Name == "LDADDALW") {
        handle_default("LDADDALw");
    } else if (Name == "LDADDALX") {
        handle_default("LDADDALx");
    } else if (Name == "LDCLRB") {
        handle_default("LDCLRB");
    } else if (Name == "LDCLRH") {
        handle_default("LDCLRH");
    } else if (Name == "LDCLRW") {
        handle_default("LDCLRw");
    } else if (Name == "LDCLRX") {
        handle_default("LDCLRx");
    } else if (Name == "LDCLRAB") {
        handle_default("LDCLRAB");
    } else if (Name == "LDCLRAH") {
        handle_default("LDCLRAH");
    } else if (Name == "LDCLRAW") {
        handle_default("LDCLRAw");
    } else if (Name == "LDCLRAX") {
        handle_default("LDCLRAx");
    } else if (Name == "LDCLRLB") {
        handle_default("LDCLRLB");
    } else if (Name == "LDCLRLH") {
        handle_default("LDCLRLH");
    } else if (Name == "LDCLRLW") {
        handle_default("LDCLRLw");
    } else if (Name == "LDCLRLX") {
        handle_default("LDCLRLx");
    } else if (Name == "LDCLRALB") {
        handle_default("LDCLRALB");
    } else if (Name == "LDCLRALH") {
        handle_default("LDCLRALH");
    } else if (Name == "LDCLRALW") {
        handle_default("LDCLRALw");
    } else if (Name == "LDCLRALX") {
        handle_default("LDCLRALx");
    } else if (Name == "LDEORB") {
        handle_default("LDEORB");
    } else if (Name == "LDEORH") {
        handle_default("LDEORH");
    } else if (Name == "LDEORW") {
        handle_default("LDEORw");
    } else if (Name == "LDEORX") {
        handle_default("LDEORx");
    } else if (Name == "LDEORAB") {
        handle_default("LDEORAB");
    } else if (Name == "LDEORAH") {
        handle_default("LDEORAH");
    } else if (Name == "LDEORAW") {
        handle_default("LDEORAw");
    } else if (Name == "LDEORAX") {
        handle_default("LDEORAx");
    } else if (Name == "LDEORLB") {
        handle_default("LDEORLB");
    } else if (Name == "LDEORLH") {
        handle_default("LDEORLH");
    } else if (Name == "LDEORLW") {
        handle_default("LDEORLw");
    } else if (Name == "LDEORLX") {
        handle_default("LDEORLx");
    } else if (Name == "LDEORALB") {
        handle_default("LDEORALB");
    } else if (Name == "LDEORALH") {
        handle_default("LDEORALH");
    } else if (Name == "LDEORALW") {
        handle_default("LDEORALw");
    } else if (Name == "LDEORALX") {
        handle_default("LDEORALx");
    } else if (Name == "LDSETB") {
        handle_default("LDSETB");
    } else if (Name == "LDSETH") {
        handle_default("LDSETH");
    } else if (Name == "LDSETW") {
        handle_default("LDSETw");
    } else if (Name == "LDSETX") {
        handle_default("LDSETx");
    } else if (Name == "LDSETAB") {
        handle_default("LDSETAB");
    } else if (Name == "LDSETAH") {
        handle_default("LDSETAH");
    } else if (Name == "LDSETAW") {
        handle_default("LDSETAw");
    } else if (Name == "LDSETAX") {
        handle_default("LDSETAx");
    } else if (Name == "LDSETLB") {
        handle_default("LDSETLB");
    } else if (Name == "LDSETLH") {
        handle_default("LDSETLH");
    } else if (Name == "LDSETLW") {
        handle_default("LDSETLw");
    } else if (Name == "LDSETLX") {
        handle_default("LDSETLx");
    } else if (Name == "LDSETALB") {
        handle_default("LDSETALB");
    } else if (Name == "LDSETALH") {
        handle_default("LDSETALH");
    } else if (Name == "LDSETALW") {
        handle_default("LDSETALw");
    } else if (Name == "LDSETALX") {
        handle_default("LDSETALx");
    } else if (Name == "LDSMAXB") {
        handle_default("LDSMAXB");
    } else if (Name == "LDSMAXH") {
        handle_default("LDSMAXH");
    } else if (Name == "LDSMAXW") {
        handle_default("LDSMAXw");
    } else if (Name == "LDSMAXX") {
        handle_default("LDSMAXx");
    } else if (Name == "LDSMAXAB") {
        handle_default("LDSMAXAB");
    } else if (Name == "LDSMAXAH") {
        handle_default("LDSMAXAH");
    } else if (Name == "LDSMAXAW") {
        handle_default("LDSMAXAw");
    } else if (Name == "LDSMAXAX") {
        handle_default("LDSMAXAx");
    } else if (Name == "LDSMAXLB") {
        handle_default("LDSMAXLB");
    } else if (Name == "LDSMAXLH") {
        handle_default("LDSMAXLH");
    } else if (Name == "LDSMAXLW") {
        handle_default("LDSMAXLw");
    } else if (Name == "LDSMAXLX") {
        handle_default("LDSMAXLx");
    } else if (Name == "LDSMAXALB") {
        handle_default("LDSMAXALB");
    } else if (Name == "LDSMAXALH") {
        handle_default("LDSMAXALH");
    } else if (Name == "LDSMAXALW") {
        handle_default("LDSMAXALw");
    } else if (Name == "LDSMAXALX") {
        handle_default("LDSMAXALx");
    } else if (Name == "LDSMINB") {
        handle_default("LDSMINB");
    } else if (Name == "LDSMINH") {
        handle_default("LDSMINH");
    } else if (Name == "LDSMINW") {
        handle_default("LDSMINw");
    } else if (Name == "LDSMINX") {
        handle_default("LDSMINx");
    } else if (Name == "LDSMINAB") {
        handle_default("LDSMINAB");
    } else if (Name == "LDSMINAH") {
        handle_default("LDSMINAH");
    } else if (Name == "LDSMINAW") {
        handle_default("LDSMINAw");
    } else if (Name == "LDSMINAX") {
        handle_default("LDSMINAx");
    } else if (Name == "LDSMINLB") {
        handle_default("LDSMINLB");
    } else if (Name == "LDSMINLH") {
        handle_default("LDSMINLH");
    } else if (Name == "LDSMINLW") {
        handle_default("LDSMINLw");
    } else if (Name == "LDSMINLX") {
        handle_default("LDSMINLx");
    } else if (Name == "LDSMINALB") {
        handle_default("LDSMINALB");
    } else if (Name == "LDSMINALH") {
        handle_default("LDSMINALH");
    } else if (Name == "LDSMINALW") {
        handle_default("LDSMINALw");
    } else if (Name == "LDSMINALX") {
        handle_default("LDSMINALx");
    } else if (Name == "LDUMAXB") {
        handle_default("LDUMAXB");
    } else if (Name == "LDUMAXH") {
        handle_default("LDUMAXH");
    } else if (Name == "LDUMAXW") {
        handle_default("LDUMAXw");
    } else if (Name == "LDUMAXX") {
        handle_default("LDUMAXx");
    } else if (Name == "LDUMAXAB") {
        handle_default("LDUMAXAB");
    } else if (Name == "LDUMAXAH") {
        handle_default("LDUMAXAH");
    } else if (Name == "LDUMAXAW") {
        handle_default("LDUMAXAw");
    } else if (Name == "LDUMAXAX") {
        handle_default("LDUMAXAx");
    } else if (Name == "LDUMAXLB") {
        handle_default("LDUMAXLB");
    } else if (Name == "LDUMAXLH") {
        handle_default("LDUMAXLH");
    } else if (Name == "LDUMAXLW") {
        handle_default("LDUMAXLw");
    } else if (Name == "LDUMAXLX") {
        handle_default("LDUMAXLx");
    } else if (Name == "LDUMAXALB") {
        handle_default("LDUMAXALB");
    } else if (Name == "LDUMAXALH") {
        handle_default("LDUMAXALH");
    } else if (Name == "LDUMAXALW") {
        handle_default("LDUMAXALw");
    } else if (Name == "LDUMAXALX") {
        handle_default("LDUMAXALx");
    } else if (Name == "LDUMINB") {
        handle_default("LDUMINB");
    } else if (Name == "LDUMINH") {
        handle_default("LDUMINH");
    } else if (Name == "LDUMINW") {
        handle_default("LDUMINw");
    } else if (Name == "LDUMINX") {
        handle_default("LDUMINx");
    } else if (Name == "LDUMINAB") {
        handle_default("LDUMINAB");
    } else if (Name == "LDUMINAH") {
        handle_default("LDUMINAH");
    } else if (Name == "LDUMINAW") {
        handle_default("LDUMINAw");
    } else if (Name == "LDUMINAX") {
        handle_default("LDUMINAx");
    } else if (Name == "LDUMINLB") {
        handle_default("LDUMINLB");
    } else if (Name == "LDUMINLH") {
        handle_default("LDUMINLH");
    } else if (Name == "LDUMINLW") {
        handle_default("LDUMINLw");
    } else if (Name == "LDUMINLX") {
        handle_default("LDUMINLx");
    } else if (Name == "LDUMINALB") {
        handle_default("LDUMINALB");
    } else if (Name == "LDUMINALH") {
        handle_default("LDUMINALH");
    } else if (Name == "LDUMINALW") {
        handle_default("LDUMINALw");
    } else if (Name == "LDUMINALX") {
        handle_default("LDUMINALx");

    } else if (Name == "LDARB") {
        handle_default("LDARB");
    } else if (Name == "LDARH") {
        handle_default("LDARH");
    } else if (Name == "LDARW") {
        handle_default("LDARw");
    } else if (Name == "LDARX") {
        handle_default("LDARx");
    } else if (Name == "LADRB") {
        handle_default("STLRB");
    } else if (Name == "STLRH") {
        handle_default("STLRH");
    } else if (Name == "STLRW") {
        handle_default("STLRw");
    } else if (Name == "STLRX") {
        handle_default("STLRx");

    } else {
        assert(false);
    }
}

} // namespace tpde_encgen::arm64
