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

namespace {

std::string format_reg(const llvm::MachineOperand &mo, std::string_view op) {
    const auto &tri =
        *mo.getParent()->getMF()->getRegInfo().getTargetRegisterInfo();
    llvm::StringRef name = tri.getName(mo.getReg());
    return name == "WZR" || name == "XZR" ? "DA_ZR" : std::string(op);
}

} // end anonymous namespace

void EncodingTargetArm64::get_inst_candidates(
    llvm::MachineInstr &mi, llvm::SmallVectorImpl<MICandidate> &candidates) {
    const llvm::LLVMTargetMachine &TM   = mi.getMF()->getTarget();
    const llvm::MCInstrInfo       &MCII = *TM.getMCInstrInfo();
    const llvm::MCInstrDesc       &MCID = MCII.get(mi.getOpcode());
    (void)MCID;

    llvm::StringRef Name = MCII.getName(mi.getOpcode());

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

    const auto handle_mem_imm = [&](std::string_view mnem,
                                    std::string_view mnemu,
                                    unsigned         shift) {
        auto cond1 = std::format("encodeable_with_mem_uoff12(this, {:#x}, {})",
                                 mi.getOperand(2).getImm(),
                                 shift);
        candidates.emplace_back(
            1,
            cond1,
            [mnem](std::string                        &buf,
                   const llvm::MachineInstr           &mi,
                   llvm::SmallVectorImpl<std::string> &ops) {
                std::string val_reg = "";
                unsigned    op_idx  = 0;
                if (mi.getOperand(0).isImm()) {
                    val_reg =
                        std::format("(Da64PrfOp){}", mi.getOperand(0).getImm());
                } else {
                    val_reg = format_reg(mi.getOperand(0), ops[op_idx++]);
                }
                std::format_to(std::back_inserter(buf),
                               "        ASMD({}, {}, {}.first, {}.second);\n",
                               mnem,
                               val_reg,
                               ops[op_idx],
                               ops[op_idx]);
            });
        (void)mnemu;
    };
    const auto handle_shift_imm = [&](std::string_view mnem) {
        auto cond = std::format("encodeable_as_imm()");
        candidates.emplace_back(
            2,
            cond,
            [mnem](std::string                        &buf,
                   const llvm::MachineInstr           &mi,
                   llvm::SmallVectorImpl<std::string> &ops) {
                std::format_to(
                    std::back_inserter(buf), "        ASMD({}", mnem);
                for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n;
                     i++) {
                    buf += ", " + format_reg(mi.getOperand(i), ops[i]);
                }
                std::format_to(std::back_inserter(buf), ");\n");
            });
    };
    const auto handle_arith_imm = [&](std::string_view mnem) {
        auto cond = std::format("encodeable_as_immarith()");
        candidates.emplace_back(
            2,
            cond,
            [mnem](std::string                        &buf,
                   const llvm::MachineInstr           &mi,
                   llvm::SmallVectorImpl<std::string> &ops) {
                auto dst = format_reg(mi.getOperand(0), ops[0]);
                auto src = format_reg(mi.getOperand(1), ops[1]);
                std::format_to(std::back_inserter(buf),
                               "        ASMD({}, {}, {}, {});",
                               mnem,
                               dst,
                               src,
                               ops[2]);
            });
        if (!mnem.starts_with("ADD")) {
            return;
        }
        candidates.emplace_back(
            1,
            cond,
            [mnem](std::string                        &buf,
                   const llvm::MachineInstr           &mi,
                   llvm::SmallVectorImpl<std::string> &ops) {
                auto dst = format_reg(mi.getOperand(0), ops[0]);
                auto src = format_reg(mi.getOperand(1), ops[1]);
                std::format_to(std::back_inserter(buf),
                               "        ASMD({}, {}, {}, {});",
                               mnem,
                               dst,
                               ops[2],
                               src);
            });
    };
    const auto handle_default = [&](std::string_view mnem,
                                    std::string      extra_ops = "") {
        candidates.emplace_back([mnem, extra_ops](
                                    std::string                        &buf,
                                    const llvm::MachineInstr           &mi,
                                    llvm::SmallVectorImpl<std::string> &ops) {
            std::format_to(std::back_inserter(buf), "        ASMD({}", mnem);
            unsigned reg_idx = 0;
            for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n; i++) {
                const auto &op = mi.getOperand(i);
                if (op.isReg()) {
                    if (!op.isTied() || !op.isUse()) {
                        buf += ", " + format_reg(op, ops[reg_idx++]);
                    }
                } else if (op.isImm()) {
                    if (mnem.starts_with("CCMP") || mnem.starts_with("FCCMP")
                        || mnem.starts_with("CS")
                        || mnem.starts_with("FCSEL")) {
                        std::array<std::string_view, 16> ccs = {"DA_EQ",
                                                                "DA_NE",
                                                                "DA_HS",
                                                                "DA_LO",
                                                                "DA_MI",
                                                                "DA_PL",
                                                                "DA_VS",
                                                                "DA_VC",
                                                                "DA_HI",
                                                                "DA_LS",
                                                                "DA_GE",
                                                                "DA_LT",
                                                                "DA_GT",
                                                                "DA_LE",
                                                                "DA_AL",
                                                                "DA_NV"};
                        std::format_to(
                            std::back_inserter(buf), ", {}", ccs[op.getImm()]);
                    } else if (op.getOperandNo() == 0
                               && mnem.starts_with("PRFM")) {
                        std::format_to(std::back_inserter(buf),
                                       ", (Da64PrfOp){}",
                                       op.getImm());
                    } else {
                        std::format_to(
                            std::back_inserter(buf), ", {:#x}", op.getImm());
                    }
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
            std::format_to(std::back_inserter(buf), "        ASMD({}", mnem);
            unsigned reg_idx = 0;
            for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n; i++) {
                const auto &op = mi.getOperand(i);
                if (op.isReg()) {
                    if (!op.isTied() || !op.isUse()) {
                        buf += ", " + format_reg(op, ops[reg_idx++]);
                    }
                } else if (op.isImm()) {
                } else {
                    assert(false);
                }
            }
            std::format_to(std::back_inserter(buf), "{});\n", extra_ops);
        });
    };
    // atomic memory operations (LDADD) have their source and dest operands
    // swapped
    const auto handle_atomic_mem_op = [&](std::string_view mnem) {
        candidates.emplace_back([mnem](
                                    std::string                        &buf,
                                    const llvm::MachineInstr           &mi,
                                    llvm::SmallVectorImpl<std::string> &ops) {
            std::format_to(std::back_inserter(buf), "        ASMD({}", mnem);
            assert(mi.getNumExplicitOperands() == 3);
            constexpr std::array<unsigned, 3> op_order = {1, 0, 2};
            for (unsigned i : op_order) {
                buf += ", " + format_reg(mi.getOperand(i), ops[i]);
            }
            std::format_to(std::back_inserter(buf), ");\n");
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
        handle_mem_imm("LDRBu", "LDURB", 0);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRBu");
    } else if (Name == "LDRHHui") {
        handle_mem_imm("LDRHu", "LDURH", 1);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRHu");
    } else if (Name == "LDRWui") {
        handle_mem_imm("LDRwu", "LDURw", 2);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRwu");
    } else if (Name == "LDRXui") {
        handle_mem_imm("LDRxu", "LDURx", 3);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRxu");
    } else if (Name == "PRFMui") {
        handle_mem_imm("PRFMu", "PRFUMu", 3);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("PRFMu");
    } else if (Name == "LDRBui") {
        handle_mem_imm("LDRbu", "LDURb", 0);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRbu");
    } else if (Name == "LDRHui") {
        handle_mem_imm("LDRhu", "LDURh", 1);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRhu");
    } else if (Name == "LDRSui") {
        handle_mem_imm("LDRsu", "LDURs", 2);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRsu");
    } else if (Name == "LDRDui") {
        handle_mem_imm("LDRdu", "LDURd", 3);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRdu");
    } else if (Name == "LDRQui") {
        handle_mem_imm("LDRqu", "LDURq", 4);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("LDRqu");
    } else if (Name == "STRBBui") {
        handle_mem_imm("STRBu", "STURB", 0);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRBu");
    } else if (Name == "STRHHui") {
        handle_mem_imm("STRHu", "STURH", 1);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRHu");
    } else if (Name == "STRWui") {
        handle_mem_imm("STRwu", "STURw", 2);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRwu");
    } else if (Name == "STRXui") {
        handle_mem_imm("STRxu", "STURx", 3);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRxu");
    } else if (Name == "STRBui") {
        handle_mem_imm("STRbu", "STURB", 0);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRbu");
    } else if (Name == "STRHui") {
        handle_mem_imm("STRhu", "STURh", 1);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRhu");
    } else if (Name == "STRSui") {
        handle_mem_imm("STRsu", "STURs", 2);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRsu");
    } else if (Name == "STRDui") {
        handle_mem_imm("STRdu", "STURd", 3);
        // TODO: If offset is zero, handle expr with base+index
        handle_default("STRdu");
    } else if (Name == "STRQui") {
        handle_mem_imm("STRqu", "STURq", 4);
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
    } else if (Name == "STLRB") {
        // TODO: Handle expr with base+off, merge offsets
        handle_default("STLRB");
    } else if (Name == "LDRSBWui") {
        handle_mem_imm("LDRSBwu", "LDURSBw", 0);
        // TODO: Handle expr with base+off, merge offsets
        handle_default("LDRSBwu");
    } else if (Name == "LDRSHWui") {
        handle_mem_imm("LDRSHwu", "LDURSHw", 1);
        // TODO: Handle expr with base+off, merge offsets
        handle_default("LDRSHwu");
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
        handle_shift_imm("LSLwi");
        handle_default("LSLVw");
    } else if (Name == "LSLVXr") {
        handle_shift_imm("LSLxi");
        handle_default("LSLVx");
    } else if (Name == "LSRVWr") {
        handle_shift_imm("LSRwi");
        handle_default("LSRVw");
    } else if (Name == "LSRVXr") {
        handle_shift_imm("LSRxi");
        handle_default("LSRVx");
    } else if (Name == "ASRVWr") {
        handle_shift_imm("ASRwi");
        handle_default("ASRVw");
    } else if (Name == "ASRVXr") {
        handle_shift_imm("ASRxi");
        handle_default("ASRVx");
    } else if (Name == "RORVWr") {
        handle_shift_imm("RORwi");
        handle_default("RORVw");
    } else if (Name == "RORVXr") {
        handle_shift_imm("RORxi");
        handle_default("RORVx");
    } else if (Name == "ADDWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDwi", std::format(", {}", imm << (shift & 0x1f)));
    } else if (Name == "ADDXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "ADDSWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDSwi", std::format(", {}", imm << (shift & 0x1f)));
    } else if (Name == "ADDSXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("ADDSxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "SUBWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBwi", std::format(", {}", imm << (shift & 0x1f)));
    } else if (Name == "SUBXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "SUBSWri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBSwi", std::format(", {}", imm << (shift & 0x1f)));
    } else if (Name == "SUBSXri") {
        unsigned imm   = mi.getOperand(2).getImm();
        unsigned shift = mi.getOperand(3).getImm();
        handle_noimm("SUBSxi", std::format(", {}", imm << (shift & 0x3f)));
    } else if (Name == "ADDWrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDw_lsl", "ADDw_lsr", "ADDw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("ADDwi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
    } else if (Name == "ADDXrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDx_lsl", "ADDx_lsr", "ADDx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("ADDxi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "ADDSWrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDSw_lsl", "ADDSw_lsr", "ADDSw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("ADDSwi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
    } else if (Name == "ADDSXrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "ADDSx_lsl", "ADDSx_lsr", "ADDSx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("ADDSxi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBWrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBw_lsl", "SUBw_lsr", "SUBw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("SUBwi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
    } else if (Name == "SUBXrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBx_lsl", "SUBx_lsr", "SUBx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("SUBxi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x3f));
    } else if (Name == "SUBSWrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBSw_lsl", "SUBSw_lsr", "SUBSw_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("SUBSwi");
        }
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
    } else if (Name == "SUBSXrs") {
        // TODO: Handle expr with only a shifted index if imm==0
        std::array<std::string_view, 4> mnems{
            "SUBSx_lsl", "SUBSx_lsr", "SUBSx_asr"};
        unsigned imm = mi.getOperand(3).getImm();
        if (imm == 0) { // TODO: apply shift to immediate?
            handle_arith_imm("SUBSxi");
        }
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
        handle_noimm(mnems[imm >> 3], std::format(", {}", imm & 0x7));
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
    } else if (Name == "REVWr") {
        handle_default("REV32w");
    } else if (Name == "REVXr") {
        handle_default("REV64x");
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_noimm(mnems[imm >> 6], std::format(", {}", imm & 0x1f));
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
        handle_default("FCMP_s");
    } else if (Name == "FCMPDrr") {
        handle_default("FCMP_d");
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
        handle_atomic_mem_op("SWPB");
    } else if (Name == "SWPH") {
        handle_atomic_mem_op("SWPH");
    } else if (Name == "SWPW") {
        handle_atomic_mem_op("SWPw");
    } else if (Name == "SWPX") {
        handle_atomic_mem_op("SWPx");
    } else if (Name == "SWPAB") {
        handle_atomic_mem_op("SWPAB");
    } else if (Name == "SWPAH") {
        handle_atomic_mem_op("SWPAH");
    } else if (Name == "SWPAW") {
        handle_atomic_mem_op("SWPAw");
    } else if (Name == "SWPAX") {
        handle_atomic_mem_op("SWPAx");
    } else if (Name == "SWPLB") {
        handle_atomic_mem_op("SWPLB");
    } else if (Name == "SWPLH") {
        handle_atomic_mem_op("SWPLH");
    } else if (Name == "SWPLW") {
        handle_atomic_mem_op("SWPLw");
    } else if (Name == "SWPLX") {
        handle_atomic_mem_op("SWPLx");
    } else if (Name == "SWPALB") {
        handle_atomic_mem_op("SWPALB");
    } else if (Name == "SWPALH") {
        handle_atomic_mem_op("SWPALH");
    } else if (Name == "SWPALW") {
        handle_atomic_mem_op("SWPALw");
    } else if (Name == "SWPALX") {
        handle_atomic_mem_op("SWPALx");

        // Good that Arm no describes their architecture as "RISC".
    } else if (Name == "LDADDB") {
        handle_atomic_mem_op("LDADDB");
    } else if (Name == "LDADDH") {
        handle_atomic_mem_op("LDADDH");
    } else if (Name == "LDADDW") {
        handle_atomic_mem_op("LDADDw");
    } else if (Name == "LDADDX") {
        handle_atomic_mem_op("LDADDx");
    } else if (Name == "LDADDAB") {
        handle_atomic_mem_op("LDADDAB");
    } else if (Name == "LDADDAH") {
        handle_atomic_mem_op("LDADDAH");
    } else if (Name == "LDADDAW") {
        handle_atomic_mem_op("LDADDAw");
    } else if (Name == "LDADDAX") {
        handle_atomic_mem_op("LDADDAx");
    } else if (Name == "LDADDLB") {
        handle_atomic_mem_op("LDADDLB");
    } else if (Name == "LDADDLH") {
        handle_atomic_mem_op("LDADDLH");
    } else if (Name == "LDADDLW") {
        handle_atomic_mem_op("LDADDLw");
    } else if (Name == "LDADDLX") {
        handle_atomic_mem_op("LDADDLx");
    } else if (Name == "LDADDALB") {
        handle_atomic_mem_op("LDADDALB");
    } else if (Name == "LDADDALH") {
        handle_atomic_mem_op("LDADDALH");
    } else if (Name == "LDADDALW") {
        handle_atomic_mem_op("LDADDALw");
    } else if (Name == "LDADDALX") {
        handle_atomic_mem_op("LDADDALx");
    } else if (Name == "LDCLRB") {
        handle_atomic_mem_op("LDCLRB");
    } else if (Name == "LDCLRH") {
        handle_atomic_mem_op("LDCLRH");
    } else if (Name == "LDCLRW") {
        handle_atomic_mem_op("LDCLRw");
    } else if (Name == "LDCLRX") {
        handle_atomic_mem_op("LDCLRx");
    } else if (Name == "LDCLRAB") {
        handle_atomic_mem_op("LDCLRAB");
    } else if (Name == "LDCLRAH") {
        handle_atomic_mem_op("LDCLRAH");
    } else if (Name == "LDCLRAW") {
        handle_atomic_mem_op("LDCLRAw");
    } else if (Name == "LDCLRAX") {
        handle_atomic_mem_op("LDCLRAx");
    } else if (Name == "LDCLRLB") {
        handle_atomic_mem_op("LDCLRLB");
    } else if (Name == "LDCLRLH") {
        handle_atomic_mem_op("LDCLRLH");
    } else if (Name == "LDCLRLW") {
        handle_atomic_mem_op("LDCLRLw");
    } else if (Name == "LDCLRLX") {
        handle_atomic_mem_op("LDCLRLx");
    } else if (Name == "LDCLRALB") {
        handle_atomic_mem_op("LDCLRALB");
    } else if (Name == "LDCLRALH") {
        handle_atomic_mem_op("LDCLRALH");
    } else if (Name == "LDCLRALW") {
        handle_atomic_mem_op("LDCLRALw");
    } else if (Name == "LDCLRALX") {
        handle_atomic_mem_op("LDCLRALx");
    } else if (Name == "LDEORB") {
        handle_atomic_mem_op("LDEORB");
    } else if (Name == "LDEORH") {
        handle_atomic_mem_op("LDEORH");
    } else if (Name == "LDEORW") {
        handle_atomic_mem_op("LDEORw");
    } else if (Name == "LDEORX") {
        handle_atomic_mem_op("LDEORx");
    } else if (Name == "LDEORAB") {
        handle_atomic_mem_op("LDEORAB");
    } else if (Name == "LDEORAH") {
        handle_atomic_mem_op("LDEORAH");
    } else if (Name == "LDEORAW") {
        handle_atomic_mem_op("LDEORAw");
    } else if (Name == "LDEORAX") {
        handle_atomic_mem_op("LDEORAx");
    } else if (Name == "LDEORLB") {
        handle_atomic_mem_op("LDEORLB");
    } else if (Name == "LDEORLH") {
        handle_atomic_mem_op("LDEORLH");
    } else if (Name == "LDEORLW") {
        handle_atomic_mem_op("LDEORLw");
    } else if (Name == "LDEORLX") {
        handle_atomic_mem_op("LDEORLx");
    } else if (Name == "LDEORALB") {
        handle_atomic_mem_op("LDEORALB");
    } else if (Name == "LDEORALH") {
        handle_atomic_mem_op("LDEORALH");
    } else if (Name == "LDEORALW") {
        handle_atomic_mem_op("LDEORALw");
    } else if (Name == "LDEORALX") {
        handle_atomic_mem_op("LDEORALx");
    } else if (Name == "LDSETB") {
        handle_atomic_mem_op("LDSETB");
    } else if (Name == "LDSETH") {
        handle_atomic_mem_op("LDSETH");
    } else if (Name == "LDSETW") {
        handle_atomic_mem_op("LDSETw");
    } else if (Name == "LDSETX") {
        handle_atomic_mem_op("LDSETx");
    } else if (Name == "LDSETAB") {
        handle_atomic_mem_op("LDSETAB");
    } else if (Name == "LDSETAH") {
        handle_atomic_mem_op("LDSETAH");
    } else if (Name == "LDSETAW") {
        handle_atomic_mem_op("LDSETAw");
    } else if (Name == "LDSETAX") {
        handle_atomic_mem_op("LDSETAx");
    } else if (Name == "LDSETLB") {
        handle_atomic_mem_op("LDSETLB");
    } else if (Name == "LDSETLH") {
        handle_atomic_mem_op("LDSETLH");
    } else if (Name == "LDSETLW") {
        handle_atomic_mem_op("LDSETLw");
    } else if (Name == "LDSETLX") {
        handle_atomic_mem_op("LDSETLx");
    } else if (Name == "LDSETALB") {
        handle_atomic_mem_op("LDSETALB");
    } else if (Name == "LDSETALH") {
        handle_atomic_mem_op("LDSETALH");
    } else if (Name == "LDSETALW") {
        handle_atomic_mem_op("LDSETALw");
    } else if (Name == "LDSETALX") {
        handle_atomic_mem_op("LDSETALx");
    } else if (Name == "LDSMAXB") {
        handle_atomic_mem_op("LDSMAXB");
    } else if (Name == "LDSMAXH") {
        handle_atomic_mem_op("LDSMAXH");
    } else if (Name == "LDSMAXW") {
        handle_atomic_mem_op("LDSMAXw");
    } else if (Name == "LDSMAXX") {
        handle_atomic_mem_op("LDSMAXx");
    } else if (Name == "LDSMAXAB") {
        handle_atomic_mem_op("LDSMAXAB");
    } else if (Name == "LDSMAXAH") {
        handle_atomic_mem_op("LDSMAXAH");
    } else if (Name == "LDSMAXAW") {
        handle_atomic_mem_op("LDSMAXAw");
    } else if (Name == "LDSMAXAX") {
        handle_atomic_mem_op("LDSMAXAx");
    } else if (Name == "LDSMAXLB") {
        handle_atomic_mem_op("LDSMAXLB");
    } else if (Name == "LDSMAXLH") {
        handle_atomic_mem_op("LDSMAXLH");
    } else if (Name == "LDSMAXLW") {
        handle_atomic_mem_op("LDSMAXLw");
    } else if (Name == "LDSMAXLX") {
        handle_atomic_mem_op("LDSMAXLx");
    } else if (Name == "LDSMAXALB") {
        handle_atomic_mem_op("LDSMAXALB");
    } else if (Name == "LDSMAXALH") {
        handle_atomic_mem_op("LDSMAXALH");
    } else if (Name == "LDSMAXALW") {
        handle_atomic_mem_op("LDSMAXALw");
    } else if (Name == "LDSMAXALX") {
        handle_atomic_mem_op("LDSMAXALx");
    } else if (Name == "LDSMINB") {
        handle_atomic_mem_op("LDSMINB");
    } else if (Name == "LDSMINH") {
        handle_atomic_mem_op("LDSMINH");
    } else if (Name == "LDSMINW") {
        handle_atomic_mem_op("LDSMINw");
    } else if (Name == "LDSMINX") {
        handle_atomic_mem_op("LDSMINx");
    } else if (Name == "LDSMINAB") {
        handle_atomic_mem_op("LDSMINAB");
    } else if (Name == "LDSMINAH") {
        handle_atomic_mem_op("LDSMINAH");
    } else if (Name == "LDSMINAW") {
        handle_atomic_mem_op("LDSMINAw");
    } else if (Name == "LDSMINAX") {
        handle_atomic_mem_op("LDSMINAx");
    } else if (Name == "LDSMINLB") {
        handle_atomic_mem_op("LDSMINLB");
    } else if (Name == "LDSMINLH") {
        handle_atomic_mem_op("LDSMINLH");
    } else if (Name == "LDSMINLW") {
        handle_atomic_mem_op("LDSMINLw");
    } else if (Name == "LDSMINLX") {
        handle_atomic_mem_op("LDSMINLx");
    } else if (Name == "LDSMINALB") {
        handle_atomic_mem_op("LDSMINALB");
    } else if (Name == "LDSMINALH") {
        handle_atomic_mem_op("LDSMINALH");
    } else if (Name == "LDSMINALW") {
        handle_atomic_mem_op("LDSMINALw");
    } else if (Name == "LDSMINALX") {
        handle_atomic_mem_op("LDSMINALx");
    } else if (Name == "LDUMAXB") {
        handle_atomic_mem_op("LDUMAXB");
    } else if (Name == "LDUMAXH") {
        handle_atomic_mem_op("LDUMAXH");
    } else if (Name == "LDUMAXW") {
        handle_atomic_mem_op("LDUMAXw");
    } else if (Name == "LDUMAXX") {
        handle_atomic_mem_op("LDUMAXx");
    } else if (Name == "LDUMAXAB") {
        handle_atomic_mem_op("LDUMAXAB");
    } else if (Name == "LDUMAXAH") {
        handle_atomic_mem_op("LDUMAXAH");
    } else if (Name == "LDUMAXAW") {
        handle_atomic_mem_op("LDUMAXAw");
    } else if (Name == "LDUMAXAX") {
        handle_atomic_mem_op("LDUMAXAx");
    } else if (Name == "LDUMAXLB") {
        handle_atomic_mem_op("LDUMAXLB");
    } else if (Name == "LDUMAXLH") {
        handle_atomic_mem_op("LDUMAXLH");
    } else if (Name == "LDUMAXLW") {
        handle_atomic_mem_op("LDUMAXLw");
    } else if (Name == "LDUMAXLX") {
        handle_atomic_mem_op("LDUMAXLx");
    } else if (Name == "LDUMAXALB") {
        handle_atomic_mem_op("LDUMAXALB");
    } else if (Name == "LDUMAXALH") {
        handle_atomic_mem_op("LDUMAXALH");
    } else if (Name == "LDUMAXALW") {
        handle_atomic_mem_op("LDUMAXALw");
    } else if (Name == "LDUMAXALX") {
        handle_atomic_mem_op("LDUMAXALx");
    } else if (Name == "LDUMINB") {
        handle_atomic_mem_op("LDUMINB");
    } else if (Name == "LDUMINH") {
        handle_atomic_mem_op("LDUMINH");
    } else if (Name == "LDUMINW") {
        handle_atomic_mem_op("LDUMINw");
    } else if (Name == "LDUMINX") {
        handle_atomic_mem_op("LDUMINx");
    } else if (Name == "LDUMINAB") {
        handle_atomic_mem_op("LDUMINAB");
    } else if (Name == "LDUMINAH") {
        handle_atomic_mem_op("LDUMINAH");
    } else if (Name == "LDUMINAW") {
        handle_atomic_mem_op("LDUMINAw");
    } else if (Name == "LDUMINAX") {
        handle_atomic_mem_op("LDUMINAx");
    } else if (Name == "LDUMINLB") {
        handle_atomic_mem_op("LDUMINLB");
    } else if (Name == "LDUMINLH") {
        handle_atomic_mem_op("LDUMINLH");
    } else if (Name == "LDUMINLW") {
        handle_atomic_mem_op("LDUMINLw");
    } else if (Name == "LDUMINLX") {
        handle_atomic_mem_op("LDUMINLx");
    } else if (Name == "LDUMINALB") {
        handle_atomic_mem_op("LDUMINALB");
    } else if (Name == "LDUMINALH") {
        handle_atomic_mem_op("LDUMINALH");
    } else if (Name == "LDUMINALW") {
        handle_atomic_mem_op("LDUMINALw");
    } else if (Name == "LDUMINALX") {
        handle_atomic_mem_op("LDUMINALx");

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
        llvm::errs() << "ERROR: unhandled instruction " << Name << "\n";
        assert(false);
    }
}

} // namespace tpde_encgen::arm64
