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

#if defined(__unix__)
  #include <sys/mman.h>
  #ifdef __x86_64__
    #include <fadec-enc2.h>
  #endif
  #ifdef __aarch64__
    #include <disarm64.h>
  #endif

extern "C" void __register_frame(void *);
extern "C" void __deregister_frame(void *);

namespace tpde {
  #ifdef __x86_64__
constexpr size_t PAGE_SIZE = 4096;
  #elif defined(__aarch64__)
// TODO(ts): I think on arm64 we need to ask the kernel for the current page
// size
constexpr size_t PAGE_SIZE = 16384;
  #endif
} // namespace tpde
#else
  #error "unsupported architecture/os combo"
#endif

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

template <typename T>
concept SymbolResolver = requires(T a) {
  {
    a.resolve_symbol(std::declval<std::string_view>())
  } -> std::same_as<void *>;
};

struct AssemblerElfBase {
  template <class Derived>
  friend struct AssemblerElf;

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

  enum class SymRef : u32 {
  };
  static constexpr SymRef INVALID_SYM_REF = static_cast<SymRef>(0u);

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
  u32 sec_bss_size = 0;

protected:
  SecRef secref_text = INVALID_SEC_REF;
  SecRef secref_rodata = INVALID_SEC_REF;
  SecRef secref_relro = INVALID_SEC_REF;
  SecRef secref_data = INVALID_SEC_REF;

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
  SymRef cur_personality_func_addr = INVALID_SYM_REF;
  u32 eh_cur_cie_off = 0u;
  u32 eh_first_fde_off = 0;

  /// Is the objective(heh) to generate an object file or to map into memory?
  bool generating_object;
  /// The current function
  SymRef cur_func = INVALID_SYM_REF;

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
  SecRef get_structor_section(bool init) noexcept;
  SecRef get_eh_frame_section() const noexcept { return secref_eh_frame; }

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

  void sym_def_predef_bss(SymRef sym_ref,
                          u32 size,
                          u32 align,
                          u32 *off = nullptr) noexcept;

  [[nodiscard]] SymRef sym_def_bss(std::string_view name,
                                   u32 size,
                                   u32 align,
                                   SymBinding binding,
                                   u32 *off = nullptr) noexcept {
    SymRef sym = sym_predef_data(name, binding);
    sym_def_predef_bss(sym, size, align, off);
    return sym;
  }

  /// Forcefully set value of symbol, doesn't change section.
  void sym_set_value(SymRef sym, u64 value) noexcept {
    sym_ptr(sym)->st_value = value;
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
    return (static_cast<u32>(sym) & 0x8000'0000) == 0;
  }

  [[nodiscard]] static u32 sym_idx(const SymRef sym) noexcept {
    return static_cast<u32>(sym) & ~0x8000'0000;
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

  void eh_init_cie(SymRef personality_func_addr = INVALID_SYM_REF) noexcept;
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
  /// INVALID_SYM_REF signals a catch(...)
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

  template <SymbolResolver SymbolResolver>
  struct Mapper {
    u8 *mapped_addr = nullptr;
    size_t mapped_size = 0;
    u32 registered_frame_off = 0;

    u32 local_sym_count = 0;
    util::SmallVector<void *, 64> sym_addrs;

    Mapper() = default;
    ~Mapper();

    bool map(Derived *, SymbolResolver *, std::span<const SymRef> func_syms);

    void *get_sym_addr(SymRef sym) {
      auto idx = AssemblerElf::sym_idx(sym);
      if (!AssemblerElf::sym_is_local(sym)) {
        idx += local_sym_count;
      }
      assert(idx < sym_addrs.size());
      return sym_addrs[idx];
    }
  };

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

template <typename Derived>
template <SymbolResolver SymbolResolver>
AssemblerElf<Derived>::Mapper<SymbolResolver>::~Mapper() {
  if (!mapped_addr) {
    return;
  }

  if (registered_frame_off) {
    __deregister_frame(mapped_addr + registered_frame_off);
  }

  munmap(mapped_addr, mapped_size);
  registered_frame_off = 0;
  mapped_addr = nullptr;
  mapped_size = 0;
}

#if 0
template <typename Derived>
template <SymbolResolver SymbolResolver>
bool AssemblerElf<Derived>::Mapper<SymbolResolver>::map(
    Derived *ad, SymbolResolver *resolver, std::span<const SymRef> func_syms) {
  assert(!mapped_addr);

  AssemblerElf<Derived> *a = static_cast<AssemblerElf<Derived> *>(ad);

  // if (!a->sec_fini_array.data.empty() || !a->sec_init_array.data.empty()) {
  //   assert(0);
  //   return false;
  // }

  // zero-terminate eh_frame
  // TODO(ts): this should probably go somewhere else
  if (!a->sec_eh_frame.data.empty()) {
    a->sec_eh_frame.data.resize(a->sec_eh_frame.data.size() + 4);
  }

  std::vector<u32> plt_or_got_offs{};
  plt_or_got_offs.resize(a->local_symbols.size() + a->global_symbols.size());

  u32 num_plt_entries = func_syms.size(), num_got_entries = 0;
  // bad approximation but otherwise we would need a hashtable i think
  for (const auto &r : a->sec_text.relocs) {
    const auto ty = ELF64_R_TYPE(r.r_info);
    if (ty == R_X86_64_GOTPCREL || ty == R_AARCH64_ADR_GOT_PAGE) {
      ++num_got_entries;
    }
  }

  #ifdef __x86_64__
  constexpr size_t PLT_ENTRY_SIZE = 16;
  #elif defined(__aarch64__)
  constexpr size_t PLT_ENTRY_SIZE = 16;
  #endif

  const u32 plt_size = num_plt_entries * PLT_ENTRY_SIZE;
  const u32 got_size = num_got_entries * 8;

  std::array<u32, elf::sec_count() + 1> sec_offs = {};

  const auto add_sec = [&](u32 idx, const DataSection &sec) {
    sec_offs[idx] = util::align_up(sec.data.size(), PAGE_SIZE);
  };

  add_sec(elf::sec_idx(".eh_frame"), a->sec_eh_frame);
  add_sec(elf::sec_idx(".data"), a->sec_data);
  add_sec(elf::sec_idx(".rodata"), a->sec_rodata);
  add_sec(elf::sec_idx(".gcc_except_table"), a->sec_except_table);

  sec_offs[elf::sec_idx(".bss")] = util::align_up(a->sec_bss_size, PAGE_SIZE);
  // TODO(ts): only copy the used parts of .text and not the reserved parts?
  sec_offs[elf::sec_idx(".text")] = util::align_up(
      util::align_up(a->sec_text.data.size(), 16) + plt_size, PAGE_SIZE);
  sec_offs[elf::sec_idx(".data.rel.ro")] = util::align_up(
      util::align_up(a->sec_relrodata.data.size(), 16) + got_size, PAGE_SIZE);

  {
    u32 off = 0;
    for (auto &e : sec_offs) {
      const auto size = e;
      e = off;
      off += size;
    }
  }

  mapped_addr = static_cast<u8 *>(mmap(nullptr,
                                       sec_offs.back(),
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS,
                                       -1,
                                       0));
  if (mapped_addr == MAP_FAILED || !mapped_addr) {
    return false;
  }
  mapped_size = sec_offs.back();

  const auto copy_sec = [&](u32 idx, const DataSection &sec) {
    std::memcpy(mapped_addr + sec_offs[idx], sec.data.data(), sec.data.size());
  };
  copy_sec(elf::sec_idx(".text"), a->sec_text);
  copy_sec(elf::sec_idx(".eh_frame"), a->sec_eh_frame);
  copy_sec(elf::sec_idx(".data"), a->sec_data);
  copy_sec(elf::sec_idx(".rodata"), a->sec_rodata);
  copy_sec(elf::sec_idx(".data.rel.ro"), a->sec_relrodata);
  copy_sec(elf::sec_idx(".gcc_except_table"), a->sec_except_table);

  const auto local_sym_count = a->local_symbols.size();
  this->local_sym_count = local_sym_count;
  sym_addrs.clear();
  sym_addrs.resize(a->local_symbols.size() + a->global_symbols.size());

  const auto sym_addr = [&](SymRef sym_ref) -> void * {
    auto idx = AssemblerElf::sym_idx(sym_ref);
    if (!AssemblerElf::sym_is_local(sym_ref)) {
      idx += local_sym_count;
    }
    if (sym_addrs[idx]) {
      return sym_addrs[idx];
    }

    const Elf64_Sym *sym = a->sym_ptr(sym_ref);
    if (sym->st_shndx == SHN_UNDEF) {
      auto name = std::string_view{a->strtab.data() + sym->st_name};
      const auto addr = resolver->resolve_symbol(name);
      assert(addr);
      sym_addrs[idx] = addr;
      return addr;
    }

    auto off = sec_offs[sym->st_shndx];
    off += sym->st_value;
    auto *addr = mapped_addr + off;
    sym_addrs[idx] = addr;
    return addr;
  };

  auto cur_plt_off = sec_offs[elf::sec_idx(".text")] +
                     util::align_up(a->sec_text.data.size(), 16);
  auto cur_got_off = sec_offs[elf::sec_idx(".data.rel.ro")] +
                     util::align_up(a->sec_relrodata.data.size(), 16);
  const auto fix_relocs = [&](u32 idx, const DataSection &sec) {
    for (auto &reloc : sec.relocs) {
      const auto reloc_ty = ELF64_R_TYPE(reloc.r_info);
      const auto sym = static_cast<SymRef>(ELF64_R_SYM(reloc.r_info));
      const auto s_addr = sym_addr(sym);

      auto *r_addr = mapped_addr + sec_offs[idx] + reloc.r_offset;
      switch (reloc_ty) {
  #ifdef __x86_64__
      case R_X86_64_64: {
        *reinterpret_cast<u64 *>(r_addr) =
            reinterpret_cast<i64>(s_addr) + reloc.r_addend;
        break;
      }
      case R_X86_64_PC32: {
        auto P = reinterpret_cast<uintptr_t>(r_addr);
        auto val = reinterpret_cast<uintptr_t>(s_addr) + reloc.r_addend - P;
        if (val >= 0xFFFF'FFFF && val <= 0xFFFF'FFFF'8000'0000) {
          assert(0);
          return false;
        }
        *reinterpret_cast<u32 *>(r_addr) = val;
        break;
      }
      case R_X86_64_PLT32: {
        assert(reloc.r_addend == -4);
        const u64 orig_disp =
            reinterpret_cast<u64>(s_addr) - (reinterpret_cast<u64>(r_addr) + 4);
        if (orig_disp <= 0x7FFF'FFFF || orig_disp >= 0xFFFF'FFFF'8000'0000) {
          // can just encode it directly in the call
          *reinterpret_cast<i32 *>(r_addr) = orig_disp;
          break;
        }

        auto idx = AssemblerElf::sym_idx(sym);
        if (!AssemblerElf::sym_is_local(sym)) {
          idx += local_sym_count;
        }
        auto &plt_off = plt_or_got_offs[idx];
        u8 *code_ptr = nullptr;
        if (plt_off != 0) {
          code_ptr = mapped_addr + plt_off;
        } else {
          plt_off = cur_plt_off;
          code_ptr = mapped_addr + cur_plt_off;

          cur_plt_off += 16;
          u64 jmp_addr = reinterpret_cast<u64>(code_ptr) + 5;
          u64 disp = reinterpret_cast<u64>(s_addr) - jmp_addr;
          if (disp <= 0x7FFF'FFFF || disp >= 0xFFFF'FFFF'8000'0000) {
            // can just use a jump
            fe64_JMP(code_ptr, FE_JMPL, s_addr);
          } else {
            auto off = fe64_MOV64ri(
                code_ptr, 0, FE_R11, reinterpret_cast<i64>(s_addr));
            fe64_JMPr(code_ptr + off, 0, FE_R11);
          }
        }

        *reinterpret_cast<i32 *>(r_addr) = reinterpret_cast<i64>(code_ptr) +
                                           reloc.r_addend -
                                           reinterpret_cast<u64>(r_addr);
        break;
      }
      case R_X86_64_GOTPCREL: {
        auto idx = AssemblerElf::sym_idx(sym);
        if (!AssemblerElf::sym_is_local(sym)) {
          idx += local_sym_count;
        }
        auto &got_off = plt_or_got_offs[idx];

        u8 *got_entry_ptr = nullptr;
        if (got_off != 0) {
          got_entry_ptr = mapped_addr + got_off;
        } else {
          got_off = cur_got_off;
          got_entry_ptr = mapped_addr + cur_got_off;
          cur_got_off += 8;
          *reinterpret_cast<u64 *>(got_entry_ptr) =
              reinterpret_cast<u64>(s_addr);
        }

        *reinterpret_cast<i32 *>(r_addr) =
            reinterpret_cast<i64>(got_entry_ptr) + reloc.r_addend -
            reinterpret_cast<u64>(r_addr);
        break;
      }
  #elif defined(__aarch64__)
      case R_AARCH64_PREL32: {
        auto P = reinterpret_cast<uintptr_t>(r_addr);
        auto val = reinterpret_cast<uintptr_t>(s_addr) + reloc.r_addend - P;
        if (val >= 0xFFFF'FFFF && val <= 0xFFFF'FFFF'8000'0000) {
          assert(0);
          return false;
        }
        *reinterpret_cast<u32 *>(r_addr) = val;
        break;
      }
      case R_AARCH64_CALL26: {
        auto P = reinterpret_cast<u64>(r_addr);
        i64 off = reinterpret_cast<i64>(s_addr) + reloc.r_addend - P;
        if ((off << 2) == ((off << 38) >> 36)) {
          *reinterpret_cast<u32 *>(r_addr) = de64_BL(off >> 2);
          break;
        }

        // need to create a PLT entry
        auto idx = AssemblerElf::sym_idx(sym);
        if (!AssemblerElf::sym_is_local(sym)) {
          idx += local_sym_count;
        }
        auto &plt_off = plt_or_got_offs[idx];
        u32 *code_ptr = nullptr;
        if (plt_off != 0) {
          code_ptr = reinterpret_cast<u32 *>(mapped_addr + plt_off);
        } else {
          plt_off = cur_plt_off;
          code_ptr = reinterpret_cast<u32 *>(mapped_addr + cur_plt_off);

          cur_plt_off += 16;

          *code_ptr++ = de64_LDRx_pcrel(DA_GP(16), 8 / 4);
          *code_ptr++ = de64_BR(DA_GP(16));
          *reinterpret_cast<u64 *>(code_ptr) = reinterpret_cast<u64>(s_addr);
        }

        off = reinterpret_cast<i64>(code_ptr) + reloc.r_addend -
              reinterpret_cast<u64>(r_addr);
        assert((off << 2) == ((off << 38) >> 36));
        *reinterpret_cast<u32 *>(r_addr) = de64_BL((off << 38) >> 38);
        break;
      }
  #endif
      default:
        TPDE_LOG_ERR("Encountered unknown relocation: {}", reloc_ty);
        assert(0);
        return false;
      }
    }
    return true;
  };

  auto succ = true;
  succ = succ && fix_relocs(elf::sec_idx(".text"), a->sec_text);
  // TODO(ts): change eh frame generation when told to not generate an object?
  succ = succ && fix_relocs(elf::sec_idx(".eh_frame"), a->sec_eh_frame);
  succ = succ && fix_relocs(elf::sec_idx(".data"), a->sec_data);
  succ = succ && fix_relocs(elf::sec_idx(".rodata"), a->sec_rodata);
  succ = succ && fix_relocs(elf::sec_idx(".data.rel.ro"), a->sec_relrodata);
  succ = succ &&
         fix_relocs(elf::sec_idx(".gcc_except_table"), a->sec_except_table);
  if (!succ) {
    return false;
  }

  // make sure all function symbols are resolved
  for (auto func : func_syms) {
    sym_addr(func);
  }

  const auto sec_protect = [&](u32 idx, int protect) {
    return mprotect(mapped_addr + sec_offs[idx],
                    sec_offs[idx + 1] - sec_offs[idx],
                    protect) == 0;
  };

  if (!sec_protect(elf::sec_idx(".text"), PROT_READ | PROT_EXEC)) {
    return false;
  }

  if (!sec_protect(elf::sec_idx(".rodata"), PROT_READ)) {
    return false;
  }

  if (!sec_protect(elf::sec_idx(".data.rel.ro"), PROT_READ)) {
    return false;
  }

  registered_frame_off =
      sec_offs[elf::sec_idx(".eh_frame")] + a->eh_first_fde_off;
  __register_frame(mapped_addr + registered_frame_off);

  return true;
}
#endif
} // namespace tpde
