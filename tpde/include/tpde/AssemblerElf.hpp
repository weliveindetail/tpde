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
constexpr u8 DW_CFA_offset = 0x80;

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

/// AssemblerElf contains the architecture-independent logic to emit
/// ELF object files (currently linux-specific) which is then extended by
/// AssemblerElfX64 or AssemblerElfA64
template <typename Derived>
struct AssemblerElf {
  enum class SymRef : u32 {
  };
  static constexpr SymRef INVALID_SYM_REF = static_cast<SymRef>(~0u);

  enum class SecRef : u32 {
  };
  static constexpr SecRef INVALID_SEC_REF = static_cast<SecRef>(~0u);

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

  util::SmallVector<DataSection, 16> sections;

  std::vector<Elf64_Sym> global_symbols, local_symbols;

  std::vector<char> strtab;
  u32 sec_bss_size = 0;

  SecRef secref_text = INVALID_SEC_REF;
  SecRef secref_rodata = INVALID_SEC_REF;
  SecRef secref_relro = INVALID_SEC_REF;
  SecRef secref_data = INVALID_SEC_REF;

  SecRef secref_init_array = INVALID_SEC_REF;
  SecRef secref_fini_array = INVALID_SEC_REF;

  /// Unwind Info
  DataSection sec_eh_frame;
  SecRef secref_except_table = INVALID_SEC_REF;

  struct ExceptCallSiteInfo {
    /// Start offset *in section* (not inside function)
    u64 start;
    u64 len;
    u32 pad_label_or_off;
    u32 action_entry;
  };

  /// Exception Handling temporary storage
  /// Call Sites for current function
  std::vector<ExceptCallSiteInfo> except_call_site_table;
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

  /// The current write pointer for the text section
  SecRef current_section = INVALID_SEC_REF;
  u8 *text_begin = nullptr;
  u8 *text_write_ptr = nullptr;
  u8 *text_reserve_end = nullptr;

  /// Is the objective(heh) to generate an object file or to map into memory?
  bool generating_object;
  /// The current function
  SymRef cur_func = INVALID_SYM_REF;

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
      : generating_object(generating_object) {
    static_assert(std::is_base_of_v<AssemblerElf, Derived>);
    strtab.push_back('\0');

    local_symbols.resize(1); // First symbol must be null.
    init_sections();
    eh_init_cie();
  }

  Derived *derived() noexcept { return static_cast<Derived *>(this); }

  void start_func(SymRef func, SymRef personality_func_addr) noexcept;

protected:
  void end_func() noexcept;
  void init_sections() noexcept;

  DataSection &get_or_create_section(SecRef &ref,
                                     unsigned rela_name,
                                     unsigned type,
                                     unsigned flags,
                                     unsigned align,
                                     bool with_rela = true) noexcept;

public:
  DataSection &get_section(SecRef ref) noexcept {
    assert(ref != INVALID_SEC_REF);
    return sections[static_cast<u32>(ref)];
  }

  SecRef get_data_section(bool rodata, bool relro = false) noexcept;
  SecRef get_structor_section(bool init) noexcept;

  [[nodiscard]] SymRef sym_add_undef(std::string_view name,
                                     bool local = false,
                                     bool weak = false);
  void sym_copy(SymRef dst, SymRef src, bool local, bool weak) noexcept;

  [[nodiscard]] SymRef
      sym_predef_func(std::string_view name, bool local, bool weak);

  [[nodiscard]] SymRef
      sym_predef_data(std::string_view name, bool local, bool weak) noexcept;

  void sym_def_predef_data(SecRef sec,
                           SymRef sym,
                           std::span<const u8> data,
                           u32 align,
                           u32 *off) noexcept;

  [[nodiscard]] SymRef sym_def_data(SecRef sec,
                                    std::string_view name,
                                    std::span<const u8> data,
                                    u32 align,
                                    bool local,
                                    bool weak,
                                    u32 *off = nullptr) {
    SymRef sym = sym_predef_data(name, local, weak);
    sym_def_predef_data(sec, sym, data, align, off);
    return sym;
  }

  [[nodiscard]] SymRef sym_def_bss(std::string_view name,
                                   u32 size,
                                   u32 align,
                                   bool local,
                                   bool weak,
                                   u32 *off = nullptr) noexcept;

  void sym_def_predef_bss(SymRef sym_ref,
                          u32 size,
                          u32 align,
                          u32 *off = nullptr) noexcept;

  /// Align the text write pointer
  void text_align(u64 align) noexcept { derived()->text_align_impl(align); }

  void text_align_impl(u64 align) noexcept;

  /// \returns The current used space in the text section
  [[nodiscard]] u32 text_cur_off() const noexcept {
    return static_cast<u32>(text_write_ptr - text_begin);
  }

  u8 *text_ptr(u32 off) noexcept { return text_begin + off; }

  /// Make sure that text_write_ptr can be safely incremented by size
  void text_ensure_space(u32 size) noexcept;

  void reset() noexcept;

  std::vector<u8> build_object_file();

  // TODO(ts): func to map into memory

  void reloc_text(SymRef sym, u32 type, u64 offset, i64 addend) noexcept;

  void reloc_sec(
      DataSection &sec, SymRef sym, u32 type, u64 offset, i64 addend) noexcept;

  void reloc_sec(
      SecRef sec, SymRef sym, u32 type, u64 offset, i64 addend) noexcept {
    reloc_sec(get_section(sec), sym, type, offset, addend);
  }

  void eh_align_frame() noexcept;
  void eh_write_inst(u8 opcode, u64 arg) noexcept;
  void eh_write_inst(u8 opcode, u64 first_arg, u64 second_arg) noexcept;
  void eh_write_uleb(u64 value) noexcept;
  void eh_write_uleb(std::vector<u8> &dst, u64 value) noexcept;
  void eh_write_sleb(std::vector<u8> &dst, i64 value) noexcept;
  u32 eh_uleb_len(u64 value) noexcept;

  void eh_init_cie(SymRef personality_func_addr = INVALID_SYM_REF) noexcept;
  u32 eh_write_fde_start() noexcept;
  void eh_write_fde_len(u32 fde_off) noexcept;
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

#ifdef TPDE_ASSERTS
  [[nodiscard]] bool func_was_ended() const noexcept {
    return !currently_in_func;
  }
#endif

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
typename AssemblerElf<Derived>::SymRef AssemblerElf<Derived>::sym_add_undef(
    const std::string_view name, const bool local, const bool weak) {
  size_t str_off = 0;
  if (!name.empty()) {
    str_off = strtab.size();
    strtab.insert(strtab.end(), name.begin(), name.end());
    strtab.emplace_back('\0');
  }

  u8 info;
  if (local) {
    assert(!weak);
    info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);
  } else if (weak) {
    info = ELF64_ST_INFO(STB_WEAK, STT_NOTYPE);
  } else {
    info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
  }

  auto sym = Elf64_Sym{.st_name = static_cast<Elf64_Word>(str_off),
                       .st_info = info,
                       .st_other = STV_DEFAULT,
                       .st_shndx = SHN_UNDEF,
                       .st_value = 0,
                       .st_size = 0};

  if (local) {
    local_symbols.push_back(sym);
    assert(local_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>(local_symbols.size() - 1);
  } else {
    global_symbols.push_back(sym);
    assert(global_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>((global_symbols.size() - 1) | 0x8000'0000);
  }
}

template <typename Derived>
void AssemblerElf<Derived>::sym_copy(const SymRef dst,
                                     const SymRef src,
                                     const bool local,
                                     const bool weak) noexcept {
  Elf64_Sym *src_ptr = sym_ptr(src), *dst_ptr = sym_ptr(dst);

  dst_ptr->st_shndx = src_ptr->st_shndx;
  dst_ptr->st_size = src_ptr->st_size;
  dst_ptr->st_value = src_ptr->st_value;

  const auto type = ELF64_ST_TYPE(src_ptr->st_info);
  u8 info;
  if (local) {
    assert(!weak);
    info = ELF64_ST_INFO(STB_LOCAL, type);
  } else if (weak) {
    info = ELF64_ST_INFO(STB_WEAK, type);
  } else {
    info = ELF64_ST_INFO(STB_GLOBAL, type);
  }
  dst_ptr->st_info = info;
}

template <typename Derived>
typename AssemblerElf<Derived>::SymRef AssemblerElf<Derived>::sym_predef_func(
    const std::string_view name, const bool local, const bool weak) {
  assert(name != "__gxx_personality_v0");

  // TODO(ts): can/should we allow empty names?
  assert(!name.empty());

  const auto strOff = strtab.size();
  strtab.insert(strtab.end(), name.begin(), name.end());
  strtab.emplace_back('\0');

  u8 info;
  if (local) {
    assert(!weak);
    info = ELF64_ST_INFO(STB_LOCAL, STT_FUNC);
  } else if (weak) {
    info = ELF64_ST_INFO(STB_WEAK, STT_FUNC);
  } else {
    info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
  }

  const auto sym = Elf64_Sym{.st_name = static_cast<Elf64_Word>(strOff),
                             .st_info = info,
                             .st_other = STV_DEFAULT};

  if (local) {
    local_symbols.push_back(sym);
    assert(local_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>(local_symbols.size() - 1);
  } else {
    global_symbols.push_back(sym);
    assert(global_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>((global_symbols.size() - 1) | 0x8000'0000);
  }
}

template <typename Derived>
typename AssemblerElf<Derived>::SymRef AssemblerElf<Derived>::sym_predef_data(
    const std::string_view name, bool const local, const bool weak) noexcept {
  size_t str_off = 0;
  if (!name.empty()) {
    str_off = strtab.size();
    strtab.insert(strtab.end(), name.begin(), name.end());
    strtab.emplace_back('\0');
  }

  u8 info;
  if (local) {
    assert(!weak);
    info = ELF64_ST_INFO(STB_LOCAL, STT_OBJECT);
  } else if (weak) {
    info = ELF64_ST_INFO(STB_WEAK, STT_OBJECT);
  } else {
    info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
  }

  auto sym = Elf64_Sym{.st_name = static_cast<Elf64_Word>(str_off),
                       .st_info = info,
                       .st_other = STV_DEFAULT,
                       .st_shndx = static_cast<Elf64_Section>(0),
                       .st_value = 0,
                       .st_size = 0};

  if (local) {
    local_symbols.push_back(sym);
    assert(local_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>(local_symbols.size() - 1);
  } else {
    global_symbols.push_back(sym);
    assert(global_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>((global_symbols.size() - 1) | 0x8000'0000);
  }
}

// TODO(ts): maybe just outsource this to a helper func that can live in a cpp
// file?
namespace elf {
// TODO(ts): this is linux-specific, no?
constexpr static std::span<const char> SECTION_NAMES = {
    "\0" // first section is the null-section
    ".note.GNU-stack\0"
    ".eh_frame\0"
    ".rela.eh_frame\0"
    ".symtab\0"
    ".strtab\0"
    ".shstrtab\0"
    ".bss\0"};

// TODO(ts): this is linux-specific, no?
constexpr static std::span<const char> SHSTRTAB = {
    "\0" // first section is the null-section
    ".note.GNU-stack\0"
    ".rela.eh_frame\0"
    ".symtab\0"
    ".strtab\0"
    ".shstrtab\0"
    ".bss\0"
    ".rela.rodata\0"
    ".rela.text\0"
    ".rela.data.rel.ro\0"
    ".rela.data\0"
    ".rela.gcc_except_table\0"
    ".rela.init_array\0"
    ".rela.fini_array\0"};

static void fail_constexpr_compile(const char *) {
  assert(0);
  exit(1);
};

consteval static u32 sec_idx(const std::string_view name) {
  // skip the first null string
  const char *data = SECTION_NAMES.data() + 1;
  u32 idx = 1;
  auto sec_name = std::string_view{data};
  while (!sec_name.empty()) {
    if (sec_name == name) {
      return idx;
    }

    ++idx;
    data += sec_name.size() + 1;
    sec_name = std::string_view{data};
  }

  fail_constexpr_compile("unknown section name");
  return ~0u;
}

consteval static u32 sec_off(const std::string_view name) {
  // skip the first null string
  const char *data = SHSTRTAB.data() + 1;
  auto sec_name = std::string_view{data};
  while (!sec_name.empty()) {
    if (sec_name.ends_with(name)) {
      return sec_name.data() + sec_name.size() - name.size() - SHSTRTAB.data();
    }

    data += sec_name.size() + 1;
    sec_name = std::string_view{data};
  }

  fail_constexpr_compile("unknown section name");
  return ~0u;
}

consteval static u32 sec_count() {
  // skip the first null string
  const char *data = SECTION_NAMES.data() + 1;
  u32 idx = 1;
  auto sec_name = std::string_view{data};
  while (!sec_name.empty()) {
    ++idx;
    data += sec_name.size() + 1;
    sec_name = std::string_view{data};
  }

  return idx;
}

} // namespace elf

template <typename Derived>
typename AssemblerElf<Derived>::DataSection &
    AssemblerElf<Derived>::get_or_create_section(SecRef &ref,
                                                 unsigned rela_name,
                                                 unsigned type,
                                                 unsigned flags,
                                                 unsigned align,
                                                 bool with_rela) noexcept {
  if (ref == INVALID_SEC_REF) [[unlikely]] {
    ref = static_cast<SecRef>(sections.size());
    const auto str_off = strtab.size();
    if (with_rela) {
      sections.push_back(DataSection(type, flags, rela_name + 5));
      sections.push_back(DataSection(SHT_RELA, 0, rela_name));
    } else {
      sections.push_back(DataSection(type, flags, rela_name));
    }

    std::string_view name{elf::SHSTRTAB.data() + rela_name +
                          (with_rela ? 5 : 0)};
    strtab.insert(strtab.end(), name.begin(), name.end());
    strtab.push_back('\0');

    DataSection &sec = get_section(ref);
    sec.hdr.sh_addralign = align;
    sec.sym = static_cast<SymRef>(local_symbols.size());
    local_symbols.push_back(Elf64_Sym{
        .st_name = static_cast<Elf64_Word>(str_off),
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_other = STV_DEFAULT,
        .st_shndx = static_cast<Elf64_Section>(ref),
        .st_value = 0,
        .st_size = 0,
    });
  }
  return get_section(ref);
}

template <typename Derived>
typename AssemblerElf<Derived>::SecRef
    AssemblerElf<Derived>::get_data_section(bool rodata, bool relro) noexcept {
  SecRef &secref = !rodata ? secref_data : relro ? secref_relro : secref_rodata;
  unsigned off_r = !rodata ? elf::sec_off(".rela.data")
                   : relro ? elf::sec_off(".rela.data.rel.ro")
                           : elf::sec_off(".rela.rodata");
  unsigned flags = SHF_ALLOC | (rodata ? 0 : SHF_WRITE);
  (void)get_or_create_section(secref, off_r, SHT_PROGBITS, flags, 16);
  return secref;
}

template <typename Derived>
typename AssemblerElf<Derived>::SecRef
    AssemblerElf<Derived>::get_structor_section(bool init) noexcept {
  // TODO: comdat, priorities
  SecRef &secref = init ? secref_init_array : secref_fini_array;
  unsigned off_r = init ? elf::sec_off(".rela.init_array")
                        : elf::sec_off(".rela.fini_array");
  unsigned type = init ? SHT_INIT_ARRAY : SHT_FINI_ARRAY;
  (void)get_or_create_section(secref, off_r, type, SHF_ALLOC | SHF_WRITE, 8);
  return secref;
}

template <typename Derived>
void AssemblerElf<Derived>::init_sections() noexcept {
  sections.resize(elf::sec_count());
  unsigned off_r = elf::sec_off(".rela.text");
  (void)get_or_create_section(
      secref_text, off_r, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16);

  current_section = secref_text;
}

template <typename Derived>
void AssemblerElf<Derived>::sym_def_predef_data(SecRef sec_ref,
                                                SymRef sym_ref,
                                                std::span<const u8> data,
                                                const u32 align,
                                                u32 *off) noexcept {
  DataSection &sec = get_section(sec_ref);
  size_t pos = util::align_up(sec.data.size(), align);
  sec.data.resize(pos);
  sec.data.insert(sec.data.end(), data.begin(), data.end());

  if (off) {
    *off = pos;
  }

  Elf64_Sym *sym = sym_ptr(sym_ref);
  sym->st_shndx = static_cast<Elf64_Section>(sec_ref);
  sym->st_value = pos;
  sym->st_size = data.size();
}

template <typename Derived>
typename AssemblerElf<Derived>::SymRef
    AssemblerElf<Derived>::sym_def_bss(const std::string_view name,
                                       const u32 size,
                                       const u32 align,
                                       const bool local,
                                       const bool weak,
                                       u32 *off) noexcept {
  size_t str_off = 0;
  if (!name.empty()) {
    str_off = strtab.size();
    strtab.insert(strtab.end(), name.begin(), name.end());
    strtab.emplace_back('\0');
  }

  uint8_t info;
  if (local) {
    assert(!weak);
    info = ELF64_ST_INFO(STB_LOCAL, STT_OBJECT);
  } else if (weak) {
    info = ELF64_ST_INFO(STB_WEAK, STT_OBJECT);
  } else {
    info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
  }

  assert((align & (align - 1)) == 0);
  const u32 pos = util::align_up(sec_bss_size, align);
  sec_bss_size += size;
  auto sym =
      Elf64_Sym{.st_name = static_cast<Elf64_Word>(str_off),
                .st_info = info,
                .st_other = STV_DEFAULT,
                .st_shndx = static_cast<Elf64_Section>(elf::sec_idx(".bss")),
                .st_value = pos,
                .st_size = size};

  if (off) {
    *off = pos;
  }

  if (local) {
    local_symbols.push_back(sym);
    assert(local_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>(local_symbols.size() - 1);
  } else {
    global_symbols.push_back(sym);
    assert(global_symbols.size() < 0x8000'0000);
    return static_cast<SymRef>((global_symbols.size() - 1) | 0x8000'0000);
  }
}

template <typename Derived>
void AssemblerElf<Derived>::sym_def_predef_bss(const SymRef sym_ref,
                                               const u32 size,
                                               const u32 align,
                                               u32 *off) noexcept {
  Elf64_Sym *sym = sym_ptr(sym_ref);

  assert((align & (align - 1)) == 0);
  const u32 pos = util::align_up(sec_bss_size, align);
  sec_bss_size = pos + size;

  if (off) {
    *off = pos;
  }

  sym->st_shndx = static_cast<Elf64_Section>(elf::sec_idx(".bss"));
  sym->st_value = pos;
  sym->st_size = size;
}

template <typename Derived>
void AssemblerElf<Derived>::text_align_impl(u64 align) noexcept {
  text_ensure_space(align);
  text_write_ptr = reinterpret_cast<u8 *>(
      util::align_up(reinterpret_cast<uintptr_t>(text_write_ptr), align));
}

template <typename Derived>
void AssemblerElf<Derived>::text_ensure_space(u32 size) noexcept {
  if (text_reserve_end - text_write_ptr >= size) [[likely]] {
    return;
  }

  size = util::align_up(size, 16 * 1024);
  const size_t off = text_write_ptr - text_begin;
  DataSection &sec = get_section(current_section);
  sec.data.resize(sec.data.size() + size);

  text_begin = sec.data.data();
  text_write_ptr = text_ptr(off);
  text_reserve_end = text_ptr(sec.data.size());
}

template <typename Derived>
void AssemblerElf<Derived>::reset() noexcept {
  sections.clear();
  global_symbols.clear();
  local_symbols.clear();
  strtab.clear();
  secref_text = INVALID_SEC_REF;
  secref_rodata = INVALID_SEC_REF;
  secref_relro = INVALID_SEC_REF;
  secref_data = INVALID_SEC_REF;
  secref_init_array = INVALID_SEC_REF;
  secref_fini_array = INVALID_SEC_REF;
  sec_eh_frame = {};
  current_section = INVALID_SEC_REF;
  text_begin = text_write_ptr = text_reserve_end = nullptr;
  cur_func = INVALID_SYM_REF;
  sec_bss_size = 0;

  eh_init_cie();
}

template <typename Derived>
std::vector<u8> AssemblerElf<Derived>::build_object_file() {
  using namespace elf;

  // zero-terminate eh_frame
  // TODO(ts): this should probably go somewhere else
  if (!sec_eh_frame.data.empty()) {
    sec_eh_frame.data.resize(sec_eh_frame.data.size() + 4);
  }

  std::vector<u8> out{};

  // Truncate text section to actually needed size
  {
    DataSection &sec_text = get_section(current_section);
    sec_text.data.resize(text_cur_off());
    text_reserve_end = text_ptr(text_cur_off());
  }

  u32 obj_size = sizeof(Elf64_Shdr) + sizeof(Elf64_Shdr) * sections.size();
  obj_size +=
      sizeof(Elf64_Sym) * (local_symbols.size() + global_symbols.size());
  obj_size += strtab.size();
  obj_size += SHSTRTAB.size();
  obj_size += sec_eh_frame.data.size() + sec_eh_frame.relocs.size() +
              sizeof(Elf64_Rela);
  for (const DataSection &sec : sections) {
    obj_size += sec.data.size() + sec.relocs.size() * sizeof(Elf64_Rela);
  }
  out.reserve(obj_size);

  out.resize(sizeof(Elf64_Ehdr));

  const auto shdr_off = out.size();
  out.resize(out.size() + sizeof(Elf64_Shdr) * sections.size());

  const auto sec_hdr = [shdr_off, &out](const u32 idx) {
    return reinterpret_cast<Elf64_Shdr *>(out.data() + shdr_off) + idx;
  };

  {
    auto *hdr = reinterpret_cast<Elf64_Ehdr *>(out.data());

    hdr->e_ident[0] = ELFMAG0;
    hdr->e_ident[1] = ELFMAG1;
    hdr->e_ident[2] = ELFMAG2;
    hdr->e_ident[3] = ELFMAG3;
    hdr->e_ident[4] = ELFCLASS64;
    hdr->e_ident[5] = ELFDATA2LSB;
    hdr->e_ident[6] = EV_CURRENT;
    hdr->e_ident[7] = Derived::ELF_OS_ABI;
    hdr->e_ident[8] = 0;
    hdr->e_type = ET_REL;
    hdr->e_machine = Derived::ELF_MACHINE;
    hdr->e_version = EV_CURRENT;
    hdr->e_shoff = shdr_off;
    hdr->e_ehsize = sizeof(Elf64_Ehdr);
    hdr->e_shentsize = sizeof(Elf64_Shdr);
    hdr->e_shnum = sections.size();
    hdr->e_shstrndx = sec_idx(".shstrtab");
  }

  const auto write_reloc_sec = [this, &out, &sec_hdr](DataSection &sec,
                                                      const u32 sec_idx,
                                                      const u32 sec_off,
                                                      const u32 info_idx) {
    // patch relocations
    for (auto idx : sec.relocs_to_patch) {
      auto ty = ELF64_R_TYPE(sec.relocs[idx].r_info);
      auto sym = ELF64_R_SYM(sec.relocs[idx].r_info);
      sym = (sym & ~0x8000'0000u) + local_symbols.size();
      sec.relocs[idx].r_info = ELF64_R_INFO(sym, ty);
    }

    const auto size = sizeof(Elf64_Rela) * sec.relocs.size();
    const auto sh_off = out.size();
    out.insert(out.end(),
               reinterpret_cast<uint8_t *>(&*sec.relocs.begin()),
               reinterpret_cast<uint8_t *>(&*sec.relocs.end()));

    auto *hdr = sec_hdr(sec_idx);
    hdr->sh_name = sec_off;
    hdr->sh_type = SHT_RELA;
    hdr->sh_flags = SHF_INFO_LINK;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_link = elf::sec_idx(".symtab");
    hdr->sh_info = info_idx;
    hdr->sh_addralign = 8;
    hdr->sh_entsize = sizeof(Elf64_Rela);
  };

  // .note.GNU-stack
  {
    auto *hdr = sec_hdr(sec_idx(".note.GNU-stack"));
    hdr->sh_name = sec_off(".note.GNU-stack");
    hdr->sh_type = SHT_PROGBITS;
    hdr->sh_offset = out.size(); // gcc seems to give empty sections an offset
    hdr->sh_addralign = 1;
  }

  // .eh_frame
  {
    const auto size = util::align_up(sec_eh_frame.data.size(), 8);
    const auto pad = size - sec_eh_frame.data.size();
    const auto sh_off = out.size();
    out.insert(out.end(), sec_eh_frame.data.begin(), sec_eh_frame.data.end());
    out.resize(out.size() + pad);

    auto *hdr = sec_hdr(sec_idx(".eh_frame"));
    hdr->sh_name = sec_off(".eh_frame");
    hdr->sh_type = SHT_PROGBITS;
    hdr->sh_flags = SHF_ALLOC;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_addralign = 8;
  }

  // .rela.eh_frame
  write_reloc_sec(sec_eh_frame,
                  sec_idx(".rela.eh_frame"),
                  sec_off(".rela.eh_frame"),
                  sec_idx(".eh_frame"));

  // .symtab
  {
    const auto size =
        sizeof(Elf64_Sym) * (local_symbols.size() + global_symbols.size());
    const auto sh_off = out.size();
    out.insert(out.end(),
               reinterpret_cast<uint8_t *>(&*local_symbols.begin()),
               reinterpret_cast<uint8_t *>(&*local_symbols.end()));
    // global symbols need to come after the local symbols
    out.insert(out.end(),
               reinterpret_cast<uint8_t *>(&*global_symbols.begin()),
               reinterpret_cast<uint8_t *>(&*global_symbols.end()));

    auto *hdr = sec_hdr(sec_idx(".symtab"));
    hdr->sh_name = sec_off(".symtab");
    hdr->sh_type = SHT_SYMTAB;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_link = sec_idx(".strtab");
    hdr->sh_info = local_symbols.size(); // first non-local symbol idx
    hdr->sh_addralign = 8;
    hdr->sh_entsize = sizeof(Elf64_Sym);
  }

  // .strtab
  {
    const auto size = util::align_up(strtab.size(), 8);
    const auto pad = size - strtab.size();
    const auto sh_off = out.size();
    out.insert(out.end(),
               reinterpret_cast<uint8_t *>(&*strtab.begin()),
               reinterpret_cast<uint8_t *>(&*strtab.end()));
    out.resize(out.size() + pad);

    auto *hdr = sec_hdr(sec_idx(".strtab"));
    hdr->sh_name = sec_off(".strtab");
    hdr->sh_type = SHT_STRTAB;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_addralign = 1;
  }

  // .shstrtab
  {
    const auto size = util::align_up(SHSTRTAB.size(), 8);
    const auto pad = size - SHSTRTAB.size();
    const auto sh_off = out.size();
    out.insert(
        out.end(),
        reinterpret_cast<const uint8_t *>(SHSTRTAB.data()),
        reinterpret_cast<const uint8_t *>(SHSTRTAB.data() + SHSTRTAB.size()));
    out.resize(out.size() + pad);

    auto *hdr = sec_hdr(sec_idx(".shstrtab"));
    hdr->sh_name = sec_off(".shstrtab");
    hdr->sh_type = SHT_STRTAB;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_addralign = 1;
  }

  // .bss
  {
    const auto size = util::align_up(sec_bss_size, 16);
    const auto sh_off = out.size();

    auto *hdr = sec_hdr(sec_idx(".bss"));
    hdr->sh_name = sec_off(".bss");
    hdr->sh_type = SHT_NOBITS;
    hdr->sh_flags = SHF_ALLOC | SHF_WRITE;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_addralign = 16;
  }

  for (size_t i = sec_count(); i < sections.size(); ++i) {
    DataSection &sec = sections[i];
    sec.hdr.sh_offset = out.size();
    sec.hdr.sh_size = sec.data.size();
    *sec_hdr(i) = sec.hdr;

    const auto pad = util::align_up(sec.data.size(), 8) - sec.data.size();
    out.insert(out.end(), sec.data.begin(), sec.data.end());
    out.resize(out.size() + pad);

    if (i + 1 < sections.size() && sections[i + 1].hdr.sh_type == SHT_RELA) {
      write_reloc_sec(sec, i + 1, sections[i + 1].hdr.sh_name, i);
      ++i;
    } else {
      assert(sec.relocs.empty() && "relocations in section without .rela");
    }
  }

  return out;
}

template <typename Derived>
void AssemblerElf<Derived>::reloc_text(const SymRef sym,
                                       const u32 type,
                                       const u64 offset,
                                       const i64 addend) noexcept {
  reloc_sec(current_section, sym, type, offset, addend);
}

template <typename Derived>
void AssemblerElf<Derived>::reloc_sec(DataSection &sec,
                                      const SymRef sym,
                                      const u32 type,
                                      const u64 offset,
                                      const i64 addend) noexcept {
  Elf64_Rela rel{};
  rel.r_offset = offset;
  rel.r_info = ELF64_R_INFO(static_cast<u32>(sym), type);
  rel.r_addend = addend;
  sec.relocs.push_back(rel);
  if (!sym_is_local(sym)) {
    sec.relocs_to_patch.push_back(sec.relocs.size() - 1);
  }
}

template <typename Derived>
void AssemblerElf<Derived>::eh_align_frame() noexcept {
  while ((sec_eh_frame.data.size() & 7) != 0) {
    sec_eh_frame.data.push_back(dwarf::DW_CFA_nop);
  }
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_inst(const u8 opcode,
                                          const u64 arg) noexcept {
  if ((opcode & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) != 0) {
    assert((arg & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) == 0);
    sec_eh_frame.data.push_back(opcode | arg);
  } else {
    sec_eh_frame.data.push_back(opcode);
    eh_write_uleb(arg);
  }
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_inst(const u8 opcode,
                                          const u64 first_arg,
                                          const u64 second_arg) noexcept {
  eh_write_inst(opcode, first_arg);
  eh_write_uleb(second_arg);
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_uleb(u64 value) noexcept {
  eh_write_uleb(sec_eh_frame.data, value);
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_uleb(std::vector<u8> &dst,
                                          u64 value) noexcept {
  while (true) {
    u8 write = value & 0b0111'1111;
    value >>= 7;
    if (value == 0) {
      dst.push_back(write);
      break;
    }
    dst.push_back(write | 0b1000'0000);
  }
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_sleb(std::vector<u8> &dst,
                                          i64 value) noexcept {
  while (true) {
    u8 tmp = value & 0x7F;
    value >>= 7;
    if ((value == 0 && (value & 0x40) == 0) || (value == -1 && value & 0x40)) {
      dst.push_back(tmp);
      break;
    } else {
      dst.push_back(tmp | 0x80);
    }
  }
}

template <typename Derived>
u32 AssemblerElf<Derived>::eh_uleb_len(u64 value) noexcept {
  u32 len = 0;
  while (true) {
    value >>= 7;
    if (value == 0) {
      ++len;
      break;
    }
    ++len;
  }
  return len;
}

template <typename Derived>
void AssemblerElf<Derived>::eh_init_cie(SymRef personality_func_addr) noexcept {
  // write out the initial CIE
  eh_align_frame();
  auto &data = sec_eh_frame.data;

  // CIE layout:
  // length: u32
  // id: u32
  // version: u8
  // augmentation: 3 or 5 bytes depending on whether the CIE has a personality
  // function
  // code_alignment_factor: uleb128 (but we only use 1 byte)
  // data_alignment_factor: sleb128 (but we only use 1 byte)
  // return_addr_register: u8
  // augmentation_data_len: uleb128 (but we only use 1 byte)
  // augmentation_data:
  //   if personality:
  //     personality_encoding: u8
  //     personality_addr: u32
  //     lsa_encoding: u8
  //   fde_ptr_encoding: u8
  // instructions: [u8]
  //
  // total: 17 bytes or 25 bytes

  const auto first = data.empty();
  auto off = data.size();
  eh_cur_cie_off = off;

  data.resize(data.size() +
              (personality_func_addr == INVALID_SYM_REF ? 17 : 25));

  // id is 0 for CIEs

  // version is 1
  data[off + 8] = 1;

  if (personality_func_addr == INVALID_SYM_REF) {
    // augmentation is "zR" for a CIE with no personality meaning there is
    // the augmentation_data_len and ptr_size field
    data[off + 9] = 'z';
    data[off + 10] = 'R';
  } else {
    // with a personality function the augmentation is "zPLR" meaning there
    // is augmentation_data_len, personality_encoding, personality_addr,
    // lsa_encoding and ptr_size
    data[off + 9] = 'z';
    data[off + 10] = 'P';
    data[off + 11] = 'L';
    data[off + 12] = 'R';
  }

  u32 bias = (personality_func_addr == INVALID_SYM_REF) ? 0 : 2;

  // code_alignment_factor is 1
  data[off + 12 + bias] = 1;

  // data_alignment_factor is 127 representing -1
  data[off + 13 + bias] = 127;

  // return_addr_register is defined by the derived impl
  data[off + 14 + bias] = Derived::DWARF_EH_RETURN_ADDR_REGISTER;

  // augmentation_data_len is 1 when no personality is present or 7 otherwise
  data[off + 15 + bias] = (personality_func_addr == INVALID_SYM_REF) ? 1 : 7;

  if (personality_func_addr != INVALID_SYM_REF) {
    // the personality encoding is a 4-byte pc-relative address where the
    // address of the personality func is stored
    data[off + 16 + bias] = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4 |
                            dwarf::DW_EH_PE_indirect;

    derived()->reloc_eh_frame_pc32(personality_func_addr, off + 17 + bias, 0);

    // the lsa_encoding as a 4-byte pc-relative address since the whole
    // object should fit in 2gb
    data[off + 21 + bias] = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;

    bias += 6;
  }

  // fde_ptr_encoding is a 4-byte signed pc-relative address
  data[off + bias + 16] = dwarf::DW_EH_PE_sdata4 | dwarf::DW_EH_PE_pcrel;

  derived()->eh_write_initial_cie_instrs();

  eh_align_frame();

  // patch size of CIE (length is not counted)
  *reinterpret_cast<u32 *>(data.data() + off) = data.size() - off - sizeof(u32);

  if (first) {
    eh_first_fde_off = data.size();
  }
}

template <typename Derived>
u32 AssemblerElf<Derived>::eh_write_fde_start() noexcept {
  eh_align_frame();

  auto &data = sec_eh_frame.data;
  const auto fde_off = data.size();

  // FDE Layout:
  //  length: u32
  //  id: u32
  //  func_start: i32
  //  func_size: i32
  // augmentation_data_len: uleb128 (but we only use 1 byte)
  // augmentation_data:
  //   if personality:
  //     lsda_ptr: i32 (we use a 4 byte signed pc-relative pointer to an
  //     absolute address)
  // instructions: [u8]
  //
  // Total Size: 17 bytes or 21 bytes

  data.resize(data.size() +
              (cur_personality_func_addr == INVALID_SYM_REF ? 17 : 21));

  // we encode length later

  // id is the offset from the current CIE to the id field
  *reinterpret_cast<u32 *>(data.data() + fde_off + 4) =
      fde_off - eh_cur_cie_off + sizeof(u32);

  // func_start will be relocated by the arch impl

  // func_size
  auto *func_sym = sym_ptr(cur_func);
  *reinterpret_cast<i32 *>(data.data() + fde_off + 12) = func_sym->st_size;

  // augmentation_data_len is 0 with no personality or 4 otherwise
  if (cur_personality_func_addr != INVALID_SYM_REF) {
    data[fde_off + 16] = 4;
  }

  return fde_off;
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_fde_len(const u32 fde_off) noexcept {
  eh_align_frame();

  const u32 len = sec_eh_frame.data.size() - fde_off - sizeof(u32);
  *reinterpret_cast<u32 *>(sec_eh_frame.data.data() + fde_off) = len;
  if (cur_personality_func_addr != INVALID_SYM_REF) {
    DataSection &except_table =
        get_or_create_section(secref_except_table,
                              elf::sec_off(".rela.gcc_except_table"),
                              SHT_PROGBITS,
                              SHF_ALLOC,
                              8);
    derived()->reloc_eh_frame_pc32(
        except_table.sym, fde_off + 17, except_table.data.size());
  }
}

template <typename Derived>
void AssemblerElf<Derived>::except_encode_func() noexcept {
  if (cur_personality_func_addr == INVALID_SYM_REF) {
    return;
  }

  // encode the call sites first, otherwise we can't write the header
  except_encoded_call_sites.clear();
  except_encoded_call_sites.reserve(16 * except_call_site_table.size());

  const auto *sym = sym_ptr(cur_func);
  u64 fn_start = sym->st_value;
  u64 fn_end = fn_start + sym->st_size;
  u64 cur = fn_start;
  for (auto &info : except_call_site_table) {
    if (info.start > cur) {
      // Encode padding entry
      eh_write_uleb(except_encoded_call_sites, cur - fn_start);
      eh_write_uleb(except_encoded_call_sites, info.start - cur);
      eh_write_uleb(except_encoded_call_sites, 0);
      eh_write_uleb(except_encoded_call_sites, 0);
    }
    eh_write_uleb(except_encoded_call_sites, info.start - fn_start);
    eh_write_uleb(except_encoded_call_sites, info.len);
    assert((info.pad_label_or_off - fn_start) < (fn_end - fn_start));
    eh_write_uleb(except_encoded_call_sites, info.pad_label_or_off - fn_start);
    eh_write_uleb(except_encoded_call_sites, info.action_entry);
    cur = info.start + info.len;
  }
  if (cur < fn_end) {
    // Add padding until the end of the function
    eh_write_uleb(except_encoded_call_sites, cur - fn_start);
    eh_write_uleb(except_encoded_call_sites, fn_end - cur);
    eh_write_uleb(except_encoded_call_sites, 0);
    eh_write_uleb(except_encoded_call_sites, 0);
  }

  // zero-terminate
  except_encoded_call_sites.push_back(0);
  except_encoded_call_sites.push_back(0);

  auto &except_table = get_section(secref_except_table).data;
  // write the lsda (see
  // https://github.com/llvm/llvm-project/blob/main/libcxxabi/src/cxa_personality.cpp#L60)
  except_table.push_back(dwarf::DW_EH_PE_omit); // lpStartEncoding
  if (except_action_table.empty()) {
    assert(except_type_info_table.empty());
    // we don't need the type_info table if there is no action entry
    except_table.push_back(dwarf::DW_EH_PE_omit); // ttypeEncoding
  } else {
    except_table.push_back(dwarf::DW_EH_PE_sdata4 | dwarf::DW_EH_PE_pcrel |
                           dwarf::DW_EH_PE_indirect); // ttypeEncoding
    uint64_t classInfoOff =
        (except_type_info_table.size() + 1) * sizeof(uint32_t);
    classInfoOff += except_action_table.size();
    classInfoOff += except_encoded_call_sites.size() +
                    eh_uleb_len(except_encoded_call_sites.size()) + 1;
    eh_write_uleb(except_table, classInfoOff);
  }

  except_table.push_back(dwarf::DW_EH_PE_uleb128); // callSiteEncoding
  eh_write_uleb(except_table,
                except_encoded_call_sites.size()); // callSiteTableLength
  except_table.insert(except_table.end(),
                      except_encoded_call_sites.begin(),
                      except_encoded_call_sites.end());
  except_table.insert(except_table.end(),
                      except_action_table.begin(),
                      except_action_table.end());

  if (!except_action_table.empty()) {
    except_table.resize(except_table.size() +
                        ((except_type_info_table.size() + 1) *
                         sizeof(u32))); // allocate space for type_info table

    // in reverse order since indices are negative
    size_t off = except_table.size() - sizeof(u32) * 2;
    for (auto sym : except_type_info_table) {
      derived()->reloc_pc32(secref_except_table, sym, off, 0);
      off -= sizeof(u32);
    }

    except_table.insert(
        except_table.end(), except_spec_table.begin(), except_spec_table.end());
  }
}

template <typename Derived>
void AssemblerElf<Derived>::except_add_call_site(
    const u32 text_off,
    const u32 len,
    const u32 landing_pad_id,
    const bool is_cleanup) noexcept {
  except_call_site_table.push_back(ExceptCallSiteInfo{
      .start = text_off,
      .len = len,
      .pad_label_or_off = landing_pad_id,
      .action_entry =
          (is_cleanup ? 0 : static_cast<u32>(except_action_table.size()) + 1),
  });
}

template <typename Derived>
void AssemblerElf<Derived>::except_add_cleanup_action() noexcept {
  // pop back the action offset
  except_action_table.pop_back();
  eh_write_sleb(except_action_table,
                -static_cast<i64>(except_action_table.size()));
}

template <typename Derived>
void AssemblerElf<Derived>::except_add_action(const bool first_action,
                                              const SymRef type_sym) noexcept {
  if (!first_action) {
    except_action_table.back() = 1;
  }

  auto idx = 0u;
  if (type_sym != INVALID_SYM_REF) {
    auto found = false;
    for (const auto &sym : except_type_info_table) {
      ++idx;
      if (sym == type_sym) {
        found = true;
        break;
      }
    }
    if (!found) {
      ++idx;
      except_type_info_table.push_back(type_sym);
    }
  }

  eh_write_sleb(except_action_table, idx + 1);
  except_action_table.push_back(0);
}

template <typename Derived>
void AssemblerElf<Derived>::except_add_empty_spec_action(
    const bool first_action) noexcept {
  if (!first_action) {
    except_action_table.back() = 1;
  }

  if (except_spec_table.empty()) {
    except_spec_table.resize(4);
  }

  eh_write_sleb(except_action_table, -1);
  except_action_table.push_back(0);
}

template <typename Derived>
u32 AssemblerElf<Derived>::except_type_idx_for_sym(const SymRef sym) noexcept {
  // to explain the indexing
  // a ttypeIndex of 0 is reserved for a cleanup action so the type table
  // starts at 1 but the first entry in the type table is reserved for the 0
  // pointer used for catch(...) meaning we start at 2
  auto idx = 2u;
  for (const auto type_sym : except_type_info_table) {
    if (type_sym == sym) {
      return idx;
    }
    ++idx;
  }
  assert(0);
  return idx;
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
