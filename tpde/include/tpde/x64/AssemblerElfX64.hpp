// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/AssemblerElf.hpp"
#include <fadec-enc2.h>

namespace tpde::x64 {

/// The x86_64-specific implementation for the AssemblerElf
struct AssemblerElfX64 : AssemblerElf<AssemblerElfX64> {
  using Base = AssemblerElf<AssemblerElfX64>;

  static const TargetInfo TARGET_INFO;

  enum class UnresolvedEntryKind : u8 {
    JMP_OR_MEM_DISP,
    JUMP_TABLE,
  };

  explicit AssemblerElfX64(const bool gen_obj) : Base{gen_obj} {}

  void add_unresolved_entry(Label label,
                            u32 text_off,
                            UnresolvedEntryKind kind) noexcept {
    AssemblerElfBase::reloc_sec(
        current_section, label, static_cast<u8>(kind), text_off);
  }

  void handle_fixup(const TempSymbolInfo &info,
                    const TempSymbolFixup &fixup) noexcept;

  void emit_jump_table(Label table, std::span<Label> labels) noexcept;

  void text_align_impl(u64 align) noexcept;
};

inline void
    AssemblerElfX64::handle_fixup(const TempSymbolInfo &info,
                                  const TempSymbolFixup &fixup) noexcept {
  // TODO: emit relocations when fixup is in different section
  assert(info.section == fixup.section && "multi-text section not supported");
  u8 *dst_ptr = get_section(fixup.section).data.data() + fixup.off;

  switch (static_cast<UnresolvedEntryKind>(fixup.kind)) {
  case UnresolvedEntryKind::JMP_OR_MEM_DISP: {
    // fix the jump immediate
    u32 value = (info.off - fixup.off) - 4;
    std::memcpy(dst_ptr, &value, sizeof(u32));
    break;
  }
  case UnresolvedEntryKind::JUMP_TABLE: {
    const auto table_off = *reinterpret_cast<u32 *>(dst_ptr);
    const auto diff = (i32)info.off - (i32)table_off;
    std::memcpy(dst_ptr, &diff, sizeof(u32));
    break;
  }
  }
}

inline void
    AssemblerElfX64::emit_jump_table(const Label table,
                                     const std::span<Label> labels) noexcept {
  text_ensure_space(4 + 4 * labels.size());
  text_align(4);
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
      const auto diff = (i32)label_off - (i32)table_off;
      *reinterpret_cast<i32 *>(text_write_ptr) = diff;
    }
    text_write_ptr += 4;
  }
}

inline void AssemblerElfX64::text_align_impl(u64 align) noexcept {
  u32 old_off = text_cur_off();
  Base::text_align_impl(align);
  // Pad text section with NOPs.
  if (u32 cur_off = text_cur_off(); cur_off > old_off) {
    fe64_NOP(text_write_ptr - (cur_off - old_off), cur_off - old_off);
  }
}

} // namespace tpde::x64
