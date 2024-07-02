// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "RegisterFile.hpp"

namespace tpde {
template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::ScratchReg {
    CompilerBase *compiler;
    // TODO(ts): get this using the CompilerConfig?
    u8            cur_reg = 0xFF;

    explicit ScratchReg(CompilerBase *compiler) : compiler(compiler) {}

    ~ScratchReg() noexcept { reset(); }

    [[nodiscard]] AsmReg alloc_gp() noexcept;
    [[nodiscard]] AsmReg alloc_from_bank(u8 bank) noexcept;

    [[nodiscard]] AsmReg alloc_from_bank_excluding(u8  bank,
                                                   u64 exclusion_mask) noexcept;

    void reset() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ScratchReg::alloc_gp() noexcept {
    return alloc_from_bank_excluding(Config::GP_BANK, 0);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ScratchReg::alloc_from_bank(
        const u8 bank) noexcept {
    return alloc_from_bank_excluding(bank, 0);
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
CompilerBase<Adaptor, Derived, Config>::AsmReg
    CompilerBase<Adaptor, Derived, Config>::ScratchReg::
        alloc_from_bank_excluding(u8 bank, u64 exclusion_mask) noexcept {
    auto &reg_file = compiler->register_file;
    if (cur_reg != 0xFF) {
        if (bank == reg_file.reg_bank(cur_reg)
            && (exclusion_mask & (1ull << cur_reg)) == 0) {
            return AsmReg{cur_reg};
        }
        reg_file.unmark_fixed(cur_reg);
        reg_file.unmark_used(cur_reg);
        cur_reg = 0xFF;
    }

    auto reg = reg_file.find_first_free_excluding(bank, exclusion_mask);
    if (reg.invalid()) {
        // TODO(ts): use clock here?
        reg = reg_file.find_first_nonfixed_excluding(bank, exclusion_mask);
        if (reg.invalid()) {
            TPDE_LOG_ERR("ran out of registers for scratch registers");
            assert(0);
            exit(1);
        }

        AssignmentPartRef part{
            compiler->val_assignment(reg_file.reg_local_idx(reg)),
            reg_file.reg_part(reg)};
        part.spill();
        reg_file.unmark_used(reg);
    }

    reg_file.mark_used(reg, Adaptor::INVALID_VALUE_REF, 0);
    reg_file.mark_fixed(reg);
    cur_reg = reg.id();
    return reg;
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ScratchReg::reset() noexcept {
    if (cur_reg == 0xFF) {
        return;
    }

    compiler->register_file.unmark_fixed(cur_reg);
    compiler->register_file.unmark_used(cur_reg);
    cur_reg = 0xFF;
}
} // namespace tpde
