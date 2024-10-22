// SPDX-FileCopyrightText: 2024 Alexis Engelke <engelke@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <string>
#include <string_view>

#include <llvm/ADT/SmallVector.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineInstr.h>

namespace tpde_encgen {

/// Candidate for an encoding of a MachineInstr.
struct MICandidate {
    struct Cond {
        // enum Kind {
        //     NONE,
        //     IMM32S,
        //     IMM8S,
        //     MEM_X86, // can replace register with x86 memory operand, if taken, operand is expanded to four values (base, scale, index, off)
        //     ADDR_X86, // can replace register with x86 address (base+scaledidx+off)
        //     ADDR_OFF12U0, // can replace register with addr+offset where offset is 12 bits unsigned, shifted left by 0 bits
        //     ADDR_OFF12U1, // can replace register with addr+offset where offset is 12 bits unsigned, shifted left by 1 bits
        //     ADDR_OFF12U2, // can replace register with addr+offset where offset is 12 bits unsigned, shifted left by 2 bits
        //     ADDR_OFF12U3, // can replace register with addr+offset where offset is 12 bits unsigned, shifted left by 3 bits
        // };
        unsigned op_idx;
        std::string cond_str;

        Cond() : op_idx(0) {}
        Cond(unsigned op_idx, std::string cond_str) : op_idx(op_idx), cond_str(cond_str) {}
    };

    using Generator = std::function<void(std::string &buf, const llvm::MachineInstr &mi, llvm::SmallVectorImpl<std::string> &ops)>;
    Cond cond;
    Generator generator;

    MICandidate(Generator generator) : cond(), generator(generator) {}
    MICandidate(unsigned op_idx, std::string cond_str, Generator generator) : cond(op_idx, cond_str), generator(generator) {}
};

struct EncodingTarget {
    llvm::MachineFunction *func;

    explicit EncodingTarget(llvm::MachineFunction *func) : func(func) {}

    virtual ~EncodingTarget() = default;

    virtual std::string_view get_invalid_reg() = 0;
    // Get all encoding candidates, the candidate without conditions must be last
    virtual void get_inst_candidates(llvm::MachineInstr &, llvm::SmallVectorImpl<MICandidate> &) {}
    virtual unsigned         reg_id_from_mc_reg(llvm::MCRegister)    = 0;
    virtual std::string_view reg_name_lower(unsigned id)             = 0;
    virtual unsigned         reg_bank(unsigned id)                   = 0;
    // some registers, e.g. flags should be ignored for generation purposes
    virtual bool             reg_should_be_ignored(llvm::MCRegister) = 0;
    virtual void             generate_copy(std::string     &buf,
                                           unsigned         indent,
                                           unsigned         bank,
                                           std::string_view dst,
                                           std::string_view src,
                                           unsigned         size)            = 0;
    virtual bool inst_should_be_skipped(llvm::MachineInstr &inst)    = 0;
};

} // end namespace tpde_encdgen
