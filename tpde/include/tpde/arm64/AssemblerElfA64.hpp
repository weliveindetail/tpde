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

  struct VeneerInfo {
    u32 off;
    u16 insts_used;
    u16 max_insts;
    UnresolvedEntryKind ty;
  };

  util::SmallVector<VeneerInfo, 16> veneers;
  u32 unresolved_test_brs = 0, unresolved_cond_brs = 0;
  u32 last_cond_veneer_off = 0;

  explicit AssemblerElfA64(const bool gen_obj) : Base{gen_obj} {}

  void end_func(u64 saved_regs, u32 frame_size) noexcept;

  [[nodiscard]] Label label_create() noexcept;

  [[nodiscard]] u32 label_offset(Label label) const noexcept;

  [[nodiscard]] bool label_is_pending(Label label) const noexcept;

  void add_unresolved_entry(Label label,
                            u32 text_off,
                            UnresolvedEntryKind) noexcept;

  VeneerInfo &find_nearest_veneer(u32 jmp_off, UnresolvedEntryKind ty) noexcept;
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

inline void AssemblerElfA64::end_func(const u64 saved_regs,
                                      const u32 frame_size) noexcept {
  Base::end_func();

  const auto fde_off = eh_write_fde_start();

  // relocate the func_start to the function
  // relocate against .text so we don't have to fix up any relocations
  const auto func_off = sym_ptr(cur_func)->st_value;
  this->reloc_sec(secref_eh_frame,
                  get_section(current_section).sym,
                  R_AARCH64_PREL32,
                  fde_off + dwarf::EH_FDE_FUNC_START_OFF,
                  static_cast<u32>(func_off));

  // setup the CFA
  // CFA = FP + frame_size
  eh_write_inst(dwarf::DW_CFA_def_cfa, dwarf::a64::DW_reg_fp, frame_size);
  // FP = CFA - frame_size
  eh_write_inst(dwarf::DW_CFA_offset, dwarf::a64::DW_reg_fp, frame_size);
  // LR = CFA - frame_size + 8
  eh_write_inst(dwarf::DW_CFA_offset, dwarf::a64::DW_reg_lr, frame_size - 8);

  // write out the saved registers

  // register saves start at CFA - frame_size + 16
  u32 cur_off = frame_size - 16;
  for (auto reg_id : util::BitSetIterator<>(saved_regs)) {
    if (reg_id >= 32) {
      reg_id = reg_id - 32 + dwarf::a64::DW_reg_v0;
    }

    // cfa_offset reg, cur_off (reg = CFA - cur_off)
    if ((reg_id & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) == 0) {
      eh_write_inst(dwarf::DW_CFA_offset, reg_id, cur_off);
    } else {
      eh_write_inst(dwarf::DW_CFA_offset_extended, reg_id, cur_off);
    }
    // hardcodes register saves of 8 bytes
    cur_off -= 8;
  }

  this->eh_write_fde_len(fde_off);
  this->except_encode_func();
}

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

inline AssemblerElfA64::VeneerInfo &
    AssemblerElfA64::find_nearest_veneer(u32 jmp_off,
                                         UnresolvedEntryKind ty) noexcept {
  assert(!veneers.empty());

  // find the veneer that comes closest after the specified imm offset
  // and fits the type
  for (u32 i = 0; i < veneers.size(); i++) {
    auto &e = veneers[i];
    if (e.off < jmp_off || e.ty != ty) {
      continue;
    }
    return e;
  }

  assert(0);
  exit(1);
}

inline void AssemblerElfA64::label_place(Label label) noexcept {
  const auto idx = static_cast<u32>(label);
  assert(label_is_pending(label));

  auto text_off = text_cur_off();

  auto cur_entry = label_offsets[idx];
  while (cur_entry != ~0u) {
    auto &entry = unresolved_entries[cur_entry];
    switch (unresolved_entry_kinds[cur_entry]) {
    case UnresolvedEntryKind::BR: {
      // diff from entry to label (should be positive tho)
      i64 diff = (i64)text_off - (i64)entry.text_off;
      assert(diff >= 0);
      assert(diff < 128 * 1024 * 1024);
      auto inst = de64_B(diff / 4);
      assert(inst != 0);
      *reinterpret_cast<u32 *>(text_ptr(entry.text_off)) = inst;
      break;
    }
    case UnresolvedEntryKind::COND_BR: {
      // diff from entry to label (should be positive tho)
      i64 diff = (i64)text_off - (i64)entry.text_off;
      assert(diff >= 0);
      // functions should fit in 128mb
      assert(diff < 128 * 1024 * 1024);
      // TODO(ts): < or <=?
      if (diff < 1024 * 1024) {
        // we can write the offset into the instruction and don't need a
        // veneer
        const auto cur_inst =
            *reinterpret_cast<u32 *>(text_ptr(entry.text_off));
        const auto new_inst = (cur_inst & 0xFF00'001F) | ((diff / 4) << 5);
        *reinterpret_cast<u32 *>(text_ptr(entry.text_off)) = new_inst;

        // if there was no veneer after this branch it is still present
        // in pendingCondBrs so we can remove it
        if (veneers.empty() || last_cond_veneer_off < entry.text_off) {
          assert(unresolved_cond_brs > 0);
          unresolved_cond_brs -= 1;
        }
      } else {
        // need to use venner
        VeneerInfo &v = find_nearest_veneer(entry.text_off,
                                            unresolved_entry_kinds[cur_entry]);
        assert(v.insts_used + 5 <= v.max_insts);

        const auto trampoline_off = v.off + (v.insts_used * sizeof(u32));
        auto *w = reinterpret_cast<u32 *>(text_ptr(trampoline_off));
        // ADR x16, [pc + 4*4]
        *w++ = de64_ADR(DA_GP(16), 0, 16);
        // LDR w17, [pc + 3*4]
        *w++ = de64_LDRw_pcrel(DA_GP(17), 3);
        *w++ = de64_ADDx(DA_GP(16), DA_GP(16), DA_GP(17));
        *w++ = de64_BR(DA_GP(16));

        const auto const_off = v.off + v.insts_used * 4 + 4 * 4;
        *w = text_off - const_off;

        const auto cur_inst =
            *reinterpret_cast<u32 *>(text_ptr(entry.text_off));
        const auto new_inst = (cur_inst & 0xFF00'001F) |
                              (((trampoline_off - entry.text_off) / 4) << 5);
        *reinterpret_cast<u32 *>(text_ptr(entry.text_off)) = new_inst;

        v.insts_used += 5;
      }
      break;
    }
    case UnresolvedEntryKind::TEST_BR: {
      // diff from entry to label (should be positive tho)
      i64 diff = (i64)text_off - (i64)entry.text_off;
      assert(diff >= 0);
      // functions should fit in 128mb
      assert(diff < 128 * 1024 * 1024);
      // TODO(ts): < or <=?
      if (diff < 32 * 1024) {
        // we can write the offset into the instruction and don't need a
        // veneer
        const auto cur_inst =
            *reinterpret_cast<u32 *>(text_ptr(entry.text_off));
        const auto new_inst = (cur_inst & 0xFF80'001F) | ((diff / 4) << 5);
        *reinterpret_cast<u32 *>(text_ptr(entry.text_off)) = new_inst;

        // if there was no veneer after this branch it is still present
        // in pendingCondBrs so we can remove it
        if (veneers.empty() || veneers.back().off < entry.text_off) {
          assert(unresolved_test_brs > 0);
          unresolved_test_brs -= 1;
        }
      } else {
        // need to use venner
        // TOOD(ts): deduplicate with above
        VeneerInfo &v = find_nearest_veneer(entry.text_off,
                                            unresolved_entry_kinds[cur_entry]);
        assert(v.insts_used + 5 <= v.max_insts);

        const auto trampoline_off = v.off + (v.insts_used * sizeof(u32));
        auto *w = reinterpret_cast<u32 *>(text_ptr(trampoline_off));
        // ADR x16, [pc + 4*4]
        *w++ = de64_ADR(DA_GP(16), 0, 16);
        // LDR w17, [pc + 3*4]
        *w++ = de64_LDRw_pcrel(DA_GP(17), 3);
        *w++ = de64_ADDx(DA_GP(16), DA_GP(16), DA_GP(17));
        *w++ = de64_BR(DA_GP(16));

        const auto const_off = v.off + v.insts_used * 4 + 4 * 4;
        *w = text_off - const_off;

        const auto cur_inst =
            *reinterpret_cast<u32 *>(text_ptr(entry.text_off));
        const auto new_inst = (cur_inst & 0xFF00'001F) |
                              (((trampoline_off - entry.text_off) / 4) << 5);
        *reinterpret_cast<u32 *>(text_ptr(entry.text_off)) = new_inst;

        v.insts_used += 5;
      }
      break;
    }
    case UnresolvedEntryKind::JUMP_TABLE: {
      const auto table_off = *reinterpret_cast<u32 *>(text_ptr(entry.text_off));
      const auto diff = (i32)text_off - (i32)table_off;
      *reinterpret_cast<i32 *>(text_ptr(entry.text_off)) = diff;
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

  u32 veneer_size = 0;

  const auto cur_off = text_cur_off();
  if (unresolved_test_brs) {
    veneer_size = unresolved_test_brs * (5 * 4);
    veneers.push_back(VeneerInfo{.off = cur_off + 4,
                                 .insts_used = 0,
                                 .max_insts = (u16)(unresolved_test_brs * 5),
                                 .ty = UnresolvedEntryKind::TEST_BR});
    unresolved_test_brs = 0;
  }

  if (unresolved_cond_brs && (cur_off - last_cond_veneer_off) >=
                                 (1024 * 1024 - 16 * 1024 - veneer_size)) {
    const u32 off = cur_off + 4 + veneer_size;
    veneer_size += unresolved_cond_brs * (5 * 4);
    veneers.push_back(VeneerInfo{.off = off,
                                 .insts_used = 0,
                                 .max_insts = (u16)(unresolved_cond_brs * 5),
                                 .ty = UnresolvedEntryKind::COND_BR});
    unresolved_cond_brs = 0;
    last_cond_veneer_off = off;
  }

  Base::text_more_space(size + veneer_size + 4);
  if (veneer_size != 0) {
    *reinterpret_cast<u32 *>(text_ptr(cur_off)) = de64_B((4 + veneer_size) / 4);
    text_write_ptr += veneer_size + 4;
  }
}

inline void AssemblerElfA64::reset() noexcept {
  label_offsets.clear();
  unresolved_entries.clear();
  unresolved_entry_kinds.clear();
  unresolved_labels.clear();
  unresolved_next_free_entry = ~0u;
  veneers.clear();
  unresolved_test_brs = unresolved_cond_brs = 0;
  last_cond_veneer_off = 0;
  Base::reset();
}
} // namespace tpde::a64
