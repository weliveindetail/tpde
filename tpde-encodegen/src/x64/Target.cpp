// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#include "x64/Target.hpp"

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

namespace tpde_encgen::x64 {

void EncodingTargetX64::get_inst_candidates(
    llvm::MachineInstr &mi, llvm::SmallVectorImpl<MICandidate> &candidates) {
    const llvm::LLVMTargetMachine &TM   = mi.getMF()->getTarget();
    const llvm::MCInstrInfo       &MCII = *TM.getMCInstrInfo();
    const llvm::MCInstrDesc       &MCID = MCII.get(mi.getOpcode());
    (void)MCID;

    llvm::StringRef Name = MCII.getName(mi.getOpcode());
    llvm::dbgs() << "get for " << Name << "\n";

    const auto handle_immrepl = [&](std::string_view mnem,
                                    unsigned         replop_idx,
                                    int              memop_start = -1) {
        if (memop_start >= 0 && mi.getOperand(memop_start).getReg().isValid()
            && mi.getOperand(memop_start + 3).isImm()) {
            bool has_idx = mi.getOperand(memop_start + 2).getReg().isValid();
            unsigned sc = has_idx ? mi.getOperand(memop_start + 1).getImm() : 0;
            std::string_view idx  = has_idx ? "FE_AX" : "FE_NOREG";
            auto             off  = mi.getOperand(memop_start + 3).getImm();
            auto             cond = std::format(
                "encodeable_with(FE_MEM(FE_NOREG, {}, {}, {}))", sc, idx, off);

            candidates.emplace_back(
                MICandidate::Cond{replop_idx, "encodeable_as_imm32_sext()"},
                MICandidate::Cond{(unsigned)memop_start, cond},
                [has_idx, mnem, memop_start, replop_idx](
                    std::string                        &buf,
                    const llvm::MachineInstr           &mi,
                    llvm::SmallVectorImpl<std::string> &ops) {
                    std::format_to(
                        std::back_inserter(buf), "        ASMD({}", mnem);
                    unsigned reg_idx = 0;
                    for (unsigned i = 0, n = mi.getNumExplicitOperands();
                         i != n;
                         i++) {
                        const auto &op = mi.getOperand(i);
                        if (i == (unsigned)memop_start) {
                            if (has_idx) { // Need to replace index register
                                std::format_to(std::back_inserter(buf),
                                               ", FE_MEM({}.base, "
                                               "{}.scale, {}, {}.off)",
                                               ops[reg_idx],
                                               ops[reg_idx],
                                               ops[reg_idx + 1],
                                               ops[reg_idx]);
                            } else {
                                std::format_to(std::back_inserter(buf),
                                               ", {}",
                                               ops[reg_idx]);
                            }
                            reg_idx += 3;
                            i       += 4;
                        } else if (i == replop_idx) {
                            assert(op.isReg());
                            std::format_to(std::back_inserter(buf),
                                           ", {}",
                                           ops[reg_idx++]);
                        } else if (op.isReg()) {
                            if (op.isTied() && op.isUse()) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {}",
                                           ops[reg_idx++]);
                        } else if (op.isImm()) {
                            if (mnem.starts_with("CMOV")
                                || mnem.starts_with("SET")) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {:#x}",
                                           op.getImm());
                        }
                    }
                    std::format_to(std::back_inserter(buf), ");\n");
                });
        }
        // TODO: better code, less copy-paste, all cases, etc.
        // Difficult, because at condition time, we don't know the operands.
        if (memop_start >= 0
            && mi.getOperand(memop_start + 0).getReg().isValid()
            && mi.getOperand(memop_start + 2).getReg().isValid()
            && mi.getOperand(memop_start + 1).getImm() == 1
            && mi.getOperand(memop_start + 3).isImm()) {
            auto off  = mi.getOperand(memop_start + 3).getImm();
            auto cond = std::format(
                "encodeable_with(FE_MEM(FE_AX, 0, FE_NOREG, {}))", off);

            candidates.emplace_back(
                MICandidate::Cond{replop_idx, "encodeable_as_imm32_sext()"},
                MICandidate::Cond{(unsigned)memop_start + 2, cond},
                [mnem, memop_start, replop_idx](
                    std::string                        &buf,
                    const llvm::MachineInstr           &mi,
                    llvm::SmallVectorImpl<std::string> &ops) {
                    std::format_to(
                        std::back_inserter(buf), "        ASMD({}", mnem);
                    unsigned reg_idx = 0;
                    for (unsigned i = 0, n = mi.getNumExplicitOperands();
                         i != n;
                         i++) {
                        const auto &op = mi.getOperand(i);
                        if (i == (unsigned)memop_start) {
                            std::format_to(
                                std::back_inserter(buf),
                                ", FE_MEM({}, {}.scale, {}.idx, {}.off)",
                                ops[reg_idx],
                                ops[reg_idx + 1],
                                ops[reg_idx + 1],
                                ops[reg_idx + 1]);
                            reg_idx += 3;
                            i       += 4;
                        } else if (i == replop_idx) {
                            assert(op.isReg());
                            std::format_to(std::back_inserter(buf),
                                           ", {}",
                                           ops[reg_idx++]);
                        } else if (op.isReg()) {
                            if (op.isTied() && op.isUse()) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {}",
                                           ops[reg_idx++]);
                        } else if (op.isImm()) {
                            if (mnem.starts_with("CMOV")
                                || mnem.starts_with("SET")) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {:#x}",
                                           op.getImm());
                        }
                    }
                    std::format_to(std::back_inserter(buf), ");\n");
                });
        }
        candidates.emplace_back(
            replop_idx,
            "encodeable_as_imm32_sext()",
            [mnem, replop_idx, memop_start](
                std::string                        &buf,
                const llvm::MachineInstr           &mi,
                llvm::SmallVectorImpl<std::string> &ops) {
                std::format_to(
                    std::back_inserter(buf), "        ASMD({}", mnem);
                unsigned         reg_idx = 0;
                std::string_view cp_sym  = "";
                bool             has_imm = false;
                for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n;
                     i++) {
                    const auto &op = mi.getOperand(i);
                    if (i == (unsigned)memop_start
                        && mi.getOperand(i + 3).isCPI()) {
                        // idx as noreg; rip is ignored
                        cp_sym   = ops[reg_idx + 1];
                        reg_idx += 2;
                        i       += 4;
                        std::format_to(std::back_inserter(buf),
                                       ", FE_MEM(FE_IP, 0, FE_NOREG, 0)");
                    } else if (i == (unsigned)memop_start) {
                        std::string_view base =
                            op.getReg().isValid() ? ops[reg_idx] : "FE_NOREG"sv;
                        unsigned sc = mi.getOperand(i + 2).getReg().isValid()
                                          ? mi.getOperand(i + 1).getImm()
                                          : 0;
                        std::string_view idx =
                            mi.getOperand(i + 2).getReg().isValid()
                                ? ops[reg_idx + 1]
                                : "FE_NOREG"sv;
                        auto off = mi.getOperand(i + 3).getImm();
                        assert(!mi.getOperand(i + 4).getReg().isValid());
                        reg_idx += 3;
                        i       += 4;
                        std::format_to(std::back_inserter(buf),
                                       ", FE_MEM({}, {}, {}, {})",
                                       base,
                                       sc,
                                       idx,
                                       off);
                    } else if (i == replop_idx) {
                        assert(op.isReg());
                        has_imm = true;
                        std::format_to(
                            std::back_inserter(buf), ", {}", ops[reg_idx++]);
                    } else if (op.isReg()) {
                        if (op.isTied() && op.isUse()) {
                            continue;
                        }
                        std::format_to(
                            std::back_inserter(buf), ", {}", ops[reg_idx++]);
                    } else if (op.isImm()) {
                        if (mnem.starts_with("CMOV")
                            || mnem.starts_with("SET")) {
                            continue;
                        }
                        has_imm = true;
                        std::format_to(
                            std::back_inserter(buf), ", {:#x}", op.getImm());
                    }
                }
                if (replop_idx
                    >= mi.getNumExplicitOperands()) { // SHL etc. with CL
                    has_imm = true;
                    std::format_to(
                        std::back_inserter(buf), ", {}", ops[reg_idx++]);
                }
                std::format_to(std::back_inserter(buf), ");\n");
                if (!cp_sym.empty()) {
                    assert(!has_imm && "CPI with imm unsupported!");
                    std::format_to(
                        std::back_inserter(buf),
                        "        "
                        "derived()->assembler.reloc_text_pc32({}, "
                        "derived()->assembler.text_cur_off() - 4, -4);\n",
                        cp_sym);
                }
            });
    };
    const auto handle_memrepl = [&](std::string_view mnem,
                                    unsigned         replop_idx) {
        candidates.emplace_back(
            replop_idx,
            "encodeable_as_mem()",
            [mnem, replop_idx](std::string                        &buf,
                               const llvm::MachineInstr           &mi,
                               llvm::SmallVectorImpl<std::string> &ops) {
                std::format_to(
                    std::back_inserter(buf), "        ASMD({}", mnem);
                unsigned reg_idx = 0;
                for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n;
                     i++) {
                    const auto &op = mi.getOperand(i);
                    if (i == replop_idx) {
                        assert(op.isReg());
                        std::format_to(
                            std::back_inserter(buf), ", {}", ops[reg_idx++]);
                    } else if (op.isReg()) {
                        if (op.isTied() && op.isUse()) {
                            continue;
                        }
                        std::format_to(
                            std::back_inserter(buf), ", {}", ops[reg_idx++]);
                    } else if (op.isImm()) {
                        if (mnem.starts_with("CMOV")
                            || mnem.starts_with("SET")) {
                            continue;
                        }
                        std::format_to(
                            std::back_inserter(buf), ", {:#x}", op.getImm());
                    }
                }
                std::format_to(std::back_inserter(buf), ");\n");
            });
    };
    const auto handle_default = [&](std::string_view mnem,
                                    int              memop_start = -1,
                                    std::string_view extra_ops   = ""sv) {
        if (memop_start >= 0 && mi.getOperand(memop_start).getReg().isValid()
            && mi.getOperand(memop_start + 3).isImm()) {
            bool has_idx = mi.getOperand(memop_start + 2).getReg().isValid();
            unsigned sc = has_idx ? mi.getOperand(memop_start + 1).getImm() : 0;
            std::string_view idx  = has_idx ? "FE_AX" : "FE_NOREG";
            auto             off  = mi.getOperand(memop_start + 3).getImm();
            auto             cond = std::format(
                "encodeable_with(FE_MEM(FE_NOREG, {}, {}, {}))", sc, idx, off);

            candidates.emplace_back(
                memop_start,
                cond,
                [has_idx, mnem, memop_start, extra_ops](
                    std::string                        &buf,
                    const llvm::MachineInstr           &mi,
                    llvm::SmallVectorImpl<std::string> &ops) {
                    std::format_to(
                        std::back_inserter(buf), "        ASMD({}", mnem);
                    unsigned reg_idx = 0;
                    for (unsigned i = 0, n = mi.getNumExplicitOperands();
                         i != n;
                         i++) {
                        const auto &op = mi.getOperand(i);
                        if (i == (unsigned)memop_start) {
                            if (has_idx) { // Need to replace index register
                                std::format_to(std::back_inserter(buf),
                                               ", FE_MEM({}.base, "
                                               "{}.scale, {}, {}.off)",
                                               ops[reg_idx],
                                               ops[reg_idx],
                                               ops[reg_idx + 1],
                                               ops[reg_idx]);
                            } else {
                                std::format_to(std::back_inserter(buf),
                                               ", {}",
                                               ops[reg_idx]);
                            }
                            reg_idx += 3;
                            i       += 4;
                        } else if (op.isReg()) {
                            if (op.isTied() && op.isUse()) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {}",
                                           ops[reg_idx++]);
                        } else if (op.isImm()) {
                            if (mnem.starts_with("CMOV")
                                || mnem.starts_with("SET")) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {:#x}",
                                           op.getImm());
                        }
                    }
                    std::format_to(
                        std::back_inserter(buf), "{});\n", extra_ops);
                });
        }
        // TODO: better code, less copy-paste, all cases, etc.
        // Difficult, because at condition time, we don't know the operands.
        if (memop_start >= 0
            && mi.getOperand(memop_start + 0).getReg().isValid()
            && mi.getOperand(memop_start + 2).getReg().isValid()
            && mi.getOperand(memop_start + 1).getImm() == 1
            && mi.getOperand(memop_start + 3).isImm()) {
            auto off  = mi.getOperand(memop_start + 3).getImm();
            auto cond = std::format(
                "encodeable_with(FE_MEM(FE_AX, 0, FE_NOREG, {}))", off);

            candidates.emplace_back(
                memop_start + 2,
                cond,
                [mnem, memop_start, extra_ops](
                    std::string                        &buf,
                    const llvm::MachineInstr           &mi,
                    llvm::SmallVectorImpl<std::string> &ops) {
                    std::format_to(
                        std::back_inserter(buf), "        ASMD({}", mnem);
                    unsigned reg_idx = 0;
                    for (unsigned i = 0, n = mi.getNumExplicitOperands();
                         i != n;
                         i++) {
                        const auto &op = mi.getOperand(i);
                        if (i == (unsigned)memop_start) {
                            std::format_to(
                                std::back_inserter(buf),
                                ", FE_MEM({}, {}.scale, {}.idx, {}.off)",
                                ops[reg_idx],
                                ops[reg_idx + 1],
                                ops[reg_idx + 1],
                                ops[reg_idx + 1]);
                            reg_idx += 3;
                            i       += 4;
                        } else if (op.isReg()) {
                            if (op.isTied() && op.isUse()) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {}",
                                           ops[reg_idx++]);
                        } else if (op.isImm()) {
                            if (mnem.starts_with("CMOV")
                                || mnem.starts_with("SET")) {
                                continue;
                            }
                            std::format_to(std::back_inserter(buf),
                                           ", {:#x}",
                                           op.getImm());
                        }
                    }
                    std::format_to(
                        std::back_inserter(buf), "{});\n", extra_ops);
                });
        }
        candidates.emplace_back([mnem, memop_start, extra_ops](
                                    std::string                        &buf,
                                    const llvm::MachineInstr           &mi,
                                    llvm::SmallVectorImpl<std::string> &ops) {
            std::format_to(std::back_inserter(buf), "        ASMD({}", mnem);
            unsigned         reg_idx = 0;
            std::string_view cp_sym  = "";
            bool             has_imm = false;
            for (unsigned i = 0, n = mi.getNumExplicitOperands(); i != n; i++) {
                const auto &op = mi.getOperand(i);
                if (i == (unsigned)memop_start
                    && mi.getOperand(i + 3).isCPI()) {
                    // idx as noreg; rip is ignored
                    cp_sym   = ops[reg_idx + 1];
                    reg_idx += 2;
                    i       += 4;
                    std::format_to(std::back_inserter(buf),
                                   ", FE_MEM(FE_IP, 0, FE_NOREG, 0)");
                } else if (i == (unsigned)memop_start) {
                    std::string_view base =
                        op.getReg().isValid() ? ops[reg_idx] : "FE_NOREG"sv;
                    unsigned sc = mi.getOperand(i + 2).getReg().isValid()
                                      ? mi.getOperand(i + 1).getImm()
                                      : 0;
                    std::string_view idx =
                        mi.getOperand(i + 2).getReg().isValid()
                            ? ops[reg_idx + 1]
                            : "FE_NOREG"sv;
                    auto off = mi.getOperand(i + 3).getImm();
                    assert(!mi.getOperand(i + 4).getReg().isValid());
                    reg_idx += 3;
                    i       += 4;
                    std::format_to(std::back_inserter(buf),
                                   ", FE_MEM({}, {}, {}, {})",
                                   base,
                                   sc,
                                   idx,
                                   off);
                } else if (op.isReg()) {
                    if (op.isTied() && op.isUse()) {
                        continue;
                    }
                    std::format_to(
                        std::back_inserter(buf), ", {}", ops[reg_idx++]);
                } else if (op.isImm()) {
                    if (mnem.starts_with("CMOV") || mnem.starts_with("SET")) {
                        continue;
                    }
                    has_imm = true;
                    std::format_to(
                        std::back_inserter(buf), ", {:#x}", op.getImm());
                }
            }
            std::format_to(std::back_inserter(buf), "{});\n", extra_ops);
            if (!cp_sym.empty()) {
                assert(!has_imm && "CPI with imm unsupported!");
                std::format_to(
                    std::back_inserter(buf),
                    "        "
                    "derived()->assembler.reloc_text_pc32({}, "
                    "derived()->assembler.text_cur_off() - 4, -4);\n",
                    cp_sym);
            }
        });
    };
    // xchg and xadd have their operands swapped in LLVM.
    const auto handle_xchg_mem = [&](std::string_view mnem, int memop_start) {
        // TODO: address candidate
        candidates.emplace_back([mnem, memop_start](
                                    std::string                        &buf,
                                    const llvm::MachineInstr           &mi,
                                    llvm::SmallVectorImpl<std::string> &ops) {
            std::string_view base =
                mi.getOperand(memop_start).getReg().isValid() ? ops[1]
                                                              : "FE_NOREG"sv;
            unsigned sc = mi.getOperand(memop_start + 2).getReg().isValid()
                              ? mi.getOperand(memop_start + 1).getImm()
                              : 0;
            std::string_view idx =
                mi.getOperand(memop_start + 2).getReg().isValid()
                    ? ops[2]
                    : "FE_NOREG"sv;
            auto off = mi.getOperand(memop_start + 3).getImm();
            assert(!mi.getOperand(memop_start + 4).getReg().isValid());

            std::format_to(std::back_inserter(buf),
                           "        ASMD({}, FE_MEM({}, {}, {}, {}), {});",
                           mnem,
                           base,
                           sc,
                           idx,
                           off,
                           ops[0]);
        });
    };

    if (Name == "MOVZX32rm8") {
        handle_default("MOVZXr32m8", 1);
    } else if (Name == "MOVZX32rm16") {
        handle_default("MOVZXr32m16", 1);
    } else if (Name == "MOVZX32rr8") {
        handle_default("MOVZXr32r8");
    } else if (Name == "MOVZX32rr16") {
        handle_default("MOVZXr32r16");
    } else if (Name == "MOVSX32rr8") {
        handle_default("MOVSXr32r8");
    } else if (Name == "MOVSX32rr16") {
        handle_default("MOVSXr32r16");
    } else if (Name == "MOVSX64rr8") {
        handle_default("MOVSXr64r8");
    } else if (Name == "MOVSX64rr16") {
        handle_default("MOVSXr64r16");
    } else if (Name == "MOVSX64rr32") {
        handle_default("MOVSXr64r32");
    } else if (Name == "MOV8mr") {
        handle_immrepl("MOV8mi", 5, 0);
        handle_default("MOV8mr", 0);
    } else if (Name == "MOV16mr") {
        handle_immrepl("MOV16mi", 5, 0);
        handle_default("MOV16mr", 0);
    } else if (Name == "MOV32mr") {
        handle_immrepl("MOV32mi", 5, 0);
        handle_default("MOV32mr", 0);
    } else if (Name == "MOV64mr") {
        handle_immrepl("MOV64mi", 5, 0);
        handle_default("MOV64mr", 0);
    } else if (Name == "MOV8mi") {
        handle_default("MOV8mi", 0);
    } else if (Name == "MOV16mi") {
        handle_default("MOV16mi", 0);
    } else if (Name == "MOV32mi") {
        handle_default("MOV32mi", 0);
    } else if (Name == "MOV64mi") {
        handle_default("MOV64mi", 0);
    } else if (Name == "MOV8ri") {
        handle_default("MOV8ri");
    } else if (Name == "MOV16ri") {
        handle_default("MOV16ri");
    } else if (Name == "MOV32ri") {
        handle_default("MOV32ri");
    } else if (Name == "MOV64ri") {
        handle_default("MOV64ri");
    } else if (Name == "MOV8rr") {
        handle_immrepl("MOV8ri", 1);
        handle_memrepl("MOV8rm", 1);
        handle_default("MOV8rr");
    } else if (Name == "MOV16rr") {
        handle_immrepl("MOV16ri", 1);
        handle_memrepl("MOV16rm", 1);
        handle_default("MOV16rr");
    } else if (Name == "MOV32rr") {
        handle_immrepl("MOV32ri", 1);
        handle_memrepl("MOV32rm", 1);
        handle_default("MOV32rr");
    } else if (Name == "MOV64rr") {
        handle_immrepl("MOV64ri", 1);
        handle_memrepl("MOV64rm", 1);
        handle_default("MOV64rr");
    } else if (Name == "MOV32rm") {
        handle_default("MOV32rm", 1);
    } else if (Name == "MOV64rm") {
        handle_default("MOV64rm", 1);

    } else if (Name == "LCMPXCHG64") {
        handle_default("LOCK_CMPXCHG64mr", 0);

    } else if (Name == "MOVSSrm_alt") {
        handle_default("SSE_MOVSSrm", 1);
    } else if (Name == "MOVSDrm_alt") {
        handle_default("SSE_MOVSDrm", 1);
    } else if (Name == "MOVAPSrm") {
        handle_default("SSE_MOVAPSrm", 1);
    } else if (Name == "VMOVAPSYrm") {
        handle_default("VMOVAPS256rm", 1);
    } else if (Name == "VMOVAPSZrm") {
        handle_default("VMOVAPS512rm", 1);
    } else if (Name == "MOVSSmr") {
        handle_default("SSE_MOVSSmr", 0);
    } else if (Name == "MOVSDmr") {
        handle_default("SSE_MOVSDmr", 0);
    } else if (Name == "MOVAPSmr") {
        handle_default("SSE_MOVAPSmr", 0);
    } else if (Name == "VMOVAPSYmr") {
        handle_default("VMOVAPS256mr", 0);
    } else if (Name == "VMOVAPSZmr") {
        handle_default("VMOVAPS512mr", 0);

    } else if (Name == "CMPSSrri") {
        handle_memrepl("SSE_CMPSSrmi", 2);
        handle_default("SSE_CMPSSrri");
    } else if (Name == "CMPSDrri") {
        handle_memrepl("SSE_CMPSDrmi", 2);
        handle_default("SSE_CMPSDrri");
    } else if (Name == "CMPPSrri") {
        handle_memrepl("SSE_CMPPSrmi", 2);
        handle_default("SSE_CMPPSrri");
    } else if (Name == "CMPPDrri") {
        handle_memrepl("SSE_CMPPDrmi", 2);
        handle_default("SSE_CMPPDrri");
    } else if (Name == "ADDSSrr") {
        handle_memrepl("SSE_ADDSSrm", 2);
        handle_default("SSE_ADDSSrr");
    } else if (Name == "SUBSSrr") {
        handle_memrepl("SSE_SUBSSrm", 2);
        handle_default("SSE_SUBSSrr");
    } else if (Name == "MULSSrr") {
        handle_memrepl("SSE_MULSSrm", 2);
        handle_default("SSE_MULSSrr");
    } else if (Name == "DIVSSrr") {
        handle_memrepl("SSE_DIVSSrm", 2);
        handle_default("SSE_DIVSSrr");
    } else if (Name == "ADDSDrr") {
        handle_memrepl("SSE_ADDSDrm", 2);
        handle_default("SSE_ADDSDrr");
    } else if (Name == "SUBSDrr") {
        handle_memrepl("SSE_SUBSDrm", 2);
        handle_default("SSE_SUBSDrr");
    } else if (Name == "MULSDrr") {
        handle_memrepl("SSE_MULSDrm", 2);
        handle_default("SSE_MULSDrr");
    } else if (Name == "DIVSDrr") {
        handle_memrepl("SSE_DIVSDrm", 2);
        handle_default("SSE_DIVSDrr");
    } else if (Name == "ADDPSrr") {
        handle_memrepl("SSE_ADDPSrm", 2);
        handle_default("SSE_ADDPSrr");
    } else if (Name == "SUBPSrr") {
        handle_memrepl("SSE_SUBPSrm", 2);
        handle_default("SSE_SUBPSrr");
    } else if (Name == "MULPSrr") {
        handle_memrepl("SSE_MULPSrm", 2);
        handle_default("SSE_MULPSrr");
    } else if (Name == "DIVPSrr") {
        handle_memrepl("SSE_DIVPSrm", 2);
        handle_default("SSE_DIVPSrr");
    } else if (Name == "ADDPDrr") {
        handle_memrepl("SSE_ADDPDrm", 2);
        handle_default("SSE_ADDPDrr");
    } else if (Name == "SUBPDrr") {
        handle_memrepl("SSE_SUBPDrm", 2);
        handle_default("SSE_SUBPDrr");
    } else if (Name == "MULPDrr") {
        handle_memrepl("SSE_MULPDrm", 2);
        handle_default("SSE_MULPDrr");
    } else if (Name == "DIVPDrr") {
        handle_memrepl("SSE_DIVPDrm", 2);
        handle_default("SSE_DIVPDrr");
    } else if (Name == "XORPSrr") {
        handle_memrepl("SSE_XORPSrm", 2);
        handle_default("SSE_XORPSrr");
    } else if (Name == "XORPDrr") {
        handle_memrepl("SSE_XORPDrm", 2);
        handle_default("SSE_XORPDrr");
    } else if (Name == "ANDPSrr") {
        handle_memrepl("SSE_ANDPSrm", 2);
        handle_default("SSE_ANDPSrr");
    } else if (Name == "ANDPDrr") {
        handle_memrepl("SSE_ANDPDrm", 2);
        handle_default("SSE_ANDPDrr");
    } else if (Name == "ANDNPSrr") {
        handle_memrepl("SSE_ANDNPSrm", 2);
        handle_default("SSE_ANDNPSrr");
    } else if (Name == "ANDNPDrr") {
        handle_memrepl("SSE_ANDNPDrm", 2);
        handle_default("SSE_ANDNPDrr");
    } else if (Name == "ORPSrr") {
        handle_memrepl("SSE_ORPSrm", 2);
        handle_default("SSE_ORPSrr");
    } else if (Name == "ORPDrr") {
        handle_memrepl("SSE_ORPDrm", 2);
        handle_default("SSE_ORPDrr");
    } else if (Name == "ADDSSrm") {
        handle_default("SSE_ADDSSrm", 2);
    } else if (Name == "SUBSSrm") {
        handle_default("SSE_SUBSSrm", 2);
    } else if (Name == "MULSSrm") {
        handle_default("SSE_MULSSrm", 2);
    } else if (Name == "DIVSSrm") {
        handle_default("SSE_DIVSSrm", 2);
    } else if (Name == "ADDSDrm") {
        handle_default("SSE_ADDSDrm", 2);
    } else if (Name == "SUBSDrm") {
        handle_default("SSE_SUBSDrm", 2);
    } else if (Name == "MULSDrm") {
        handle_default("SSE_MULSDrm", 2);
    } else if (Name == "DIVSDrm") {
        handle_default("SSE_DIVSDrm", 2);
    } else if (Name == "ADDPSrm") {
        handle_default("SSE_ADDPSrm", 2);
    } else if (Name == "SUBPSrm") {
        handle_default("SSE_SUBPSrm", 2);
    } else if (Name == "MULPSrm") {
        handle_default("SSE_MULPSrm", 2);
    } else if (Name == "DIVPSrm") {
        handle_default("SSE_DIVPSrm", 2);
    } else if (Name == "ADDSDrm") {
        handle_default("SSE_ADDSDrm", 2);
    } else if (Name == "SUBPDrm") {
        handle_default("SSE_SUBPDrm", 2);
    } else if (Name == "MULPDrm") {
        handle_default("SSE_MULPDrm", 2);
    } else if (Name == "DIVPDrm") {
        handle_default("SSE_DIVPDrm", 2);
    } else if (Name == "XORPSrm") {
        handle_default("SSE_XORPSrm", 2);
    } else if (Name == "XORPDrm") {
        handle_default("SSE_XORPDrm", 2);
    } else if (Name == "ANDPSrm") {
        handle_default("SSE_ANDPSrm", 2);
    } else if (Name == "ANDPDrm") {
        handle_default("SSE_ANDPDrm", 2);
    } else if (Name == "ANDNPSrm") {
        handle_default("SSE_ANDNPSrm", 2);
    } else if (Name == "ANDNPDrm") {
        handle_default("SSE_ANDNPDrm", 2);
    } else if (Name == "ORPSrm") {
        handle_default("SSE_ORPSrm", 2);
    } else if (Name == "ORPDrm") {
        handle_default("SSE_ORPDrm", 2);
    } else if (Name == "COMISSrr") {
        handle_memrepl("SSE_COMISSrm", 1);
        handle_default("SSE_COMISSrr");
    } else if (Name == "COMISDrr") {
        handle_memrepl("SSE_COMISDrm", 1);
        handle_default("SSE_COMISDrr");
    } else if (Name == "UCOMISSrr") {
        handle_memrepl("SSE_UCOMISSrm", 1);
        handle_default("SSE_UCOMISSrr");
    } else if (Name == "UCOMISDrr") {
        handle_memrepl("SSE_UCOMISDrm", 1);
        handle_default("SSE_UCOMISDrr");
    } else if (Name == "COMISSrm") {
        handle_default("SSE_COMISSrm", 1);
    } else if (Name == "COMISDrm") {
        handle_default("SSE_COMISDrm", 1);
    } else if (Name == "UCOMISSrm") {
        handle_default("SSE_UCOMISSrm", 1);
    } else if (Name == "UCOMISDrm") {
        handle_default("SSE_UCOMISDrm", 1);
    } else if (Name == "CVTSD2SSrr") {
        handle_memrepl("SSE_CVTSD2SSrm", 1);
        handle_default("SSE_CVTSD2SSrr");
    } else if (Name == "CVTSS2SDrr") {
        handle_memrepl("SSE_CVTSS2SDrm", 1);
        handle_default("SSE_CVTSS2SDrr");
    } else if (Name == "CVTSI2SSrr") {
        handle_memrepl("SSE_CVTSI2SS32rm", 1);
        handle_default("SSE_CVTSI2SS32rr");
    } else if (Name == "CVTSI2SDrr") {
        handle_memrepl("SSE_CVTSI2SD32rm", 1);
        handle_default("SSE_CVTSI2SD32rr");
    } else if (Name == "CVTSI642SSrr") {
        handle_memrepl("SSE_CVTSI2SS64rm", 1);
        handle_default("SSE_CVTSI2SS64rr");
    } else if (Name == "CVTSI642SDrr") {
        handle_memrepl("SSE_CVTSI2SD64rm", 1);
        handle_default("SSE_CVTSI2SD64rr");
    } else if (Name == "CVTTSD2SIrr") {
        handle_memrepl("SSE_CVTTSD2SI32rm", 1);
        handle_default("SSE_CVTTSD2SI32rr");
    } else if (Name == "CVTTSS2SIrr") {
        handle_memrepl("SSE_CVTTSS2SI32rm", 1);
        handle_default("SSE_CVTTSS2SI32rr");
    } else if (Name == "CVTTSD2SI64rr") {
        handle_memrepl("SSE_CVTTSD2SI64rm", 1);
        handle_default("SSE_CVTTSD2SI64rr");
    } else if (Name == "CVTTSS2SI64rr") {
        handle_memrepl("SSE_CVTTSS2SI64rm", 1);
        handle_default("SSE_CVTTSS2SI64rr");
    } else if (Name == "CVTTSD2SI64rr_Int") {
        handle_memrepl("SSE_CVTTSD2SI64rm", 1);
        handle_default("SSE_CVTTSD2SI64rr");
    } else if (Name == "CVTTSS2SI64rr_Int") {
        handle_memrepl("SSE_CVTTSS2SI64rm", 1);
        handle_default("SSE_CVTTSS2SI64rr");
    } else if (Name == "MOV64toPQIrr") {
        handle_default("SSE_MOVQ_G2Xrr");
    } else if (Name == "MOVSS2DIrr") {
        handle_default("SSE_MOVD_X2Grr");
    } else if (Name == "MOVSDto64rr") {
        handle_default("SSE_MOVQ_X2Grr");
    } else if (Name == "PUNPCKLDQrm") {
        handle_default("SSE_PUNPCKLDQrm", 2);
    } else if (Name == "UNPCKHPDrr") {
        handle_default("SSE_UNPCKHPDrr");

    } else if (Name == "SHR32ri") {
        handle_default("SHR32ri");
    } else if (Name == "SHR64ri") {
        handle_default("SHR64ri");
    } else if (Name == "SAR32ri") {
        handle_default("SAR32ri");
    } else if (Name == "SAR64ri") {
        handle_default("SAR64ri");
    } else if (Name == "SHL32ri") {
        handle_default("SHL32ri");
    } else if (Name == "SHL64ri") {
        handle_default("SHL64ri");
    } else if (Name == "SHR32rCL") {
        handle_immrepl("SHR32ri", 3);
        handle_default("SHR32rr", -1, ", FE_CX");
    } else if (Name == "SHR64rCL") {
        handle_immrepl("SHR64ri", 3);
        handle_default("SHR64rr", -1, ", FE_CX");
    } else if (Name == "SAR32rCL") {
        handle_immrepl("SAR32ri", 3);
        handle_default("SAR32rr", -1, ", FE_CX");
    } else if (Name == "SAR64rCL") {
        handle_immrepl("SAR64ri", 3);
        handle_default("SAR64rr", -1, ", FE_CX");
    } else if (Name == "SHL32rCL") {
        handle_immrepl("SHL32ri", 3);
        handle_default("SHL32rr", -1, ", FE_CX");
    } else if (Name == "SHL64rCL") {
        handle_immrepl("SHL64ri", 3);
        handle_default("SHL64rr", -1, ", FE_CX");
    } else if (Name == "ROR8ri") {
        handle_default("ROR8ri");
    } else if (Name == "ROR16ri") {
        handle_default("ROR16ri");
    } else if (Name == "ROR32ri") {
        handle_default("ROR32ri");
    } else if (Name == "ROR64ri") {
        handle_default("ROR64ri");
    } else if (Name == "ROL8ri") {
        handle_default("ROL8ri");
    } else if (Name == "ROL16ri") {
        handle_default("ROL16ri");
    } else if (Name == "ROL32ri") {
        handle_default("ROL32ri");
    } else if (Name == "ROL64ri") {
        handle_default("ROL64ri");
    } else if (Name == "ROR8rCL") {
        handle_immrepl("ROR8ri", 3);
        handle_default("ROR8rr", -1, ", FE_CX");
    } else if (Name == "ROR16rCL") {
        handle_immrepl("ROR16ri", 3);
        handle_default("ROR16rr", -1, ", FE_CX");
    } else if (Name == "ROR32rCL") {
        handle_immrepl("ROR32ri", 3);
        handle_default("ROR32rr", -1, ", FE_CX");
    } else if (Name == "ROR64rCL") {
        handle_immrepl("ROR64ri", 3);
        handle_default("ROR64rr", -1, ", FE_CX");
    } else if (Name == "ROL8rCL") {
        handle_immrepl("ROL8ri", 3);
        handle_default("ROL8rr", -1, ", FE_CX");
    } else if (Name == "ROL16rCL") {
        handle_immrepl("ROL16ri", 3);
        handle_default("ROL16rr", -1, ", FE_CX");
    } else if (Name == "ROL32rCL") {
        handle_immrepl("ROL32ri", 3);
        handle_default("ROL32rr", -1, ", FE_CX");
    } else if (Name == "ROL64rCL") {
        handle_immrepl("ROL64ri", 3);
        handle_default("ROL64rr", -1, ", FE_CX");

    } else if (Name == "INC8r") {
        handle_default("INC8r");
    } else if (Name == "INC16r") {
        handle_default("INC16r");
    } else if (Name == "INC32r") {
        handle_default("INC32r");
    } else if (Name == "INC64r") {
        handle_default("INC64r");
    } else if (Name == "DEC8r") {
        handle_default("DEC8r");
    } else if (Name == "DEC16r") {
        handle_default("DEC16r");
    } else if (Name == "DEC32r") {
        handle_default("DEC32r");
    } else if (Name == "DEC64r") {
        handle_default("DEC64r");
    } else if (Name == "ADD8rr") {
        handle_immrepl("ADD8ri", 2);
        handle_memrepl("ADD8rm", 2);
        handle_default("ADD8rr");
    } else if (Name == "ADD16rr") {
        handle_immrepl("ADD16ri", 2);
        handle_memrepl("ADD16rm", 2);
        handle_default("ADD16rr");
    } else if (Name == "ADD32rr") {
        handle_immrepl("ADD32ri", 2);
        handle_memrepl("ADD32rm", 2);
        handle_default("ADD32rr");
    } else if (Name == "ADD64rr") {
        handle_immrepl("ADD64ri", 2);
        handle_memrepl("ADD64rm", 2);
        handle_default("ADD64rr");
    } else if (Name == "ADD32ri") {
        handle_default("ADD32ri");
    } else if (Name == "ADD64ri32") {
        handle_default("ADD64ri");
    } else if (Name == "ADC8rr") {
        handle_immrepl("ADC8ri", 2);
        handle_memrepl("ADC8rm", 2);
        handle_default("ADC8rr");
    } else if (Name == "ADC16rr") {
        handle_immrepl("ADC16ri", 2);
        handle_memrepl("ADC16rm", 2);
        handle_default("ADC16rr");
    } else if (Name == "ADC32rr") {
        handle_immrepl("ADC32ri", 2);
        handle_memrepl("ADC32rm", 2);
        handle_default("ADC32rr");
    } else if (Name == "ADC64rr") {
        handle_immrepl("ADC64ri", 2);
        handle_memrepl("ADC64rm", 2);
        handle_default("ADC64rr");
    } else if (Name == "ADC32ri") {
        handle_default("ADC32ri");
    } else if (Name == "ADC64ri32") {
        handle_default("ADC64ri");
    } else if (Name == "SUB8rr") {
        handle_immrepl("SUB8ri", 2);
        handle_memrepl("SUB8rm", 2);
        handle_default("SUB8rr");
    } else if (Name == "SUB16rr") {
        handle_immrepl("SUB16ri", 2);
        handle_memrepl("SUB16rm", 2);
        handle_default("SUB16rr");
    } else if (Name == "SUB32rr") {
        handle_immrepl("SUB32ri", 2);
        handle_memrepl("SUB32rm", 2);
        handle_default("SUB32rr");
    } else if (Name == "SUB64rr") {
        handle_immrepl("SUB64ri", 2);
        handle_memrepl("SUB64rm", 2);
        handle_default("SUB64rr");
    } else if (Name == "SBB32rr") {
        handle_immrepl("SBB32ri", 2);
        handle_memrepl("SBB32rm", 2);
        handle_default("SBB32rr");
    } else if (Name == "SBB64rr") {
        handle_immrepl("SBB64ri", 2);
        handle_memrepl("SBB64rm", 2);
        handle_default("SBB64rr");
    } else if (Name == "CMP8rr") {
        handle_immrepl("CMP8ri", 1);
        handle_memrepl("CMP8rm", 1);
        handle_default("CMP8rr");
    } else if (Name == "CMP16rr") {
        handle_immrepl("CMP16ri", 1);
        handle_memrepl("CMP16rm", 1);
        handle_default("CMP16rr");
    } else if (Name == "CMP32rr") {
        handle_immrepl("CMP32ri", 1);
        handle_memrepl("CMP32rm", 1);
        handle_default("CMP32rr");
    } else if (Name == "CMP64rr") {
        handle_immrepl("CMP64ri", 1);
        handle_memrepl("CMP64rm", 1);
        handle_default("CMP64rr");
    } else if (Name == "CMP32ri") {
        handle_memrepl("CMP32mi", 0);
        handle_default("CMP32ri");
    } else if (Name == "CMP64ri") {
        handle_memrepl("CMP64mi", 0);
        handle_default("CMP64ri");
    } else if (Name == "OR8rr") {
        handle_immrepl("OR8ri", 2);
        handle_memrepl("OR8rm", 2);
        handle_default("OR8rr");
    } else if (Name == "OR16rr") {
        handle_immrepl("OR16ri", 2);
        handle_memrepl("OR16rm", 2);
        handle_default("OR16rr");
    } else if (Name == "OR32rr") {
        handle_immrepl("OR32ri", 2);
        handle_memrepl("OR32rm", 2);
        handle_default("OR32rr");
    } else if (Name == "OR64rr") {
        handle_immrepl("OR64ri", 2);
        handle_memrepl("OR64rm", 2);
        handle_default("OR64rr");
    } else if (Name == "OR8ri") {
        handle_default("OR8ri");
    } else if (Name == "OR16ri") {
        handle_default("OR16ri");
    } else if (Name == "OR32ri") {
        handle_default("OR32ri");
    } else if (Name == "OR64ri32") {
        handle_default("OR64ri");
    } else if (Name == "XOR32rr") {
        handle_immrepl("XOR32ri", 2);
        handle_memrepl("XOR32rm", 2);
        handle_default("XOR32rr");
    } else if (Name == "XOR64rr") {
        handle_immrepl("XOR64ri", 2);
        handle_memrepl("XOR64rm", 2);
        handle_default("XOR64rr");
    } else if (Name == "XOR32ri") {
        handle_default("XOR32ri");
    } else if (Name == "XOR64ri32") {
        handle_default("XOR64ri");
    } else if (Name == "AND8rr") {
        handle_immrepl("AND8ri", 2);
        handle_memrepl("AND8rm", 2);
        handle_default("AND8rr");
    } else if (Name == "AND16rr") {
        handle_immrepl("AND16ri", 2);
        handle_memrepl("AND16rm", 2);
        handle_default("AND16rr");
    } else if (Name == "AND32rr") {
        handle_immrepl("AND32ri", 2);
        handle_memrepl("AND32rm", 2);
        handle_default("AND32rr");
    } else if (Name == "AND64rr") {
        handle_immrepl("AND64ri", 2);
        handle_memrepl("AND64rm", 2);
        handle_default("AND64rr");
    } else if (Name == "AND32ri") {
        handle_default("AND32ri");
    } else if (Name == "AND64ri32") {
        handle_default("AND64ri");
    } else if (Name == "TEST8ri") {
        handle_memrepl("TEST8mi", 0);
        handle_default("TEST8ri");
    } else if (Name == "TEST16ri") {
        handle_memrepl("TEST16mi", 0);
        handle_default("TEST16ri");
    } else if (Name == "TEST32ri") {
        handle_memrepl("TEST32mi", 0);
        handle_default("TEST32ri");
    } else if (Name == "TEST64ri") {
        handle_memrepl("TEST64mi", 0);
        handle_default("TEST64ri");
    } else if (Name == "TEST8rr") {
        handle_immrepl("TEST8ri", 1);
        handle_memrepl("TEST8mr", 0);
        handle_default("TEST8rr");
    } else if (Name == "TEST16rr") {
        handle_immrepl("TEST16ri", 1);
        handle_memrepl("TEST16mr", 0);
        handle_default("TEST16rr");
    } else if (Name == "TEST32rr") {
        handle_immrepl("TEST32ri", 1);
        handle_memrepl("TEST32mr", 0);
        handle_default("TEST32rr");
    } else if (Name == "TEST64rr") {
        handle_immrepl("TEST64ri", 1);
        handle_memrepl("TEST64mr", 0);
        handle_default("TEST64rr");
    } else if (Name == "BSF16rr") {
        handle_default("BSF16rr");
    } else if (Name == "BSF32rr") {
        handle_default("BSF32rr");
    } else if (Name == "BSF64rr") {
        handle_default("BSF64rr");
    } else if (Name == "BSR16rr") {
        handle_default("BSR16rr");
    } else if (Name == "BSR32rr") {
        handle_default("BSR32rr");
    } else if (Name == "BSR64rr") {
        handle_default("BSR64rr");
    } else if (Name == "MUL8r") {
        handle_memrepl("MUL8m", 0);
        handle_default("MUL8r");
    } else if (Name == "MUL16r") {
        handle_memrepl("MUL16m", 0);
        handle_default("MUL16r");
    } else if (Name == "MUL32r") {
        handle_memrepl("MUL32m", 0);
        handle_default("MUL32r");
    } else if (Name == "MUL64r") {
        handle_memrepl("MUL64m", 0);
        handle_default("MUL64r");
    } else if (Name == "IMUL8r") {
        handle_memrepl("IMUL8m", 0);
        handle_default("IMUL8r");
    } else if (Name == "IMUL16r") {
        handle_memrepl("IMUL16m", 0);
        handle_default("IMUL16r");
    } else if (Name == "IMUL32r") {
        handle_memrepl("IMUL32m", 0);
        handle_default("IMUL32r");
    } else if (Name == "IMUL64r") {
        handle_memrepl("IMUL64m", 0);
        handle_default("IMUL64r");
    } else if (Name == "IMUL16rr") {
        // TODO: for imm replacment, use rri encoding
        handle_memrepl("IMUL16rm", 2);
        handle_default("IMUL16rr");
    } else if (Name == "IMUL32rr") {
        // TODO: for imm replacment, use rri encoding
        handle_memrepl("IMUL32rm", 2);
        handle_default("IMUL32rr");
    } else if (Name == "IMUL64rr") {
        // TODO: for imm replacment, use rri encoding
        handle_memrepl("IMUL64rm", 2);
        handle_default("IMUL64rr");
    } else if (Name == "DIV32r") {
        handle_memrepl("DIV32m", 0);
        handle_default("DIV32r");
    } else if (Name == "DIV64r") {
        handle_memrepl("DIV64m", 0);
        handle_default("DIV64r");
    } else if (Name == "IDIV32r") {
        handle_memrepl("IDIV32m", 0);
        handle_default("IDIV32r");
    } else if (Name == "IDIV64r") {
        handle_memrepl("IDIV64m", 0);
        handle_default("IDIV64r");
    } else if (Name == "NEG32r") {
        handle_default("NEG32r");
    } else if (Name == "NEG64r") {
        handle_default("NEG64r");
    } else if (Name == "NOT8r") {
        handle_default("NOT8r");
    } else if (Name == "NOT16r") {
        handle_default("NOT16r");
    } else if (Name == "NOT32r") {
        handle_default("NOT32r");
    } else if (Name == "NOT64r") {
        handle_default("NOT64r");
    } else if (Name == "CWD") {
        handle_default("CWD");
    } else if (Name == "CDQ") {
        handle_default("CDQ");
    } else if (Name == "CQO") {
        handle_default("CQO");
    } else if (Name == "LEA64_32r") {
        handle_default("LEA32rm", 1);
    } else if (Name == "LEA64r") {
        handle_default("LEA64rm", 1);

    } else if (Name == "XCHG8rm") {
        handle_xchg_mem("XCHG8mr", 2);
    } else if (Name == "XCHG16rm") {
        handle_xchg_mem("XCHG16mr", 2);
    } else if (Name == "XCHG32rm") {
        handle_xchg_mem("XCHG32mr", 2);
    } else if (Name == "XCHG64rm") {
        handle_xchg_mem("XCHG64mr", 2);
    } else if (Name == "LXADD8") {
        handle_xchg_mem("LOCK_XADD8mr", 2);
    } else if (Name == "LXADD16") {
        handle_xchg_mem("LOCK_XADD16mr", 2);
    } else if (Name == "LXADD32") {
        handle_xchg_mem("LOCK_XADD32mr", 2);
    } else if (Name == "LXADD64") {
        handle_xchg_mem("LOCK_XADD64mr", 2);

    } else if (Name == "SETCCr") {
        std::array<std::string_view, 16> cond_codes = {"SETO8r",
                                                       "SETNO8r",
                                                       "SETC8r",
                                                       "SETNC8r",
                                                       "SETZ8r",
                                                       "SETNZ8r",
                                                       "SETBE8r",
                                                       "SETA8r",
                                                       "SETS8r",
                                                       "SETNS8r",
                                                       "SETP8r",
                                                       "SETNP8r",
                                                       "SETL8r",
                                                       "SETGE8r",
                                                       "SETLE8r",
                                                       "SETG8r"};
        handle_default(cond_codes[mi.getOperand(1).getImm()]);
    } else if (Name == "CMOV32rr") {
        std::array<std::string_view, 16> cond_codes = {"CMOVO32rr",
                                                       "CMOVNO32rr",
                                                       "CMOVC32rr",
                                                       "CMOVNC32rr",
                                                       "CMOVZ32rr",
                                                       "CMOVNZ32rr",
                                                       "CMOVBE32rr",
                                                       "CMOVA32rr",
                                                       "CMOVS32rr",
                                                       "CMOVNS32rr",
                                                       "CMOVP32rr",
                                                       "CMOVNP32rr",
                                                       "CMOVL32rr",
                                                       "CMOVGE32rr",
                                                       "CMOVLE32rr",
                                                       "CMOVG32rr"};
        handle_default(cond_codes[mi.getOperand(3).getImm()]);
    } else if (Name == "CMOV64rr") {
        std::array<std::string_view, 16> cond_codes = {"CMOVO64rr",
                                                       "CMOVNO64rr",
                                                       "CMOVC64rr",
                                                       "CMOVNC64rr",
                                                       "CMOVZ64rr",
                                                       "CMOVNZ64rr",
                                                       "CMOVBE64rr",
                                                       "CMOVA64rr",
                                                       "CMOVS64rr",
                                                       "CMOVNS64rr",
                                                       "CMOVP64rr",
                                                       "CMOVNP64rr",
                                                       "CMOVL64rr",
                                                       "CMOVGE64rr",
                                                       "CMOVLE64rr",
                                                       "CMOVG64rr"};
        handle_default(cond_codes[mi.getOperand(3).getImm()]);

    } else if (Name == "PREFETCHNTA") {
        handle_default("PREFETCHNTAm", 0);
    } else if (Name == "PREFETCHT2") {
        handle_default("PREFETCHT2m", 0);
    } else if (Name == "PREFETCHT1") {
        handle_default("PREFETCHT1m", 0);
    } else if (Name == "PREFETCHT0") {
        handle_default("PREFETCHT0m", 0);
    } else {
        assert(false);
    }
}

} // namespace tpde_encgen::x64
