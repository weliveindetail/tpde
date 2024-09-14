// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <string>

#include <llvm/CodeGen/MachineInstr.h>

namespace tpde_encgen {

// TODO(ts): I'm really unsure how to properly implement the replacement
// strategy so for now we wrap the InstInfo from tpde-asmgen
// in a bit different to support a more condition-driven replacement style
// compared to the operand-based one in tpde-asmgen
struct InstDesc {
    struct PreferredEncoding {
        enum TARGET {
            TARGET_NONE,
            // TARGET_DEF,
            TARGET_USE,
        };

        enum COND {
            COND_IMM64,
            COND_IMM32,
            COND_IMM16,
            COND_IMM8,
            COND_MEM,
        };

        TARGET                    target;
        unsigned                  target_def_use_idx;
        COND                      cond;
        std::shared_ptr<InstDesc> replacement;
    };

    // std::string name_llvm;
    std::string name_fadec;

    // order of operands does not matter
    bool commutable;

    enum OP_TYPE {
        OP_NONE,
        OP_IMM,
        OP_MEM,
        OP_REG,
    };

    struct Operand {
        OP_TYPE  type;
        bool     op_type_manual = false;
        unsigned llvm_idx;
    };

    std::array<Operand, 4> operands;

    std::vector<PreferredEncoding> preferred_encodings;
    // TODO(ts): need to think about operand encoding
    // we would for example like to support
    // SHL32rCL -> SHL32ri
    // or SHL32rCL -> SHLXrrr
    // or SHLXrrr -> SHL32ri (is this problematic?)
};

extern bool get_inst_def(llvm::MachineInstr &inst, InstDesc &inst_desc);
} // namespace tpde_encgen
