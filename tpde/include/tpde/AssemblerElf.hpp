// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <cassert>
#include <cstdlib>
#include <elf.h>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "base.hpp"
#include "util/SmallVector.hpp"
#include "util/misc.hpp"

namespace tpde {

namespace dwarf {
// DWARF constants
constexpr u8 DW_CFA_nop = 0;
constexpr u8 DW_EH_PE_uleb128 = 0x01;
constexpr u8 DW_EH_PE_pcrel = 0x10;
constexpr u8 DW_EH_PE_indirect = 0x80;
constexpr u8 DW_EH_PE_sdata4 = 0x0b;
constexpr u8 DW_EH_PE_omit = 0xff;

constexpr u8 DW_CFA_offset_extended = 0x05;
constexpr u8 DW_CFA_def_cfa = 0x0c;
constexpr u8 DW_CFA_def_cfa_register = 0x0d;
constexpr u8 DW_CFA_def_cfa_offset = 0x0e;
constexpr u8 DW_CFA_offset = 0x80;
constexpr u8 DW_CFA_advance_loc = 0x40;
constexpr u8 DW_CFA_advance_loc4 = 0x04;

constexpr u8 DWARF_CFI_PRIMARY_OPCODE_MASK = 0xc0;

constexpr u32 EH_FDE_FUNC_START_OFF = 0x8;

namespace x64 {
constexpr u8 DW_reg_rax = 0;
constexpr u8 DW_reg_rdx = 1;
constexpr u8 DW_reg_rcx = 2;
constexpr u8 DW_reg_rbx = 3;
constexpr u8 DW_reg_rsi = 4;
constexpr u8 DW_reg_rdi = 5;
constexpr u8 DW_reg_rbp = 6;
constexpr u8 DW_reg_rsp = 7;
constexpr u8 DW_reg_r8 = 8;
constexpr u8 DW_reg_r9 = 9;
constexpr u8 DW_reg_r10 = 10;
constexpr u8 DW_reg_r11 = 11;
constexpr u8 DW_reg_r12 = 12;
constexpr u8 DW_reg_r13 = 13;
constexpr u8 DW_reg_r14 = 14;
constexpr u8 DW_reg_r15 = 15;
constexpr u8 DW_reg_ra = 16;
} // namespace x64

namespace a64 {
constexpr u8 DW_reg_x0 = 0;
constexpr u8 DW_reg_x1 = 1;
constexpr u8 DW_reg_x2 = 2;
constexpr u8 DW_reg_x3 = 3;
constexpr u8 DW_reg_x4 = 4;
constexpr u8 DW_reg_x5 = 5;
constexpr u8 DW_reg_x6 = 6;
constexpr u8 DW_reg_x7 = 7;
constexpr u8 DW_reg_x8 = 8;
constexpr u8 DW_reg_x9 = 9;
constexpr u8 DW_reg_x10 = 10;
constexpr u8 DW_reg_x11 = 11;
constexpr u8 DW_reg_x12 = 12;
constexpr u8 DW_reg_x13 = 13;
constexpr u8 DW_reg_x14 = 14;
constexpr u8 DW_reg_x15 = 15;
constexpr u8 DW_reg_x16 = 16;
constexpr u8 DW_reg_x17 = 17;
constexpr u8 DW_reg_x18 = 18;
constexpr u8 DW_reg_x19 = 19;
constexpr u8 DW_reg_x20 = 20;
constexpr u8 DW_reg_x21 = 21;
constexpr u8 DW_reg_x22 = 22;
constexpr u8 DW_reg_x23 = 23;
constexpr u8 DW_reg_x24 = 24;
constexpr u8 DW_reg_x25 = 25;
constexpr u8 DW_reg_x26 = 26;
constexpr u8 DW_reg_x27 = 27;
constexpr u8 DW_reg_x28 = 28;
constexpr u8 DW_reg_x29 = 29;
constexpr u8 DW_reg_x30 = 30;

constexpr u8 DW_reg_fp = 29;
constexpr u8 DW_reg_lr = 30;

constexpr u8 DW_reg_v0 = 64;
constexpr u8 DW_reg_v1 = 65;
constexpr u8 DW_reg_v2 = 66;
constexpr u8 DW_reg_v3 = 67;
constexpr u8 DW_reg_v4 = 68;
constexpr u8 DW_reg_v5 = 69;
constexpr u8 DW_reg_v6 = 70;
constexpr u8 DW_reg_v7 = 71;
constexpr u8 DW_reg_v8 = 72;
constexpr u8 DW_reg_v9 = 73;
constexpr u8 DW_reg_v10 = 74;
constexpr u8 DW_reg_v11 = 75;
constexpr u8 DW_reg_v12 = 76;
constexpr u8 DW_reg_v13 = 77;
constexpr u8 DW_reg_v14 = 78;
constexpr u8 DW_reg_v15 = 79;
constexpr u8 DW_reg_v16 = 80;
constexpr u8 DW_reg_v17 = 81;
constexpr u8 DW_reg_v18 = 82;
constexpr u8 DW_reg_v19 = 83;
constexpr u8 DW_reg_v20 = 84;
constexpr u8 DW_reg_v21 = 85;
constexpr u8 DW_reg_v22 = 86;
constexpr u8 DW_reg_v23 = 87;
constexpr u8 DW_reg_v24 = 88;
constexpr u8 DW_reg_v25 = 89;
constexpr u8 DW_reg_v26 = 90;
constexpr u8 DW_reg_v27 = 91;
constexpr u8 DW_reg_v28 = 92;
constexpr u8 DW_reg_v29 = 93;
constexpr u8 DW_reg_v30 = 94;

constexpr u8 DW_reg_sp = 31;
constexpr u8 DW_reg_pc = 32;
} // namespace a64

} // namespace dwarf

struct AssemblerElfBase {
  template <class Derived>
  friend struct AssemblerElf;
  friend class ElfMapper;

  struct TargetInfo {
    /// The OS ABI for the ELF header.
    u8 elf_osabi;
    /// The machine for the ELF header.
    u16 elf_machine;

    /// The return address register for the CIE.
    u8 cie_return_addr_register;
    /// The initial instructions for the CIE.
    std::span<const u8> cie_instrs;

    /// The relocation type for 32-bit pc-relative offsets.
    u32 reloc_pc32;
    /// The relocation type for 64-bit absolute addresses.
    u32 reloc_abs64;
  };

  enum class SymBinding : u8 {
    /// Symbol with local linkage, must be defined
    LOCAL,
    /// Weak linkage
    WEAK,
    /// Global linkage
    GLOBAL,
  };

  struct SymRef {
  private:
    u32 val;

  public:
    /// Invalid symbol reference
    constexpr SymRef() : val(0) {}

    explicit constexpr SymRef(u32 id) : val(id) {}

    u32 id() const { return val; }
    bool valid() const { return val != 0; }

    bool operator==(const SymRef &other) const { return other.val == val; }
  };

  enum class SecRef : u32 {
  };
  static constexpr SecRef INVALID_SEC_REF = static_cast<SecRef>(0u);

  // TODO: merge Label with SymRef when adding private symbols
  enum class Label : u32 {
  };

  // TODO(ts): 32 bit version?
  struct DataSection {
    std::vector<u8> data;
    std::vector<Elf64_Rela> relocs;
    std::vector<u32> relocs_to_patch;

    Elf64_Shdr hdr;
    SymRef sym;

    DataSection() = default;
    DataSection(unsigned type, unsigned flags, unsigned name_off)
        : hdr{.sh_name = name_off, .sh_type = type, .sh_flags = flags} {}

    size_t size() const { return (hdr.sh_type == SHT_NOBITS) ? hdr.sh_size : data.size(); }
  };

private:
  const TargetInfo &target_info;

  util::SmallVector<DataSection, 16> sections;

  std::vector<Elf64_Sym> global_symbols, local_symbols;

protected:
  struct TempSymbolInfo {
    /// Section, or INVALID_SEC_REF if pending
    SecRef section;
    /// Offset into section, or index into temp_symbol_fixups if pending
    union {
      u32 fixup_idx;
      u32 off;
    };
  };

  struct TempSymbolFixup {
    SecRef section;
    u32 next_list_entry;
    u32 off;
    u8 kind;
  };

private:
  std::vector<TempSymbolInfo> temp_symbols;
  std::vector<TempSymbolFixup> temp_symbol_fixups;
  u32 next_free_tsfixup = ~0u;

  std::vector<char> strtab;

protected:
  SecRef secref_text = INVALID_SEC_REF;
  SecRef secref_rodata = INVALID_SEC_REF;
  SecRef secref_relro = INVALID_SEC_REF;
  SecRef secref_data = INVALID_SEC_REF;
  SecRef secref_bss = INVALID_SEC_REF;
  SecRef secref_tdata = INVALID_SEC_REF;
  SecRef secref_tbss = INVALID_SEC_REF;

  SecRef secref_init_array = INVALID_SEC_REF;
  SecRef secref_fini_array = INVALID_SEC_REF;

  /// Unwind Info
  SecRef secref_eh_frame = INVALID_SEC_REF;
  SecRef secref_except_table = INVALID_SEC_REF;

  struct ExceptCallSiteInfo {
    /// Start offset *in section* (not inside function)
    u64 start;
    u64 len;
    u32 pad_label_or_off;
    u32 action_entry;
  };

public:
  /// Exception Handling temporary storage
  /// Call Sites for current function
  std::vector<ExceptCallSiteInfo> except_call_site_table;

protected:
  /// Temporary storage for encoding call sites
  std::vector<u8> except_encoded_call_sites;
  /// Action Table for current function
  std::vector<u8> except_action_table;
  /// The type_info table (contains the symbols which contain the pointers to
  /// the type_info)
  std::vector<SymRef> except_type_info_table;
  /// Table for exception specs
  std::vector<u8> except_spec_table;
  /// The current personality function (if any)
  SymRef cur_personality_func_addr;
  u32 eh_cur_cie_off = 0u;
  u32 eh_first_fde_off = 0;

  /// Is the objective(heh) to generate an object file or to map into memory?
  bool generating_object;
  /// The current function
  SymRef cur_func;

public:
  explicit AssemblerElfBase(const TargetInfo &target_info,
                            bool generating_object)
      : target_info(target_info), generating_object(generating_object) {
    strtab.push_back('\0');

    local_symbols.resize(1); // First symbol must be null.
    init_sections();
    eh_init_cie();
  }

  void reset() noexcept;

  // Sections

  DataSection &get_section(SecRef ref) noexcept {
    assert(ref != INVALID_SEC_REF);
    return sections[static_cast<u32>(ref)];
  }

  const DataSection &get_section(SecRef ref) const noexcept {
    assert(ref != INVALID_SEC_REF);
    return sections[static_cast<u32>(ref)];
  }

private:
  void init_sections() noexcept;

  DataSection &get_or_create_section(SecRef &ref,
                                     unsigned rela_name,
                                     unsigned type,
                                     unsigned flags,
                                     unsigned align,
                                     bool with_rela = true) noexcept;

public:
  SecRef get_data_section(bool rodata, bool relro = false) noexcept;
  SecRef get_bss_section() noexcept;
  SecRef get_tdata_section() noexcept;
  SecRef get_tbss_section() noexcept;
  SecRef get_structor_section(bool init) noexcept;
  SecRef get_eh_frame_section() const noexcept { return secref_eh_frame; }

  const char *sec_name(SecRef ref) const noexcept;

  // Symbols

  void sym_copy(SymRef dst, SymRef src) noexcept;

private:
  [[nodiscard]] SymRef
      sym_add(std::string_view name, SymBinding binding, u32 type) noexcept;

public:
  [[nodiscard]] SymRef sym_add_undef(std::string_view name,
                                     SymBinding binding) noexcept {
    return sym_add(name, binding, STT_NOTYPE);
  }

  [[nodiscard]] SymRef sym_predef_func(std::string_view name,
                                       SymBinding binding) noexcept {
    return sym_add(name, binding, STT_FUNC);
  }

  [[nodiscard]] SymRef sym_predef_data(std::string_view name,
                                       SymBinding binding) noexcept {
    return sym_add(name, binding, STT_OBJECT);
  }

  [[nodiscard]] SymRef sym_predef_tls(std::string_view name,
                                      SymBinding binding) noexcept {
    return sym_add(name, binding, STT_TLS);
  }

  void sym_def_predef_data(SecRef sec,
                           SymRef sym,
                           std::span<const u8> data,
                           u32 align,
                           u32 *off) noexcept;

  [[nodiscard]] SymRef sym_def_data(SecRef sec,
                                    std::string_view name,
                                    std::span<const u8> data,
                                    u32 align,
                                    SymBinding binding,
                                    u32 *off = nullptr) {
    SymRef sym = sym_predef_data(name, binding);
    sym_def_predef_data(sec, sym, data, align, off);
    return sym;
  }

  void sym_def_predef_zero(SecRef sec_ref,
                           SymRef sym_ref,
                           u32 size,
                           u32 align,
                           u32 *off = nullptr) noexcept;

  void sym_def(SymRef sym_ref, SecRef sec_ref, u64 pos, u64 size) noexcept {
    Elf64_Sym *sym = sym_ptr(sym_ref);
    assert(sym->st_shndx == SHN_UNDEF && "cannot redefined symbol");
    sym->st_shndx = static_cast<Elf64_Section>(sec_ref);
    sym->st_value = pos;
    sym->st_size = size;
    // TODO: handle fixups?
  }

  /// Forcefully set value of symbol, doesn't change section.
  void sym_set_value(SymRef sym, u64 value) noexcept {
    sym_ptr(sym)->st_value = value;
  }

  const char *sym_name(SymRef sym) const noexcept {
    return strtab.data() + sym_ptr(sym)->st_name;
  }

  Label label_create() noexcept {
    const Label label = static_cast<Label>(temp_symbols.size());
    temp_symbols.push_back(TempSymbolInfo{INVALID_SEC_REF, {.fixup_idx = ~0u}});
    return label;
  }

  // TODO: return pair of section, offset
  u32 label_is_pending(Label label) const noexcept {
    const auto &info = temp_symbols[static_cast<u32>(label)];
    return info.section == INVALID_SEC_REF;
  }

  // TODO: return pair of section, offset
  u32 label_offset(Label label) const noexcept {
    assert(!label_is_pending(label));
    const auto &info = temp_symbols[static_cast<u32>(label)];
    return info.off;
  }

protected:
  [[nodiscard]] static bool sym_is_local(const SymRef sym) noexcept {
    return (sym.id() & 0x8000'0000) == 0;
  }

  [[nodiscard]] static u32 sym_idx(const SymRef sym) noexcept {
    return sym.id() & ~0x8000'0000;
  }

  [[nodiscard]] Elf64_Sym *sym_ptr(const SymRef sym) noexcept {
    if (sym_is_local(sym)) {
      return &local_symbols[sym_idx(sym)];
    } else {
      return &global_symbols[sym_idx(sym)];
    }
  }

  [[nodiscard]] const Elf64_Sym *sym_ptr(const SymRef sym) const noexcept {
    if (sym_is_local(sym)) {
      return &local_symbols[sym_idx(sym)];
    } else {
      return &global_symbols[sym_idx(sym)];
    }
  }

  // Relocations

public:
  void reloc_sec(
      SecRef sec, SymRef sym, u32 type, u64 offset, i64 addend) noexcept;

  void reloc_pc32(SecRef sec, SymRef sym, u64 offset, i64 addend) noexcept {
    reloc_sec(sec, sym, target_info.reloc_pc32, offset, addend);
  }

  void reloc_abs(SecRef sec, SymRef sym, u64 offset, i64 addend) noexcept {
    reloc_sec(sec, sym, target_info.reloc_abs64, offset, addend);
  }

  void reloc_sec(SecRef sec, Label label, u8 kind, u32 offset) noexcept;

  // Unwind and exception info

  static constexpr u32 write_eh_inst(u8 *dst, u8 opcode, u64 arg) noexcept {
    if (opcode & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) {
      assert((arg & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) == 0);
      *dst = opcode | arg;
      return 1;
    }
    *dst++ = opcode;
    return 1 + util::uleb_write(dst, arg);
  }

  static constexpr u32
      write_eh_inst(u8 *dst, u8 opcode, u64 arg1, u64 arg2) noexcept {
    u8 *base = dst;
    dst += write_eh_inst(dst, opcode, arg1);
    dst += util::uleb_write(dst, arg2);
    return dst - base;
  }

  void eh_align_frame() noexcept;
  void eh_write_inst(u8 opcode, u64 arg) noexcept;
  void eh_write_inst(u8 opcode, u64 first_arg, u64 second_arg) noexcept;
  void eh_write_uleb(u64 value) noexcept;
  void eh_write_uleb(std::vector<u8> &dst, u64 value) noexcept;
  void eh_write_sleb(std::vector<u8> &dst, i64 value) noexcept;

  void eh_init_cie(SymRef personality_func_addr = SymRef()) noexcept;
  u32 eh_begin_fde() noexcept;
  void eh_end_fde(u32 fde_start, SymRef func) noexcept;
  void except_encode_func() noexcept;

  /// add an entry to the call-site table
  /// must be called in strictly increasing order wrt text_off
  void except_add_call_site(u32 text_off,
                            u32 len,
                            u32 landing_pad_id,
                            bool is_cleanup) noexcept;

  /// Add a cleanup action to the action table
  /// *MUST* be the last one
  void except_add_cleanup_action() noexcept;

  /// add an action to the action table
  /// An invalid SymRef signals a catch(...)
  void except_add_action(bool first_action, SymRef type_sym) noexcept;

  void except_add_empty_spec_action(bool first_action) noexcept;

  u32 except_type_idx_for_sym(SymRef sym) noexcept;

  // Output file generation

  std::vector<u8> build_object_file() noexcept;
};

/// AssemblerElf contains the architecture-independent logic to emit
/// ELF object files (currently linux-specific) which is then extended by
/// AssemblerElfX64 or AssemblerElfA64
template <typename Derived>
struct AssemblerElf : public AssemblerElfBase {
  /// The current write pointer for the text section
  SecRef current_section = INVALID_SEC_REF;
  u8 *text_begin = nullptr;
  u8 *text_write_ptr = nullptr;
  u8 *text_reserve_end = nullptr;

#ifdef TPDE_ASSERTS
  bool currently_in_func = false;
#endif

  explicit AssemblerElf(const bool generating_object)
      : AssemblerElfBase(Derived::TARGET_INFO, generating_object) {
    static_assert(std::is_base_of_v<AssemblerElf, Derived>);
    current_section = secref_text;
  }

  Derived *derived() noexcept { return static_cast<Derived *>(this); }

  void start_func(SymRef func, SymRef personality_func_addr) noexcept;

  void end_func() noexcept;

  /// Align the text write pointer
  void text_align(u64 align) noexcept { derived()->text_align_impl(align); }

  void text_align_impl(u64 align) noexcept;

  /// \returns The current used space in the text section
  [[nodiscard]] u32 text_cur_off() const noexcept {
    return static_cast<u32>(text_write_ptr - text_begin);
  }

  u8 *text_ptr(u32 off) noexcept { return text_begin + off; }

  /// Make sure that text_write_ptr can be safely incremented by size
  void text_ensure_space(u32 size) noexcept {
    if (text_reserve_end - text_write_ptr < size) [[unlikely]] {
      derived()->text_more_space(size);
    }
  }

  void text_more_space(u32 size) noexcept;

  void reset() noexcept;

  void reloc_text(SymRef sym, u32 type, u64 offset, i64 addend = 0) noexcept {
    reloc_sec(current_section, sym, type, offset, addend);
  }

  void label_place(Label label, SecRef sec, u32 off) noexcept;

  void label_place(Label label) noexcept {
    derived()->label_place(label, current_section, text_cur_off());
  }

  std::vector<u8> build_object_file() {
    // Truncate text section to actually needed size
    DataSection &sec_text = get_section(current_section);
    sec_text.data.resize(text_cur_off());
    text_reserve_end = text_ptr(text_cur_off());

    return AssemblerElfBase::build_object_file();
  }

  // TODO(ts): func to map into memory

#ifdef TPDE_ASSERTS
  [[nodiscard]] bool func_was_ended() const noexcept {
    return !currently_in_func;
  }
#endif
};

template <typename Derived>
void AssemblerElf<Derived>::start_func(
    const SymRef func, const SymRef personality_func_addr) noexcept {
  cur_func = func;

  text_align(16);
  auto *elf_sym = sym_ptr(func);
  elf_sym->st_value = text_cur_off();
  elf_sym->st_shndx = static_cast<Elf64_Section>(current_section);

  if (personality_func_addr != cur_personality_func_addr) {
    assert(generating_object); // the jit model does not yet output relocations
    // need to start a new CIE
    eh_init_cie(personality_func_addr);

    cur_personality_func_addr = personality_func_addr;
  }

  except_call_site_table.clear();
  except_action_table.clear();
  except_type_info_table.clear();
  except_spec_table.clear();
  except_action_table.resize(2); // cleanup entry

#ifdef TPDE_ASSERTS
  currently_in_func = true;
#endif
}

template <typename Derived>
void AssemblerElf<Derived>::end_func() noexcept {
  auto *elf_sym = sym_ptr(cur_func);
  elf_sym->st_size = text_cur_off() - elf_sym->st_value;

#ifdef TPDE_ASSERTS
  currently_in_func = false;
#endif
}

template <typename Derived>
void AssemblerElf<Derived>::text_align_impl(u64 align) noexcept {
  text_ensure_space(align);
  text_write_ptr = reinterpret_cast<u8 *>(
      util::align_up(reinterpret_cast<uintptr_t>(text_write_ptr), align));
}

template <typename Derived>
void AssemblerElf<Derived>::text_more_space(u32 size) noexcept {
  size = util::align_up(size, 16 * 1024);
  const size_t off = text_write_ptr - text_begin;
  DataSection &sec = get_section(current_section);
  sec.data.resize(sec.data.size() + size);

  text_begin = sec.data.data();
  text_write_ptr = text_ptr(off);
  text_reserve_end = text_ptr(sec.data.size());
}

template <typename Derived>
void AssemblerElf<Derived>::label_place(Label label,
                                        SecRef sec,
                                        u32 offset) noexcept {
  assert(label_is_pending(label));
  TempSymbolInfo &info = temp_symbols[static_cast<u32>(label)];
  u32 fixup_idx = info.fixup_idx;
  info.section = sec;
  info.off = offset;

  while (fixup_idx != ~0u) {
    TempSymbolFixup &fixup = temp_symbol_fixups[fixup_idx];
    derived()->handle_fixup(info, fixup);
    auto next = fixup.next_list_entry;
    fixup.next_list_entry = next_free_tsfixup;
    next_free_tsfixup = fixup_idx;
    fixup_idx = next;
  }
}

template <typename Derived>
void AssemblerElf<Derived>::reset() noexcept {
  AssemblerElfBase::reset();

  current_section = secref_text;
  text_begin = text_write_ptr = text_reserve_end = nullptr;
}

} // namespace tpde
