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
    // TODO(ts): also reset labels after each function since we basically
    // only use SymRefs for cross-function references now?
    enum class Label : u32 {
    };

    // TODO(ts): smallvector?
    std::vector<u32>     label_offsets;
    constexpr static u32 INVALID_LABEL_OFF = ~0u;

    util::SmallBitSet<512> unresolved_labels;

    enum class UnresolvedEntryKind : u8 {
        JMP_OR_MEM_DISP,
        JUMP_TABLE,
    };

    struct UnresolvedEntry {
        u32 text_off        = 0u;
        u32 next_list_entry = ~0u;
    };

    std::vector<UnresolvedEntry>     unresolved_entries;
    std::vector<UnresolvedEntryKind> unresolved_entry_kinds;
    u32                              unresolved_next_free_entry = ~0u;

    explicit AssemblerElfX64(const bool gen_obj) : Base{gen_obj} {}

    void end_func(u64 saved_regs) noexcept;

    [[nodiscard]] Label label_create() noexcept;

    [[nodiscard]] u32 label_offset(Label label) const noexcept;

    [[nodiscard]] bool label_is_pending(Label label) const noexcept;

    void label_add_unresolved_jump_offset(Label, u32 text_imm32_off) noexcept;
    void add_unresolved_entry(Label label,
                              u32   text_off,
                              UnresolvedEntryKind) noexcept;

    void label_place(Label label) noexcept;

    void emit_jump_table(Label table, std::span<Label> labels) noexcept;

    // relocs
    void reloc_text_plt32(SymRef, u32 text_imm32_off) noexcept;
    void reloc_text_pc32(SymRef sym, u32 text_imm32_off, i32 addend) noexcept;
    void reloc_text_got(SymRef sym, u32 text_imm32_off, i32 addend) noexcept;

    void reloc_abs_init(SymRef target, bool init, u32 off, i32 addend) noexcept;

    void reloc_data_abs(SymRef target,
                        bool   read_only,
                        u32    off,
                        i32    addend) noexcept;
    void reloc_data_pc32(SymRef target,
                         bool   read_only,
                         u32    off,
                         i32    addend) noexcept;

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
    add_unresolved_entry(
        label, text_imm32_off, UnresolvedEntryKind::JMP_OR_MEM_DISP);
}

inline void AssemblerElfX64::add_unresolved_entry(
    Label label, u32 text_off, UnresolvedEntryKind kind) noexcept {
    assert(label_is_pending(label));

    const auto idx = static_cast<u32>(label);
    assert(label_is_pending(label));

    auto pending_head = label_offsets[idx];
    if (unresolved_next_free_entry != ~0u) {
        auto entry                         = unresolved_next_free_entry;
        unresolved_entries[entry].text_off = text_off;
        unresolved_entry_kinds[entry]      = kind;
        unresolved_next_free_entry = unresolved_entries[entry].next_list_entry;
        unresolved_entries[entry].next_list_entry = pending_head;
        label_offsets[idx]                        = entry;
    } else {
        auto entry = static_cast<u32>(unresolved_entries.size());
        unresolved_entries.push_back(UnresolvedEntry{
            .text_off = text_off, .next_list_entry = pending_head});
        unresolved_entry_kinds.push_back(kind);
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
        switch (unresolved_entry_kinds[cur_entry]) {
        case UnresolvedEntryKind::JMP_OR_MEM_DISP: {
            // fix the jump immediate
            *reinterpret_cast<u32 *>(sec_text.data.data() + entry.text_off) =
                (text_off - entry.text_off) - 4;
            break;
        }
        case UnresolvedEntryKind::JUMP_TABLE: {
            const auto table_off =
                *reinterpret_cast<u32 *>(sec_text.data.data() + entry.text_off);
            const auto diff = (i32)text_off - (i32)table_off;
            *reinterpret_cast<i32 *>(sec_text.data.data() + entry.text_off) =
                diff;
            break;
        }
        }
        auto next                  = entry.next_list_entry;
        entry.next_list_entry      = unresolved_next_free_entry;
        unresolved_next_free_entry = cur_entry;
        cur_entry                  = next;
    }

    label_offsets[idx] = text_off;
    unresolved_labels.mark_unset(idx);
}

inline void
    AssemblerElfX64::emit_jump_table(const Label            table,
                                     const std::span<Label> labels) noexcept {
    text_ensure_space(4 + 4 * labels.size());
    text_align_4();
    label_place(table);
    const auto table_off = text_cur_off();
    for (u32 i = 0; i < labels.size(); i++) {
        const auto entry_off = table_off + 4 * i;
        if (label_is_pending(labels[i])) {
            *reinterpret_cast<u32 *>(text_write_ptr) = table_off;
            add_unresolved_entry(
                labels[i], entry_off, UnresolvedEntryKind::JUMP_TABLE);
        } else {
            const auto label_off = this->label_offset(labels[i]);
            const auto diff      = (i32)label_off - (i32)table_off;
            *reinterpret_cast<i32 *>(text_write_ptr) = diff;
        }
        text_write_ptr += 4;
    }
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

inline void AssemblerElfX64::reloc_text_got(const SymRef sym,
                                            const u32    text_imm32_off,
                                            const i32    addend) noexcept {
    reloc_text(sym, R_X86_64_GOTPCREL, text_imm32_off, addend);
}

inline void AssemblerElfX64::reloc_abs_init(const SymRef target,
                                            const bool   init,
                                            const u32    off,
                                            const i32    addend) noexcept {
    if (init) {
        reloc_sec(sec_init_array, target, R_X86_64_64, off, addend);
    } else {
        reloc_sec(sec_fini_array, target, R_X86_64_64, off, addend);
    }
}

inline void AssemblerElfX64::reloc_data_abs(const SymRef target,
                                            const bool   read_only,
                                            const u32    off,
                                            const i32    addend) noexcept {
    if (read_only) {
        reloc_sec(sec_relrodata, target, R_X86_64_64, off, addend);
    } else {
        reloc_sec(sec_data, target, R_X86_64_64, off, addend);
    }
}

inline void AssemblerElfX64::reloc_data_pc32(const SymRef target,
                                             const bool   read_only,
                                             const u32    off,
                                             const i32    addend) noexcept {
    if (read_only) {
        reloc_sec(sec_relrodata, target, R_X86_64_PC32, off, addend);
    } else {
        reloc_sec(sec_data, target, R_X86_64_PC32, off, addend);
    }
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
    unresolved_entry_kinds.clear();
    unresolved_labels.clear();
    unresolved_next_free_entry = ~0u;
    Base::reset();
}
} // namespace tpde::x64
