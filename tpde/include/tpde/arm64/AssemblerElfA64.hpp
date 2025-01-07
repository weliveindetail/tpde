// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <vector>

#include "tpde/AssemblerElf.hpp"
#include "tpde/util/SmallBitSet.hpp"
#include <disarm64.h>

namespace tpde::a64 {

/// The AArch64-specific implementation for the AssemblerElf
struct AssemblerElfA64 : AssemblerElf<AssemblerElfA64> {
  using Base = AssemblerElf<AssemblerElfA64>;

  static const TargetInfo TARGET_INFO;

  // TODO(ts): maybe move Labels into the compiler since they are kind of more
  // arch specific and probably don't change if u compile Elf/PE/Mach-O? then
  // we could just turn the assemblers into "ObjectWriters"
  // TODO(ts): also reset labels after each function since we basically
  // only use SymRefs for cross-function references now?
  enum class Label : u32 {
  };

  // TODO(ts): smallvector?
  std::vector<u32> label_offsets;
  constexpr static u32 INVALID_LABEL_OFF = ~0u;

  util::SmallBitSet<512> unresolved_labels;

  enum class UnresolvedEntryKind : u8 {
    BR,
    COND_BR,
    TEST_BR,
    JUMP_TABLE,
  };

  struct UnresolvedEntry {
    u32 text_off = 0u;
    u32 next_list_entry = ~0u;
  };

  std::vector<UnresolvedEntry> unresolved_entries;
  std::vector<UnresolvedEntryKind> unresolved_entry_kinds;
  u32 unresolved_next_free_entry = ~0u;

  /// Begin offsets of veneer space. A veneer always has space for all
  /// unresolved cbz/tbz branches that come after it.
  util::SmallVector<u32, 16> veneers;
  u32 unresolved_test_brs = 0, unresolved_cond_brs = 0;

  explicit AssemblerElfA64(const bool gen_obj) : Base{gen_obj} {}

  [[nodiscard]] Label label_create() noexcept;

  [[nodiscard]] u32 label_offset(Label label) const noexcept;

  [[nodiscard]] bool label_is_pending(Label label) const noexcept;

  void add_unresolved_entry(Label label,
                            u32 text_off,
                            UnresolvedEntryKind) noexcept;

  void label_place(Label label) noexcept;

  void emit_jump_table(Label table, std::span<Label> labels) noexcept;

  // relocs
  void reloc_text_call(SymRef target, u32 off) noexcept;

  void reloc_abs(SecRef sec, SymRef target, u32 off, i32 addend) noexcept {
    reloc_sec(sec, target, R_AARCH64_ABS64, off, addend);
  }

  void reloc_pc32(SecRef sec, SymRef target, u32 off, i32 addend) noexcept {
    reloc_sec(sec, target, R_AARCH64_PREL32, off, addend);
  }


  /// Make sure that text_write_ptr can be safely incremented by size
  void text_more_space(u32 size) noexcept;

  void reset() noexcept;
};

inline AssemblerElfA64::Label AssemblerElfA64::label_create() noexcept {
  const auto label = static_cast<Label>(label_offsets.size());
  label_offsets.push_back(INVALID_LABEL_OFF);
  unresolved_labels.push_back(true);
  return label;
}

inline u32 AssemblerElfA64::label_offset(const Label label) const noexcept {
  const auto idx = static_cast<u32>(label);
  assert(idx < label_offsets.size());
  assert(!unresolved_labels.is_set(idx));

  const auto off = label_offsets[idx];
  assert(off != INVALID_LABEL_OFF);
  return off;
}

inline bool
    AssemblerElfA64::label_is_pending(const Label label) const noexcept {
  const auto idx = static_cast<u32>(label);
  assert(idx < label_offsets.size());
  return unresolved_labels.is_set(idx);
}

inline void AssemblerElfA64::add_unresolved_entry(
    Label label, u32 text_off, UnresolvedEntryKind kind) noexcept {
  assert(label_is_pending(label));

  const auto idx = static_cast<u32>(label);
  assert(label_is_pending(label));

  auto pending_head = label_offsets[idx];
  if (unresolved_next_free_entry != ~0u) {
    auto entry = unresolved_next_free_entry;
    unresolved_entries[entry].text_off = text_off;
    unresolved_entry_kinds[entry] = kind;
    unresolved_next_free_entry = unresolved_entries[entry].next_list_entry;
    unresolved_entries[entry].next_list_entry = pending_head;
    label_offsets[idx] = entry;
  } else {
    auto entry = static_cast<u32>(unresolved_entries.size());
    unresolved_entries.push_back(
        UnresolvedEntry{.text_off = text_off, .next_list_entry = pending_head});
    unresolved_entry_kinds.push_back(kind);
    label_offsets[idx] = entry;
  }

  if (kind == UnresolvedEntryKind::COND_BR) {
    ++unresolved_cond_brs;
  } else if (kind == UnresolvedEntryKind::TEST_BR) {
    ++unresolved_test_brs;
  }
}

inline void AssemblerElfA64::label_place(Label label) noexcept {
  const auto idx = static_cast<u32>(label);
  assert(label_is_pending(label));

  auto text_off = text_cur_off();

  auto cur_entry = label_offsets[idx];
  while (cur_entry != ~0u) {
    auto &entry = unresolved_entries[cur_entry];
    u32 *dst_ptr = reinterpret_cast<u32 *>(text_ptr(entry.text_off));

    auto fix_condbr = [=, this](unsigned nbits) {
      i64 diff = (i64)text_off - (i64)entry.text_off;
      assert(diff >= 0 && diff < 128 * 1024 * 1024);
      // lowest two bits are ignored, highest bit is sign bit
      if (diff >= (4 << (nbits - 1))) {
        auto veneer =
            std::lower_bound(veneers.begin(), veneers.end(), entry.text_off);
        assert(veneer != veneers.end());

        // Create intermediate branch at v.begin
        auto *br = reinterpret_cast<u32 *>(text_ptr(*veneer));
        assert(*br == 0 && "overwriting instructions with veneer branch");
        *br = de64_B((text_off - *veneer) / 4);
        diff = *veneer - entry.text_off;
        *veneer += 4;
      }
      u32 off_mask = ((1 << nbits) - 1) << 5;
      *dst_ptr = (*dst_ptr & ~off_mask) | ((diff / 4) << 5);
    };

    switch (unresolved_entry_kinds[cur_entry]) {
    case UnresolvedEntryKind::BR: {
      // diff from entry to label (should be positive tho)
      i64 diff = (i64)text_off - (i64)entry.text_off;
      assert(diff >= 0 && diff < 128 * 1024 * 1024);
      *dst_ptr = de64_B(diff / 4);
      break;
    }
    case UnresolvedEntryKind::COND_BR:
      if (veneers.empty() || veneers.back() < entry.text_off) {
        assert(unresolved_cond_brs > 0);
        unresolved_cond_brs -= 1;
      }
      fix_condbr(19); // CBZ/CBNZ has 19 bits.
      break;
    case UnresolvedEntryKind::TEST_BR:
      if (veneers.empty() || veneers.back() < entry.text_off) {
        assert(unresolved_test_brs > 0);
        unresolved_test_brs -= 1;
      }
      fix_condbr(14); // TBZ/TBNZ has 14 bits.
      break;
    case UnresolvedEntryKind::JUMP_TABLE: {
      const auto table_off = *reinterpret_cast<u32 *>(text_ptr(entry.text_off));
      *dst_ptr = (i32)text_off - (i32)table_off;
      break;
    }
    }
    auto next = entry.next_list_entry;
    entry.next_list_entry = unresolved_next_free_entry;
    unresolved_next_free_entry = cur_entry;
    cur_entry = next;
  }

  label_offsets[idx] = text_off;
  unresolved_labels.mark_unset(idx);
}

inline void
    AssemblerElfA64::emit_jump_table(const Label table,
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

inline void AssemblerElfA64::reloc_text_call(const SymRef target,
                                             const u32 off) noexcept {
  reloc_text(target, R_AARCH64_CALL26, off, 0);
}

inline void AssemblerElfA64::text_more_space(u32 size) noexcept {
  if (text_reserve_end - text_begin >= (128 * 1024 * 1024)) {
    // we do not support multiple text sections currently
    assert(0);
    exit(1);
  }

  u32 veneer_size = sizeof(u32) * (unresolved_test_brs + unresolved_cond_brs);
  Base::text_more_space(size + veneer_size + 4);
  if (veneer_size == 0) {
    return;
  }

  // TBZ has 14 bits, CBZ has 19 bits; but the first bit is the sign bit
  u32 max_dist = unresolved_test_brs ? 4 << (14 - 1) : 4 << (19 - 1);
  max_dist -= veneer_size; // must be able to reach last veneer
  // TODO: get a better approximation of the first unresolved condbr after the
  // last veneer.
  u32 first_condbr = veneers.empty() ? 0 : veneers.back();
  // If all condbrs can only jump inside the now-reserved memory, do nothing.
  if (first_condbr + max_dist > text_reserve_end - text_begin) {
    return;
  }

  u32 cur_off = text_cur_off();
  veneers.push_back(cur_off + 4);
  unresolved_test_brs = unresolved_cond_brs = 0;

  *reinterpret_cast<u32 *>(text_ptr(cur_off)) = de64_B(veneer_size / 4 + 1);
  text_write_ptr += veneer_size + 4;
}

inline void AssemblerElfA64::reset() noexcept {
  label_offsets.clear();
  unresolved_entries.clear();
  unresolved_entry_kinds.clear();
  unresolved_labels.clear();
  unresolved_next_free_entry = ~0u;
  veneers.clear();
  unresolved_test_brs = unresolved_cond_brs = 0;
  Base::reset();
}
} // namespace tpde::a64
