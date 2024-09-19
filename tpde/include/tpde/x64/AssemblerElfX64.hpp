// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/AssemblerElf.hpp"
#include "tpde/util/SmallBitSet.hpp"
#include <fadec-enc2.h>

namespace tpde::x64 {

/// The x86_64-specific implementation for the AssemblerElf
struct AssemblerElfX64 : AssemblerElf<AssemblerElfX64> {
    using Base = AssemblerElf<AssemblerElfX64>;

    static constexpr u8         ELF_OS_ABI  = ELFOSABI_SYSV;
    static constexpr Elf64_Half ELF_MACHINE = EM_X86_64;

    // fake register number for the return address
    static constexpr u8 DWARF_EH_RETURN_ADDR_REGISTER = dwarf::x64::DW_reg_ra;

    // TODO(ts): maybe move Labels into the compiler since they are kind of more
    // arch specific and probably don't change if u compile Elf/PE/Mach-O? then
    // we could just turn the assemblers into "ObjectWriters"
    enum class Label : u32 {
    };

    // TODO(ts): smallvector?
    std::vector<u32>     label_offsets;
    constexpr static u32 INVALID_LABEL_OFF = ~0u;

    util::SmallBitSet<512> unresolved_labels;

    struct UnresolvedEntry {
        u32 text_off        = 0u;
        u32 next_list_entry = ~0u;
    };

    std::vector<UnresolvedEntry> unresolved_entries;
    u32                          unresolved_next_free_entry = ~0u;

    explicit AssemblerElfX64(const bool gen_obj) : Base{gen_obj} {}

    void end_func(u64 saved_regs) noexcept;

    [[nodiscard]] Label label_create() noexcept;

    [[nodiscard]] u32 label_offset(Label label) const noexcept;

    [[nodiscard]] bool label_is_pending(Label label) const noexcept;

    void label_add_unresolved_jump_offset(Label, u32 text_imm32_off) noexcept;

    void label_place(Label label) noexcept;


    // relocs
    void reloc_text_plt32(SymRef, u32 text_imm32_off) noexcept;
    void reloc_text_pc32(SymRef sym, u32 text_imm32_off, i32 addend) noexcept;

    void eh_write_initial_cie_instrs() noexcept;

    void reset() noexcept;
};

inline void AssemblerElfX64::end_func(const u64 saved_regs) noexcept {
    Base::end_func();

    const auto fde_off = eh_write_fde_start();

    // relocate the func_start to the function
    // relocate against .text so we don't have to fix up any relocations
    const auto func_off = sym_ptr(cur_func)->st_value;
    this->reloc_sec(sec_eh_frame,
                    TEXT_SYM_REF,
                    R_X86_64_PC32,
                    fde_off + dwarf::EH_FDE_FUNC_START_OFF,
                    static_cast<u32>(func_off));

    // write out the saved registers

    // register saves start at CFA - 24
    u32 cur_off = 24;
    for (auto reg_id : util::BitSetIterator<>(saved_regs)) {
        if (reg_id >= 32) {
            reg_id -= 15; // vector register ids start at 32 but dwarf encodes
                          // them starting at 17
        }
        // cfa_offset reg, cur_off (reg = CFA - cur_off)
        eh_write_inst(dwarf::DW_CFA_offset, reg_id, cur_off);
        // TODO(ts): this does not cope with register saves of xmm regs > 8
        // bytes
        cur_off += 8;
    }

    this->eh_write_fde_len(fde_off);
}

inline AssemblerElfX64::Label AssemblerElfX64::label_create() noexcept {
    const auto label = static_cast<Label>(label_offsets.size());
    label_offsets.push_back(INVALID_LABEL_OFF);
    unresolved_labels.push_back(true);
    return label;
}

inline u32 AssemblerElfX64::label_offset(const Label label) const noexcept {
    const auto idx = static_cast<u32>(label);
    assert(idx < label_offsets.size());
    assert(!unresolved_labels.is_set(idx));

    const auto off = label_offsets[idx];
    assert(off != INVALID_LABEL_OFF);
    return off;
}

inline bool
    AssemblerElfX64::label_is_pending(const Label label) const noexcept {
    const auto idx = static_cast<u32>(label);
    assert(idx < label_offsets.size());
    return unresolved_labels.is_set(idx);
}

inline void AssemblerElfX64::label_add_unresolved_jump_offset(
    Label label, const u32 text_imm32_off) noexcept {
    const auto idx = static_cast<u32>(label);
    assert(label_is_pending(label));

    auto pending_head = label_offsets[idx];
    if (unresolved_next_free_entry != ~0u) {
        auto entry                         = unresolved_next_free_entry;
        unresolved_entries[entry].text_off = text_imm32_off;
        unresolved_next_free_entry = unresolved_entries[entry].next_list_entry;
        unresolved_entries[entry].next_list_entry = pending_head;
        label_offsets[idx]                        = entry;
    } else {
        auto entry = static_cast<u32>(unresolved_entries.size());
        unresolved_entries.push_back(UnresolvedEntry{
            .text_off = text_imm32_off, .next_list_entry = pending_head});
        label_offsets[idx] = entry;
    }
}

inline void AssemblerElfX64::label_place(Label label) noexcept {
    const auto idx = static_cast<u32>(label);
    assert(label_is_pending(label));

    auto text_off = text_cur_off();

    auto cur_entry = label_offsets[idx];
    while (cur_entry != ~0u) {
        auto &entry = unresolved_entries[cur_entry];
        // fix the jump immediate
        *reinterpret_cast<u32 *>(sec_text.data.data() + entry.text_off) =
            (text_off - entry.text_off) - 4;
        auto next                  = entry.next_list_entry;
        entry.next_list_entry      = unresolved_next_free_entry;
        unresolved_next_free_entry = cur_entry;
        cur_entry                  = next;
    }

    label_offsets[idx] = text_off;
    unresolved_labels.mark_unset(idx);
}

inline void
    AssemblerElfX64::reloc_text_plt32(const SymRef sym,
                                      const u32    text_imm32_off) noexcept {
    reloc_text(sym, R_X86_64_PLT32, text_imm32_off, -4);
}

inline void AssemblerElfX64::reloc_text_pc32(SymRef sym,
                                             u32    text_imm32_off,
                                             i32    addend) noexcept {
    reloc_text(sym, R_X86_64_PC32, text_imm32_off, addend);
}

inline void AssemblerElfX64::eh_write_initial_cie_instrs() noexcept {
    // we always emit a frame-setup so we can encode that in the CIE

    // def_cfa rbp, 16 (CFA = rbp + 16)
    eh_write_inst(dwarf::DW_CFA_def_cfa, dwarf::x64::DW_reg_rbp, 16);
    // cfa_offset ra, 8 (ra = CFA - 8)
    eh_write_inst(dwarf::DW_CFA_offset, dwarf::x64::DW_reg_ra, 8);
    // cfa_offset rbp, 16 (rbp = CFA - 16)
    eh_write_inst(dwarf::DW_CFA_offset, dwarf::x64::DW_reg_rbp, 16);
}

inline void AssemblerElfX64::reset() noexcept {
    label_offsets.clear();
    unresolved_entries.clear();
    unresolved_labels.clear();
    unresolved_next_free_entry = ~0u;
    Base::reset();
}
} // namespace tpde::x64
