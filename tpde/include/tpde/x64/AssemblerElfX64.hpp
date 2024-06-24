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

    void reset() noexcept;
};

inline AssemblerElfX64::Label AssemblerElfX64::label_create() noexcept {
    const auto label = static_cast<Label>(label_offsets.size());
    label_offsets.push_back(INVALID_LABEL_OFF);
    unresolved_labels.push_back(false);
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

inline void AssemblerElfX64::reset() noexcept {
    label_offsets.clear();
    unresolved_entries.clear();
    unresolved_labels.clear();
    unresolved_next_free_entry = ~0u;
    Base::reset();
}
} // namespace tpde::x64
