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

    [[nodiscard]] Label label_create() noexcept;

    [[nodiscard]] u32 label_offset(Label label) const noexcept;

    [[nodiscard]] bool label_is_pending(Label label) const noexcept;

    void label_add_unresolved_jump_offset(Label, u32 text_imm32_off) noexcept;

    void label_place(Label label) noexcept;


    // relocs
    void reloc_text_plt32(SymRef, u32 text_imm32_off) noexcept;

    void reloc_text(SymRef sym, u32 type, u64 offset, i64 addend) noexcept;

    void reloc_sec(DataSection &sec,
                   SymRef       sym,
                   u32          type,
                   u64          offset,
                   i64          addend) noexcept;

    void reset() noexcept;
};

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

inline void AssemblerElfX64::reloc_text(const SymRef sym,
                                        const u32    type,
                                        const u64    offset,
                                        const i64    addend) noexcept {
    reloc_sec(sec_text, sym, type, offset, addend);
}

inline void AssemblerElfX64::reloc_sec(DataSection &sec,
                                       const SymRef sym,
                                       const u32    type,
                                       const u64    offset,
                                       const i64    addend) noexcept {
    Elf64_Rela rel{};
    rel.r_offset = offset;
    rel.r_info   = ELF64_R_INFO(sym_idx(sym), type);
    rel.r_addend = addend;
    sec.relocs.push_back(rel);
    if (!sym_is_local(sym)) {
        sec.relocs_to_patch.push_back(sec.relocs.size() - 1);
    }
}

inline void AssemblerElfX64::reset() noexcept {
    label_offsets.clear();
    unresolved_entries.clear();
    unresolved_labels.clear();
    unresolved_next_free_entry = ~0u;
    Base::reset();
}
} // namespace tpde::x64
