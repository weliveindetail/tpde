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

// TODO(ts): add manual replacements for SSE_MOVQ_X2Grr -> MOV64rm,
// SSE_MOVQ_G2Xrr -> MOV64rm, etc
// TODO(ts): for TESTrr, we do not generate TESTmi as a replacement
// TODO(ts): we kind of want to support two->three operand replacements
// e.g. IMUL32rr -> IMUL32rri

struct ManualOperand {
    std::string       inst_name;
    unsigned          op_idx;
    InstDesc::OP_TYPE op_type;
};

std::array<ManualOperand, 2> manual_operands = {
    {
     ManualOperand{
            .inst_name = "LEA64rm", .op_idx = 1, .op_type = InstDesc::OP_MEM},
     ManualOperand{
            .inst_name = "LEA32rm", .op_idx = 1, .op_type = InstDesc::OP_MEM},
     }
};

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

    if (desc.name_fadec.find("CC") != std::string::npos) {
        // replace condition code for instructions like SETcc/Jcc
        std::array<const char *, 16> cond_codes = {"O",
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
        auto                         use_it     = inst.uses().begin();
        for (auto use_idx = 0; use_idx < llvm_desc.NumOperands;
             ++use_idx, ++use_it) {
            assert(use_it != inst.uses().end());
            if (!use_it->isImm()) {
                // condition codes are immediates
                continue;
            }
            if (llvm_desc.operands()[use_idx].OperandType
                == llvm::MCOI::OPERAND_IMMEDIATE) {
                // condition codes have their own operand type
                continue;
            }
            // should be it
            const auto code_idx = use_it->getImm();
            assert(code_idx >= 0
                   && code_idx < static_cast<int64_t>(cond_codes.size()));
            desc.name_fadec.replace(
                desc.name_fadec.find("CC"), 2, cond_codes[code_idx]);
            break;
        }
    }

    const auto add_op_ty_name = [&desc](const std::string_view str) {
        desc.name_fadec += str;
        for (auto &replacement : desc.preferred_encodings) {
            replacement.replacement->name_fadec += str;
        }
    };

    const auto set_op_ty_and_llvm_idx = [&desc](const unsigned          op_idx,
                                                const InstDesc::OP_TYPE ty,
                                                const unsigned llvm_idx) {
        desc.operands[op_idx].type     = ty;
        desc.operands[op_idx].llvm_idx = llvm_idx;

        for (auto &replacement : desc.preferred_encodings) {
            assert(replacement.replacement->operands[op_idx].type
                   == InstDesc::OP_NONE);
            replacement.replacement->operands[op_idx].type     = ty;
            replacement.replacement->operands[op_idx].llvm_idx = llvm_idx;
        }
    };

    // MOVSX/MOVZX require special handling, because they also state the
    // parameter sizes
    bool is_mov_with_extension = info.fadecName.starts_with("MOVSX")
                                 || info.fadecName.starts_with("MOVZX");

    const auto imm_cond = [](const OpType ty) {
        // TODO(ts): for immediates we should try to know whether they are
        // actually sign-extended and what the operation size is, e.g. for
        // ADD64ri we actually care whether the 64 bit immediate fits into
        // 32bits as sign-extended but for MOV16mi, we dont actually care at all
        // and can take any immediate and we can just truncate
        switch (ty) {
        case IMM64: return InstDesc::PreferredEncoding::COND_IMM64;
        case IMM32: return InstDesc::PreferredEncoding::COND_IMM32;
        case IMM16: return InstDesc::PreferredEncoding::COND_IMM16;
        case IMM8: return InstDesc::PreferredEncoding::COND_IMM8;
        default: assert(0); exit(1);
        }
    };

    const auto create_imm_replacement = [&desc, &inst, &info, &imm_cond](
                                            const std::string &old_fadec_name,
                                            const unsigned     idx,
                                            OpSupports        &op) {
        if (!inst.getOperand(op.opIndex).isDef()) {
            if (op.supportsImm()) {
                assert(!info.isFullName);
                auto replacement = std::make_shared<InstDesc>(desc);
                replacement->operands[idx].type  = InstDesc::OP_IMM;
                replacement->name_fadec          = old_fadec_name;
                replacement->name_fadec         += 'i';
                desc.preferred_encodings.push_back(InstDesc::PreferredEncoding{
                    .target = InstDesc::PreferredEncoding::TARGET_USE,
                    .target_def_use_idx = op.opIndex,
                    .cond               = imm_cond(op.getLargestImmOpType()),
                    .replacement        = std::move(replacement)});
            }
        }
    };

    const auto create_mem_replacement = [&desc,
                                         &inst,
                                         &info,
                                         is_mov_with_extension](
                                            const std::string &old_fadec_name,
                                            const unsigned     idx,
                                            OpSupports        &op) {
        if (!inst.getOperand(op.opIndex).isDef()) {
            if (op.supportsMem()) {
                assert(!info.isFullName);
                auto replacement = std::make_shared<InstDesc>(desc);
                replacement->operands[idx].type  = InstDesc::OP_MEM;
                replacement->name_fadec          = old_fadec_name;
                replacement->name_fadec         += 'm';
                if (is_mov_with_extension) {
                    replacement->name_fadec += std::format("{}", op.opSize);
                }
                desc.preferred_encodings.push_back(InstDesc::PreferredEncoding{
                    .target = InstDesc::PreferredEncoding::TARGET_USE,
                    .target_def_use_idx = op.opIndex,
                    .cond               = InstDesc::PreferredEncoding::COND_MEM,
                    .replacement        = std::move(replacement)});
            }
        }
    };

    const auto get_op_type = [&llvm_desc,
                              &desc](const unsigned op_idx) -> uint8_t {
        for (const auto &manual_op : manual_operands) {
            if (manual_op.inst_name != desc.name_fadec
                || manual_op.op_idx != op_idx) {
                continue;
            }
            desc.operands[op_idx].op_type_manual = true;
            switch (manual_op.op_type) {
            case InstDesc::OP_IMM: return llvm::MCOI::OPERAND_IMMEDIATE;
            case InstDesc::OP_REG: return llvm::MCOI::OPERAND_REGISTER;
            case InstDesc::OP_MEM: return llvm::MCOI::OPERAND_MEMORY;
            case InstDesc::OP_NONE: return llvm::MCOI::OPERAND_UNKNOWN;
            }
        }

        return llvm_desc.operands()[op_idx].OperandType;
    };

    for (auto &op : desc.operands) {
        op.type = InstDesc::OP_NONE;
    }

    for (unsigned idx = 0; idx < info.numOps; ++idx) {
        auto &op = info.ops[idx];

        desc.operands[idx].llvm_idx = op.opIndex;

        const auto old_fadec_name = desc.name_fadec;

        auto force_is_reg = false;
        if (op.opIndex > inst.getNumExplicitOperands()) {
            // this operand is an implicit operand
            assert(inst.getOperand(op.opIndex).isReg());
            if (!inst.getOperand(op.opIndex).isImplicit()) {
                assert(0);
                return false;
            }
            force_is_reg = true;
        }

        // for now just take what's in the MachineInstr
        if (force_is_reg
            || get_op_type(op.opIndex) == llvm::MCOI::OPERAND_REGISTER) {
            assert(op.supportsReg());

            if (!info.isFullName) {
                add_op_ty_name("r");
                if (is_mov_with_extension) {
                    add_op_ty_name(std::format("{}", op.opSize));
                }
            }
            set_op_ty_and_llvm_idx(idx, InstDesc::OP_REG, op.opIndex);

            create_imm_replacement(old_fadec_name, idx, op);
            create_mem_replacement(old_fadec_name, idx, op);

            continue;
        }

        if (get_op_type(op.opIndex) == llvm::MCOI::OPERAND_MEMORY) {
            assert(op.supportsMem());
            if (!info.isFullName) {
                desc.name_fadec += 'm';
                if (is_mov_with_extension) {
                    desc.name_fadec += std::format("{}", op.opSize);
                }
            }
            set_op_ty_and_llvm_idx(idx, InstDesc::OP_MEM, op.opIndex);

            create_imm_replacement(old_fadec_name, idx, op);
            continue;
        }

        if (get_op_type(op.opIndex) == llvm::MCOI::OPERAND_IMMEDIATE) {
            assert(op.supportsImm());
            if (!info.isFullName) {
                desc.name_fadec += 'i';
                assert(!is_mov_with_extension);
            }

            set_op_ty_and_llvm_idx(idx, InstDesc::OP_IMM, op.opIndex);
            continue;
        }

        assert(0);
        return false;
    }

    return true;
}
