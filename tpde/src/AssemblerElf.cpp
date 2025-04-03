// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde/AssemblerElf.hpp"

#include <algorithm>
#include <elf.h>

namespace tpde {

// TODO(ts): maybe just outsource this to a helper func that can live in a cpp
// file?
namespace elf {
// TODO(ts): this is linux-specific, no?
constexpr static std::span<const char> SECTION_NAMES = {
    "\0" // first section is the null-section
    ".note.GNU-stack\0"
    ".symtab\0"
    ".strtab\0"
    ".shstrtab\0"};

// TODO(ts): this is linux-specific, no?
constexpr static std::span<const char> SHSTRTAB = {
    "\0" // first section is the null-section
    ".note.GNU-stack\0"
    ".rela.eh_frame\0"
    ".symtab\0"
    ".strtab\0"
    ".shstrtab\0"
    ".bss\0"
    ".tbss\0"
    ".rela.rodata\0"
    ".rela.text\0"
    ".rela.data.rel.ro\0"
    ".rela.data\0"
    ".rela.tdata\0"
    ".rela.gcc_except_table\0"
    ".rela.init_array\0"
    ".rela.fini_array\0"};

static void fail_constexpr_compile(const char *) {
  assert(0);
  exit(1);
}

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

void AssemblerElfBase::reset() noexcept {
  sections.clear();
  section_allocator.reset();

  global_symbols.clear();
  local_symbols.resize(1); // first symbol must be null
  temp_symbols.clear();
  temp_symbol_fixups.clear();
  next_free_tsfixup = ~0u;
  strtab.resize(1); // must begin with null byte
  shstrtab_extra.clear();
  secref_text = INVALID_SEC_REF;
  secref_rodata = INVALID_SEC_REF;
  secref_relro = INVALID_SEC_REF;
  secref_data = INVALID_SEC_REF;
  secref_bss = INVALID_SEC_REF;
  secref_tdata = INVALID_SEC_REF;
  secref_tbss = INVALID_SEC_REF;
  secref_init_array = INVALID_SEC_REF;
  secref_fini_array = INVALID_SEC_REF;
  secref_eh_frame = INVALID_SEC_REF;
  secref_except_table = INVALID_SEC_REF;
  cur_personality_func_addr = SymRef();

  init_sections();
  eh_init_cie();
}

AssemblerElfBase::SecRef AssemblerElfBase::create_section(
    unsigned type, unsigned flags, unsigned name) noexcept {
  SecRef ref = static_cast<SecRef>(sections.size());
  sections.emplace_back(new (section_allocator)
                            DataSection(ref, type, flags, name));
  return ref;
}

AssemblerElfBase::SymRef
    AssemblerElfBase::create_section_symbol(SecRef ref,
                                            std::string_view name) noexcept {
  const auto str_off = strtab.size();
  strtab.insert(strtab.end(), name.begin(), name.end());
  strtab.push_back('\0');

  SymRef sym = SymRef(local_symbols.size());
  local_symbols.push_back(Elf64_Sym{
      .st_name = static_cast<Elf64_Word>(str_off),
      .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
      .st_other = STV_DEFAULT,
      .st_shndx = static_cast<Elf64_Section>(ref),
      .st_value = 0,
      .st_size = 0,
  });
  return sym;
}

AssemblerElfBase::DataSection &
    AssemblerElfBase::get_or_create_section(SecRef &ref,
                                            unsigned rela_name,
                                            unsigned type,
                                            unsigned flags,
                                            unsigned align,
                                            bool with_rela) noexcept {
  if (ref == INVALID_SEC_REF) [[unlikely]] {
    if (with_rela) {
      ref = create_section(type, flags, rela_name + 5);
      (void)create_section(SHT_RELA, 0, rela_name);
    } else {
      ref = create_section(type, flags, rela_name);
    }

    std::string_view name{elf::SHSTRTAB.data() + rela_name +
                          (with_rela ? 5 : 0)};

    DataSection &sec = get_section(ref);
    sec.hdr.sh_addralign = align;
    sec.sym = create_section_symbol(ref, name);
  }
  return get_section(ref);
}

const char *AssemblerElfBase::sec_name(SecRef ref) const noexcept {
  const DataSection &sec = get_section(ref);
  assert(sec.hdr.sh_name < elf::SHSTRTAB.size());
  return elf::SHSTRTAB.data() + sec.hdr.sh_name;
}

AssemblerElfBase::SecRef
    AssemblerElfBase::get_data_section(bool rodata, bool relro) noexcept {
  SecRef &secref = !rodata ? secref_data : relro ? secref_relro : secref_rodata;
  unsigned off_r = !rodata ? elf::sec_off(".rela.data")
                   : relro ? elf::sec_off(".rela.data.rel.ro")
                           : elf::sec_off(".rela.rodata");
  unsigned flags = SHF_ALLOC | (rodata && !relro ? 0 : SHF_WRITE);
  (void)get_or_create_section(secref, off_r, SHT_PROGBITS, flags, 1);
  return secref;
}

AssemblerElfBase::SecRef AssemblerElfBase::get_bss_section() noexcept {
  unsigned off = elf::sec_off(".bss");
  unsigned flags = SHF_ALLOC | SHF_WRITE;
  (void)get_or_create_section(secref_bss, off, SHT_NOBITS, flags, 1, false);
  return secref_bss;
}

AssemblerElfBase::SecRef AssemblerElfBase::get_tdata_section() noexcept {
  unsigned off_r = elf::sec_off(".rela.tdata");
  unsigned flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
  (void)get_or_create_section(secref_tdata, off_r, SHT_PROGBITS, flags, 1);
  return secref_tdata;
}

AssemblerElfBase::SecRef AssemblerElfBase::get_tbss_section() noexcept {
  unsigned off = elf::sec_off(".tbss");
  unsigned flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
  (void)get_or_create_section(secref_tbss, off, SHT_NOBITS, flags, 1, false);
  return secref_tbss;
}

AssemblerElfBase::SecRef
    AssemblerElfBase::get_structor_section(bool init) noexcept {
  // TODO: comdat, priorities
  SecRef &secref = init ? secref_init_array : secref_fini_array;
  unsigned off_r = init ? elf::sec_off(".rela.init_array")
                        : elf::sec_off(".rela.fini_array");
  unsigned type = init ? SHT_INIT_ARRAY : SHT_FINI_ARRAY;
  (void)get_or_create_section(secref, off_r, type, SHF_ALLOC | SHF_WRITE, 8);
  return secref;
}

AssemblerElfBase::SecRef
    AssemblerElfBase::create_section(std::string_view name,
                                     unsigned type,
                                     unsigned flags,
                                     bool with_rela) noexcept {
  assert(name.find('\0') == std::string_view::npos &&
         "name must not contain null-bytes");
  size_t rela_name = shstrtab_extra.size() + elf::SHSTRTAB.size();
  shstrtab_extra.reserve(shstrtab_extra.size() + name.size() + 6);
  if (with_rela) {
    shstrtab_extra += ".rela";
  }
  shstrtab_extra += name;
  shstrtab_extra.push_back('\0');

  SecRef ref;
  if (with_rela) {
    ref = create_section(type, flags, rela_name + 5);
    (void)create_section(SHT_RELA, 0, rela_name);
  } else {
    ref = create_section(type, flags, rela_name);
  }

  get_section(ref).sym = create_section_symbol(ref, name);
  return ref;
}

void AssemblerElfBase::init_sections() noexcept {
  for (size_t i = 0; i < elf::sec_count(); i++) {
    (void)create_section(SHT_NULL, 0, 0);
  }

  unsigned off_text = elf::sec_off(".rela.text");
  (void)get_or_create_section(
      secref_text, off_text, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16);
  unsigned off_eh_frame = elf::sec_off(".rela.eh_frame");
  (void)get_or_create_section(
      secref_eh_frame, off_eh_frame, SHT_PROGBITS, SHF_ALLOC, 8);
}


void AssemblerElfBase::sym_copy(SymRef dst, SymRef src) noexcept {
  Elf64_Sym *src_ptr = sym_ptr(src), *dst_ptr = sym_ptr(dst);

  dst_ptr->st_shndx = src_ptr->st_shndx;
  dst_ptr->st_size = src_ptr->st_size;
  dst_ptr->st_value = src_ptr->st_value;
  // Don't copy st_info.
}

AssemblerElfBase::SymRef AssemblerElfBase::sym_add(const std::string_view name,
                                                   SymBinding binding,
                                                   u32 type) noexcept {
  size_t str_off = 0;
  if (!name.empty()) {
    str_off = strtab.size();
    strtab.insert(strtab.end(), name.begin(), name.end());
    strtab.emplace_back('\0');
  }

  u8 info;
  switch (binding) {
    using enum AssemblerElfBase::SymBinding;
  case LOCAL: info = ELF64_ST_INFO(STB_LOCAL, type); break;
  case WEAK: info = ELF64_ST_INFO(STB_WEAK, type); break;
  case GLOBAL: info = ELF64_ST_INFO(STB_GLOBAL, type); break;
  default: TPDE_UNREACHABLE("invalid symbol binding");
  }
  auto sym = Elf64_Sym{.st_name = static_cast<Elf64_Word>(str_off),
                       .st_info = info,
                       .st_other = STV_DEFAULT,
                       .st_shndx = SHN_UNDEF,
                       .st_value = 0,
                       .st_size = 0};

  if (binding == SymBinding::LOCAL) {
    local_symbols.push_back(sym);
    assert(local_symbols.size() < 0x8000'0000);
    return SymRef(local_symbols.size() - 1);
  } else {
    global_symbols.push_back(sym);
    assert(global_symbols.size() < 0x8000'0000);
    return SymRef((global_symbols.size() - 1) | 0x8000'0000);
  }
}

void AssemblerElfBase::sym_def_predef_data(SecRef sec_ref,
                                           SymRef sym_ref,
                                           std::span<const u8> data,
                                           const u32 align,
                                           u32 *off) noexcept {
  DataSection &sec = get_section(sec_ref);
  sec.hdr.sh_addralign = std::max(sec.hdr.sh_addralign, size_t{align});
  size_t pos = util::align_up(sec.size(), align);
  sym_def(sym_ref, sec_ref, pos, data.size());
  assert(sec.hdr.sh_type != SHT_NOBITS && "cannot add data to SHT_NOBITS");
  sec.data.resize(pos);
  sec.data.insert(sec.data.end(), data.begin(), data.end());

  if (off) {
    *off = pos;
  }
}

void AssemblerElfBase::sym_def_predef_zero(
    SecRef sec_ref, SymRef sym_ref, u32 size, u32 align, u32 *off) noexcept {
  DataSection &sec = get_section(sec_ref);
  sec.hdr.sh_addralign = std::max(sec.hdr.sh_addralign, size_t{align});
  size_t pos = util::align_up(sec.size(), align);
  sym_def(sym_ref, sec_ref, pos, size);
  if (sec.hdr.sh_type == SHT_NOBITS) {
    sec.hdr.sh_size = pos + size;
  } else {
    sec.data.resize(pos + size);
  }

  if (off) {
    *off = pos;
  }
}

void AssemblerElfBase::reloc_sec(const SecRef sec_ref,
                                 const SymRef sym,
                                 const u32 type,
                                 const u64 offset,
                                 const i64 addend) noexcept {
  Elf64_Rela rel{};
  rel.r_offset = offset;
  rel.r_info = ELF64_R_INFO(sym.id(), type);
  rel.r_addend = addend;
  DataSection &sec = get_section(sec_ref);
  sec.relocs.push_back(rel);
}

void AssemblerElfBase::reloc_sec(const SecRef sec,
                                 const Label label,
                                 const u8 kind,
                                 const u32 offset) noexcept {
  assert(label_is_pending(label));
  u32 fixup_idx;
  if (next_free_tsfixup != ~0u) {
    fixup_idx = next_free_tsfixup;
    next_free_tsfixup = temp_symbol_fixups[fixup_idx].next_list_entry;
  } else {
    fixup_idx = temp_symbol_fixups.size();
    temp_symbol_fixups.push_back(TempSymbolFixup{});
  }

  TempSymbolInfo &info = temp_symbols[static_cast<u32>(label)];
  temp_symbol_fixups[fixup_idx] = TempSymbolFixup{
      .section = sec,
      .next_list_entry = info.fixup_idx,
      .off = offset,
      .kind = kind,
  };
  info.fixup_idx = fixup_idx;
}

void AssemblerElfBase::eh_align_frame() noexcept {
  auto &data = get_section(secref_eh_frame).data;
  while ((data.size() & 7) != 0) {
    data.push_back(dwarf::DW_CFA_nop);
  }
}

void AssemblerElfBase::eh_write_inst(const u8 opcode, const u64 arg) noexcept {
  if ((opcode & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) != 0) {
    assert((arg & dwarf::DWARF_CFI_PRIMARY_OPCODE_MASK) == 0);
    get_section(secref_eh_frame).data.push_back(opcode | arg);
  } else {
    get_section(secref_eh_frame).data.push_back(opcode);
    eh_write_uleb(arg);
  }
}

void AssemblerElfBase::eh_write_inst(const u8 opcode,
                                     const u64 first_arg,
                                     const u64 second_arg) noexcept {
  eh_write_inst(opcode, first_arg);
  eh_write_uleb(second_arg);
}

void AssemblerElfBase::eh_write_uleb(u64 value) noexcept {
  eh_write_uleb(get_section(secref_eh_frame).data, value);
}

void AssemblerElfBase::eh_write_uleb(std::vector<u8> &dst, u64 value) noexcept {
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

void AssemblerElfBase::eh_write_sleb(std::vector<u8> &dst, i64 value) noexcept {
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

void AssemblerElfBase::eh_init_cie(SymRef personality_func_addr) noexcept {
  // write out the initial CIE
  eh_align_frame();
  auto &data = get_section(secref_eh_frame).data;

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

  data.resize(data.size() + (!personality_func_addr.valid() ? 17 : 25));

  // id is 0 for CIEs

  // version is 1
  data[off + 8] = 1;

  if (!personality_func_addr.valid()) {
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

  u32 bias = (!personality_func_addr.valid()) ? 0 : 2;

  data[off + 12 + bias] = target_info.cie_code_alignment_factor;
  data[off + 13 + bias] = target_info.cie_data_alignment_factor;

  // return_addr_register is defined by the derived impl
  data[off + 14 + bias] = target_info.cie_return_addr_register;

  // augmentation_data_len is 1 when no personality is present or 7 otherwise
  data[off + 15 + bias] = (!personality_func_addr.valid()) ? 1 : 7;

  if (personality_func_addr.valid()) {
    // the personality encoding is a 4-byte pc-relative address where the
    // address of the personality func is stored
    data[off + 16 + bias] = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4 |
                            dwarf::DW_EH_PE_indirect;

    reloc_sec(secref_eh_frame,
              personality_func_addr,
              target_info.reloc_pc32,
              off + 17 + bias,
              0);

    // the lsa_encoding as a 4-byte pc-relative address since the whole
    // object should fit in 2gb
    data[off + 21 + bias] = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;

    bias += 6;
  }

  // fde_ptr_encoding is a 4-byte signed pc-relative address
  data[off + bias + 16] = dwarf::DW_EH_PE_sdata4 | dwarf::DW_EH_PE_pcrel;

  data.insert(
      data.end(), target_info.cie_instrs.begin(), target_info.cie_instrs.end());

  eh_align_frame();

  // patch size of CIE (length is not counted)
  *reinterpret_cast<u32 *>(data.data() + off) = data.size() - off - sizeof(u32);

  if (first) {
    eh_first_fde_off = data.size();
  }
}

u32 AssemblerElfBase::eh_begin_fde(SymRef personality_func_addr) noexcept {
  if (personality_func_addr != cur_personality_func_addr) {
    eh_init_cie(personality_func_addr);
    cur_personality_func_addr = personality_func_addr;
  }

  eh_align_frame();

  auto &data = get_section(secref_eh_frame).data;
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

  data.resize(data.size() + (!cur_personality_func_addr.valid() ? 17 : 21));

  // we encode length later

  // id is the offset from the current CIE to the id field
  *reinterpret_cast<u32 *>(data.data() + fde_off + 4) =
      fde_off - eh_cur_cie_off + sizeof(u32);

  // func_start and func_size will be relocated at the end

  // augmentation_data_len is 0 with no personality or 4 otherwise
  if (cur_personality_func_addr.valid()) {
    data[fde_off + 16] = 4;
  }

  return fde_off;
}

void AssemblerElfBase::eh_end_fde(u32 fde_start, SymRef func) noexcept {
  eh_align_frame();

  auto &eh_data = get_section(secref_eh_frame).data;
  auto *func_sym = sym_ptr(func);

  // relocate the func_start to the function
  // relocate against .text so we don't have to fix up any relocations
  // NB: ld.bfd (for a reason that needs to be investigated) doesn't accept
  // using the function symbol here.
  DataSection &func_sec = get_section(SecRef{func_sym->st_shndx});
  this->reloc_sec(secref_eh_frame,
                  func_sec.sym,
                  target_info.reloc_pc32,
                  fde_start + 8,
                  func_sym->st_value);
  // Adjust func_size to the function size
  *reinterpret_cast<i32 *>(eh_data.data() + fde_start + 12) = func_sym->st_size;

  const u32 len = eh_data.size() - fde_start - sizeof(u32);
  *reinterpret_cast<u32 *>(eh_data.data() + fde_start) = len;
  if (cur_personality_func_addr.valid()) {
    DataSection &except_table =
        get_or_create_section(secref_except_table,
                              elf::sec_off(".rela.gcc_except_table"),
                              SHT_PROGBITS,
                              SHF_ALLOC,
                              8);
    ;
    reloc_sec(secref_eh_frame,
              except_table.sym,
              target_info.reloc_pc32,
              fde_start + 17,
              except_table.data.size());
  }
}

void AssemblerElfBase::except_encode_func(SymRef func_sym) noexcept {
  if (!cur_personality_func_addr.valid()) {
    return;
  }

  // encode the call sites first, otherwise we can't write the header
  except_encoded_call_sites.clear();
  except_encoded_call_sites.reserve(16 * except_call_site_table.size());

  const auto *sym = sym_ptr(func_sym);
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
                    util::uleb_len(except_encoded_call_sites.size()) + 1;
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
      reloc_sec(secref_except_table, sym, target_info.reloc_pc32, off, 0);
      off -= sizeof(u32);
    }

    except_table.insert(
        except_table.end(), except_spec_table.begin(), except_spec_table.end());
  }

  except_call_site_table.clear();
  except_action_table.clear();
  except_type_info_table.clear();
  except_spec_table.clear();
  except_action_table.resize(2); // cleanup entry
}

void AssemblerElfBase::except_add_call_site(const u32 text_off,
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

void AssemblerElfBase::except_add_cleanup_action() noexcept {
  // pop back the action offset
  except_action_table.pop_back();
  eh_write_sleb(except_action_table,
                -static_cast<i64>(except_action_table.size()));
}

void AssemblerElfBase::except_add_action(const bool first_action,
                                         const SymRef type_sym) noexcept {
  if (!first_action) {
    except_action_table.back() = 1;
  }

  auto idx = 0u;
  if (type_sym.valid()) {
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

void AssemblerElfBase::except_add_empty_spec_action(
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

u32 AssemblerElfBase::except_type_idx_for_sym(const SymRef sym) noexcept {
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

std::vector<u8> AssemblerElfBase::build_object_file() noexcept {
  using namespace elf;

  // zero-terminate eh_frame
  // TODO(ts): this should probably go somewhere else
  if (auto &eh_frame = get_section(secref_eh_frame); !eh_frame.data.empty()) {
    eh_frame.data.resize(eh_frame.data.size() + 4);
  }

  std::vector<u8> out{};

  u32 obj_size = sizeof(Elf64_Ehdr) + sizeof(Elf64_Shdr) * sections.size() + 16;
  obj_size +=
      sizeof(Elf64_Sym) * (local_symbols.size() + global_symbols.size());
  obj_size += strtab.size();
  obj_size += SHSTRTAB.size() + shstrtab_extra.size();
  for (const auto &sec : sections) {
    obj_size += sec->data.size() + sec->relocs.size() * sizeof(Elf64_Rela) + 16;
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
    hdr->e_ident[7] = target_info.elf_osabi;
    hdr->e_ident[8] = 0;
    hdr->e_type = ET_REL;
    hdr->e_machine = target_info.elf_machine;
    hdr->e_version = EV_CURRENT;
    hdr->e_shoff = shdr_off;
    hdr->e_ehsize = sizeof(Elf64_Ehdr);
    hdr->e_shentsize = sizeof(Elf64_Shdr);
    hdr->e_shnum = sections.size();
    hdr->e_shstrndx = sec_idx(".shstrtab");
  }

  // .note.GNU-stack
  {
    auto *hdr = sec_hdr(sec_idx(".note.GNU-stack"));
    hdr->sh_name = sec_off(".note.GNU-stack");
    hdr->sh_type = SHT_PROGBITS;
    hdr->sh_offset = out.size(); // gcc seems to give empty sections an offset
    hdr->sh_addralign = 1;
  }

  // .symtab
  {
    const size_t local_cnt = local_symbols.size();
    const size_t global_cnt = global_symbols.size();
    const auto size =
        sizeof(Elf64_Sym) * (local_symbols.size() + global_symbols.size());
    const auto sh_off = out.size();
    out.insert(out.end(),
               reinterpret_cast<uint8_t *>(local_symbols.data()),
               reinterpret_cast<uint8_t *>(local_symbols.data() + local_cnt));
    // global symbols need to come after the local symbols
    out.insert(out.end(),
               reinterpret_cast<uint8_t *>(global_symbols.data()),
               reinterpret_cast<uint8_t *>(global_symbols.data() + global_cnt));

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
               reinterpret_cast<uint8_t *>(strtab.data()),
               reinterpret_cast<uint8_t *>(strtab.data() + strtab.size()));
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
    const auto size = SHSTRTAB.size() + shstrtab_extra.size();
    const auto pad = util::align_up(size, 8) - size;
    const auto sh_off = out.size();
    out.insert(
        out.end(),
        reinterpret_cast<const uint8_t *>(SHSTRTAB.data()),
        reinterpret_cast<const uint8_t *>(SHSTRTAB.data() + SHSTRTAB.size()));
    out.insert(out.end(),
               reinterpret_cast<const uint8_t *>(shstrtab_extra.data()),
               reinterpret_cast<const uint8_t *>(shstrtab_extra.data() +
                                                 shstrtab_extra.size()));
    out.resize(out.size() + pad);

    auto *hdr = sec_hdr(sec_idx(".shstrtab"));
    hdr->sh_name = sec_off(".shstrtab");
    hdr->sh_type = SHT_STRTAB;
    hdr->sh_offset = sh_off;
    hdr->sh_size = size;
    hdr->sh_addralign = 1;
  }

  for (size_t i = sec_count(); i < sections.size(); ++i) {
    DataSection &sec = *sections[i];
    sec.hdr.sh_offset = out.size();
    sec.hdr.sh_size = sec.size();
    *sec_hdr(i) = sec.hdr;

    const auto pad = util::align_up(sec.data.size(), 8) - sec.data.size();
    out.insert(out.end(), sec.data.begin(), sec.data.end());
    out.resize(out.size() + pad);

    if (i + 1 < sections.size() && sections[i + 1]->hdr.sh_type == SHT_RELA) {
      const auto rela_sh_off = out.size();
      if (size_t count = sec.relocs.size()) {
        out.insert(out.end(),
                   reinterpret_cast<uint8_t *>(sec.relocs.data()),
                   reinterpret_cast<uint8_t *>(sec.relocs.data() + count));

        // patch relocations in output
        std::span<Elf64_Rela> out_relocs{
            reinterpret_cast<Elf64_Rela *>(&out[rela_sh_off]), count};
        for (auto &reloc : out_relocs) {
          if (u32 sym = ELF64_R_SYM(reloc.r_info); !sym_is_local(SymRef{sym})) {
            auto ty = ELF64_R_TYPE(reloc.r_info);
            auto fixed_sym = (sym & ~0x8000'0000u) + local_symbols.size();
            reloc.r_info = ELF64_R_INFO(fixed_sym, ty);
          }
        }
      }


      auto *hdr = sec_hdr(i + 1);
      hdr->sh_name = sections[i + 1]->hdr.sh_name;
      hdr->sh_type = SHT_RELA;
      hdr->sh_flags = SHF_INFO_LINK;
      hdr->sh_offset = rela_sh_off;
      hdr->sh_size = sizeof(Elf64_Rela) * sec.relocs.size();
      hdr->sh_link = elf::sec_idx(".symtab");
      hdr->sh_info = i;
      hdr->sh_addralign = 8;
      hdr->sh_entsize = sizeof(Elf64_Rela);
      ++i;
    } else {
      assert(sec.relocs.empty() && "relocations in section without .rela");
    }
  }

  return out;
}

} // end namespace tpde
