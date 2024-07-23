// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <elf.h>
#include <type_traits>

#include "base.hpp"
#include "util/misc.hpp"

namespace tpde {

namespace dwarf {
// DWARF constants
constexpr u8 DW_CFA_nop        = 0;
constexpr u8 DW_EH_PE_pcrel    = 0x10;
constexpr u8 DW_EH_PE_indirect = 0x80;
constexpr u8 DW_EH_PE_sdata4   = 0x0b;

constexpr u8 DW_CFA_def_cfa = 0x0c;
constexpr u8 DW_CFA_offset  = 0x80;

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
constexpr u8 DW_reg_r8  = 8;
constexpr u8 DW_reg_r9  = 9;
constexpr u8 DW_reg_r10 = 10;
constexpr u8 DW_reg_r11 = 11;
constexpr u8 DW_reg_r12 = 12;
constexpr u8 DW_reg_r13 = 13;
constexpr u8 DW_reg_r14 = 14;
constexpr u8 DW_reg_r15 = 15;
constexpr u8 DW_reg_ra  = 16;
} // namespace x64

} // namespace dwarf

/// AssemblerElf contains the architecture-independent logic to emit
/// ELF object files (currently linux-specific) which is then extended by
/// AssemblerElfX64 or AssemblerElfA64
template <typename Derived>
struct AssemblerElf {
    // TODO(ts): 32 bit version?
    struct DataSection {
        std::vector<u8>         data;
        std::vector<Elf64_Rela> relocs;
        std::vector<u32>        relocs_to_patch;
    };

    enum class SymRef : u32 {
    };
    static constexpr SymRef INVALID_SYM_REF = static_cast<SymRef>(~0u);

    std::vector<Elf64_Sym> global_symbols, local_symbols;

    std::vector<char> strtab;
    DataSection sec_text, sec_data, sec_rodata, sec_relrodata, sec_init_array,
        sec_fini_array;

    /// Unwind Info
    DataSection sec_eh_frame;

    /// The current write pointer for the text section
    u8 *text_write_ptr   = nullptr;
    u8 *text_reserve_end = nullptr;

    /// Is the objective(heh) to generate an object file or to map into memory?
    bool   generating_object;
    /// The current function
    SymRef cur_func = INVALID_SYM_REF;

#ifdef TPDE_ASSERTS
    bool currently_in_func = false;
#endif

    static constexpr size_t RESERVED_SYM_COUNT = 3;

    // TODO(ts): add option to emit multiple text sections (e.g. on ARM)
    static constexpr size_t TEXT_SYM_IDX         = 1;
    static constexpr size_t EXCEPT_TABLE_SYM_IDX = 2;

    static constexpr SymRef TEXT_SYM_REF = static_cast<SymRef>(TEXT_SYM_IDX);

    explicit AssemblerElf(const bool generating_object)
        : generating_object(generating_object) {
        static_assert(std::is_base_of_v<AssemblerElf, Derived>);
        strtab.push_back('\0');

        local_symbols.resize(RESERVED_SYM_COUNT);
        eh_init_cie();
    }

    Derived *derived() noexcept { return static_cast<Derived *>(this); }

    void start_func(SymRef func) noexcept;

  protected:
    void end_func() noexcept;

  public:
    [[nodiscard]] SymRef sym_add_undef(std::string_view name,
                                       bool             local = false);

    [[nodiscard]] SymRef
        sym_predef_func(std::string_view name, bool local, bool weak);

    /// Align the text write pointer to 16 bytes
    void text_align_16() noexcept;

    /// \returns The current used space in the text section
    [[nodiscard]] u32 text_cur_off() const noexcept;

    /// Make sure that text_write_ptr can be safely incremented by size
    void text_ensure_space(u32 size) noexcept;

    void reset() noexcept;

    std::vector<u8> build_object_file();

    // TODO(ts): func to map into memory

    void reloc_text(SymRef sym, u32 type, u64 offset, i64 addend) noexcept;

    void reloc_sec(DataSection &sec,
                   SymRef       sym,
                   u32          type,
                   u64          offset,
                   i64          addend) noexcept;

    void eh_align_frame() noexcept;
    void eh_write_inst(u8 opcode, u64 arg) noexcept;
    void eh_write_inst(u8 opcode, u64 first_arg, u64 second_arg) noexcept;
    void eh_write_uleb(u64 value) noexcept;

    void eh_init_cie() noexcept;
    u32  eh_write_fde_start() noexcept;
    void eh_write_fde_len(u32 fde_off) noexcept;

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
};

template <typename Derived>
void AssemblerElf<Derived>::start_func(const SymRef func) noexcept {
    cur_func = func;

    text_align_16();
    auto *elf_sym     = sym_ptr(func);
    elf_sym->st_value = text_cur_off();

#ifdef TPDE_ASSERTS
    currently_in_func = true;
#endif
}

template <typename Derived>
void AssemblerElf<Derived>::end_func() noexcept {
    auto *elf_sym    = sym_ptr(cur_func);
    elf_sym->st_size = text_cur_off() - elf_sym->st_value;

#ifdef TPDE_ASSERTS
    currently_in_func = false;
#endif
}

template <typename Derived>
typename AssemblerElf<Derived>::SymRef
    AssemblerElf<Derived>::sym_add_undef(const std::string_view name,
                                         const bool             local) {
    size_t strOff = 0;
    if (!name.empty()) {
        strOff = strtab.size();
        strtab.insert(strtab.end(), name.begin(), name.end());
        strtab.emplace_back('\0');
    }

    u8 info;
    if (local) {
        info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);
    } else {
        info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    }

    auto sym = Elf64_Sym{.st_name  = static_cast<Elf64_Word>(strOff),
                         .st_info  = info,
                         .st_other = STV_DEFAULT,
                         .st_shndx = SHN_UNDEF,
                         .st_value = 0,
                         .st_size  = 0};

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

    const auto sym =
        Elf64_Sym{.st_name  = static_cast<Elf64_Word>(strOff),
                  .st_info  = info,
                  .st_other = STV_DEFAULT,
                  .st_shndx = 1, // .text is always the first section
                  .st_value = 0,
                  .st_size  = 0};

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
void AssemblerElf<Derived>::text_align_16() noexcept {
    text_ensure_space(16);
    text_write_ptr = reinterpret_cast<u8 *>(
        util::align_up(reinterpret_cast<uintptr_t>(text_write_ptr), 16));
}

template <typename Derived>
u32 AssemblerElf<Derived>::text_cur_off() const noexcept {
    return static_cast<u32>(text_write_ptr - sec_text.data.data());
}

template <typename Derived>
void AssemblerElf<Derived>::text_ensure_space(u32 size) noexcept {
    if (text_reserve_end - text_write_ptr >= size) [[likely]] {
        return;
    }

    // TODO(ts): include veneer handling on architectures that require it

    size             = util::align_up(size, 16 * 1024);
    const size_t off = text_write_ptr - sec_text.data.data();
    sec_text.data.resize(sec_text.data.size() + size);

    text_write_ptr   = sec_text.data.data() + off;
    text_reserve_end = sec_text.data.data() + sec_text.data.size();
}

template <typename Derived>
void AssemblerElf<Derived>::reset() noexcept {
    global_symbols.clear();
    local_symbols.clear();
    strtab.clear();
    sec_text       = {};
    sec_data       = {};
    sec_rodata     = {};
    sec_relrodata  = {};
    sec_init_array = {};
    sec_fini_array = {};
    sec_eh_frame   = {};
    text_write_ptr = text_reserve_end = nullptr;
    cur_func                          = INVALID_SYM_REF;

    eh_init_cie();
}

// TODO(ts): maybe just outsource this to a helper func that can live in a cpp
// file?
namespace elf {
// TODO(ts): this is linux-specific, no?
constexpr static std::span<const char> SECTION_NAMES = {
    "\0" // first section is the null-section
    ".text\0"
    ".note.GNU-stack\0"
    ".eh_frame\0"
    ".rela.eh_frame\0"
    ".symtab\0"
    ".strtab\0"
    ".shstrtab\0"
    ".data\0"
    ".rodata\0"
    ".rela.text\0"
    ".data.rel.ro\0"
    ".rela.data.rel.ro\0"
    ".rela.data\0"
    //".gcc_except_table\0"
    //".rela.gcc_except_table\0"
    ".init_array\0"
    ".rela.init_array\0"
    ".fini_array\0"
    ".rela.fini_array\0"};

consteval static u32 sec_idx(const std::string_view name) {
    // skip the first null string
    const char *data     = SECTION_NAMES.data() + 1;
    u32         idx      = 1;
    auto        sec_name = std::string_view{data};
    while (!sec_name.empty()) {
        if (sec_name == name) {
            return idx;
        }

        ++idx;
        data     += sec_name.size() + 1;
        sec_name  = std::string_view{data};
    }

    throw std::invalid_argument{"unknown section name"};
}

consteval static u32 sec_off(const std::string_view name) {
    // skip the first null string
    const char *data     = SECTION_NAMES.data() + 1;
    auto        sec_name = std::string_view{data};
    while (!sec_name.empty()) {
        if (sec_name == name) {
            return sec_name.data() - SECTION_NAMES.data();
        }

        data     += sec_name.size() + 1;
        sec_name  = std::string_view{data};
    }

    throw std::invalid_argument{"unknown section name"};
}

consteval static u32 sec_count() {
    // skip the first null string
    const char *data     = SECTION_NAMES.data() + 1;
    u32         idx      = 1;
    auto        sec_name = std::string_view{data};
    while (!sec_name.empty()) {
        ++idx;
        data     += sec_name.size() + 1;
        sec_name  = std::string_view{data};
    }

    return idx;
}

} // namespace elf

template <typename Derived>
std::vector<u8> AssemblerElf<Derived>::build_object_file() {
    using namespace elf;

    std::vector<u8> out{};

    // special symbols here
    {
        std::string_view name    = ".text";
        const auto       str_off = strtab.size();
        strtab.insert(strtab.end(), name.begin(), name.end());
        strtab.emplace_back('\0');

        auto &sym    = local_symbols[TEXT_SYM_IDX];
        sym.st_name  = static_cast<Elf64_Word>(str_off);
        sym.st_info  = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        sym.st_other = STV_DEFAULT;
        sym.st_shndx = sec_idx(".text");
        sym.st_value = 0;
        sym.st_size  = 0;
    }
    /*{
        std::string_view name    = ".gcc_except_table";
        const auto       str_off = strtab.size();
        strtab.insert(strtab.end(), name.begin(), name.end());
        strtab.emplace_back('\0');

        auto &sym    = local_symbols[2];
        sym.st_name  = static_cast<Elf64_Word>(str_off);
        sym.st_info  = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        sym.st_other = STV_DEFAULT;
        sym.st_shndx = sec_idx(".gcc_except_table");
        sym.st_value = 0;
        sym.st_size  = 0;
    }*/

    u32 obj_size = sizeof(Elf64_Shdr) + sizeof(Elf64_Shdr) * sec_count();
    obj_size +=
        sizeof(Elf64_Sym) * (local_symbols.size() + global_symbols.size());
    obj_size += strtab.size();
    obj_size += SECTION_NAMES.size();
    obj_size +=
        sec_text.data.size() + sec_text.relocs.size() + sizeof(Elf64_Rela);
    obj_size +=
        sec_data.data.size() + sec_data.relocs.size() + sizeof(Elf64_Rela);
    obj_size +=
        sec_rodata.data.size() + sec_rodata.relocs.size() + sizeof(Elf64_Rela);
    obj_size += sec_relrodata.data.size() + sec_relrodata.relocs.size()
                + sizeof(Elf64_Rela);
    obj_size += sec_init_array.data.size() + sec_init_array.relocs.size()
                + sizeof(Elf64_Rela);
    obj_size += sec_fini_array.data.size() + sec_fini_array.relocs.size()
                + sizeof(Elf64_Rela);
    obj_size += sec_eh_frame.data.size() + sec_eh_frame.relocs.size()
                + sizeof(Elf64_Rela);
    out.reserve(obj_size);

    out.resize(sizeof(Elf64_Ehdr));

    const auto shdr_off = out.size();
    out.resize(out.size() + sizeof(Elf64_Shdr) * sec_count());

    const auto sec_hdr = [shdr_off, &out](const u32 idx) {
        return reinterpret_cast<Elf64_Shdr *>(out.data() + shdr_off) + idx;
    };

    {
        auto *hdr = reinterpret_cast<Elf64_Ehdr *>(out.data());

        hdr->e_ident[0]  = ELFMAG0;
        hdr->e_ident[1]  = ELFMAG1;
        hdr->e_ident[2]  = ELFMAG2;
        hdr->e_ident[3]  = ELFMAG3;
        hdr->e_ident[4]  = ELFCLASS64;
        hdr->e_ident[5]  = ELFDATA2LSB;
        hdr->e_ident[6]  = EV_CURRENT;
        hdr->e_ident[7]  = Derived::ELF_OS_ABI;
        hdr->e_ident[8]  = 0;
        hdr->e_type      = ET_REL;
        hdr->e_machine   = Derived::ELF_MACHINE;
        hdr->e_version   = EV_CURRENT;
        hdr->e_shoff     = shdr_off;
        hdr->e_ehsize    = sizeof(Elf64_Ehdr);
        hdr->e_shentsize = sizeof(Elf64_Shdr);
        hdr->e_shnum     = sec_count();
        hdr->e_shstrndx  = sec_idx(".shstrtab");
    }

    const auto write_reloc_sec = [this, &out, &sec_hdr](DataSection &sec,
                                                        const u32    sec_idx,
                                                        const u32    sec_off,
                                                        const u32    info_idx) {
        // patch relocations
        for (auto idx : sec.relocs_to_patch) {
            auto ty                 = ELF64_R_TYPE(sec.relocs[idx].r_info);
            auto sym                = ELF64_R_SYM(sec.relocs[idx].r_info);
            sym                    += local_symbols.size();
            sec.relocs[idx].r_info  = ELF64_R_INFO(sym, ty);
        }

        const auto size   = sizeof(Elf64_Rela) * sec.relocs.size();
        const auto sh_off = out.size();
        out.insert(out.end(),
                   reinterpret_cast<uint8_t *>(&*sec.relocs.begin()),
                   reinterpret_cast<uint8_t *>(&*sec.relocs.end()));

        auto *hdr         = sec_hdr(sec_idx);
        hdr->sh_name      = sec_off;
        hdr->sh_type      = SHT_RELA;
        hdr->sh_flags     = SHF_INFO_LINK;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_link      = elf::sec_idx(".symtab");
        hdr->sh_info      = info_idx;
        hdr->sh_addralign = 8;
        hdr->sh_entsize   = sizeof(Elf64_Rela);
    };

    // .text
    {
        const auto size   = util::align_up(sec_text.data.size(), 16);
        const auto pad    = size - sec_text.data.size();
        const auto sh_off = out.size();
        out.insert(out.end(), sec_text.data.begin(), sec_text.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".text"));
        hdr->sh_name      = sec_off(".text");
        hdr->sh_type      = SHT_PROGBITS;
        hdr->sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 16;
    }

    // .note.GNU-stack
    {
        auto *hdr    = sec_hdr(sec_idx(".note.GNU-stack"));
        hdr->sh_name = sec_off(".note.GNU-stack");
        hdr->sh_type = SHT_PROGBITS;
        hdr->sh_offset =
            out.size(); // gcc seems to give empty sections an offset
        hdr->sh_addralign = 1;
    }

    // .eh_frame
    {
        const auto size   = util::align_up(sec_eh_frame.data.size(), 8);
        const auto pad    = size - sec_eh_frame.data.size();
        const auto sh_off = out.size();
        out.insert(
            out.end(), sec_eh_frame.data.begin(), sec_eh_frame.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".eh_frame"));
        hdr->sh_name      = sec_off(".eh_frame");
        hdr->sh_type      = SHT_PROGBITS;
        hdr->sh_flags     = SHF_ALLOC;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
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

        auto *hdr         = sec_hdr(sec_idx(".symtab"));
        hdr->sh_name      = sec_off(".symtab");
        hdr->sh_type      = SHT_SYMTAB;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_link      = sec_idx(".strtab");
        hdr->sh_info      = local_symbols.size(); // first non-local symbol idx
        hdr->sh_addralign = 8;
        hdr->sh_entsize   = sizeof(Elf64_Sym);
    }

    // .strtab
    {
        const auto size   = util::align_up(strtab.size(), 8);
        const auto pad    = size - strtab.size();
        const auto sh_off = out.size();
        out.insert(out.end(),
                   reinterpret_cast<uint8_t *>(&*strtab.begin()),
                   reinterpret_cast<uint8_t *>(&*strtab.end()));
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".strtab"));
        hdr->sh_name      = sec_off(".strtab");
        hdr->sh_type      = SHT_STRTAB;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 1;
    }

    // .shstrtab
    {
        const auto size   = util::align_up(SECTION_NAMES.size(), 8);
        const auto pad    = size - SECTION_NAMES.size();
        const auto sh_off = out.size();
        out.insert(out.end(),
                   reinterpret_cast<const uint8_t *>(SECTION_NAMES.data()),
                   reinterpret_cast<const uint8_t *>(SECTION_NAMES.data()
                                                     + SECTION_NAMES.size()));
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".shstrtab"));
        hdr->sh_name      = sec_off(".shstrtab");
        hdr->sh_type      = SHT_STRTAB;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 1;
    }

    // .data
    {
        const auto size   = util::align_up(sec_data.data.size(), 16);
        const auto pad    = size - sec_data.data.size();
        const auto sh_off = out.size();
        out.insert(out.end(), sec_data.data.begin(), sec_data.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".data"));
        hdr->sh_name      = sec_off(".data");
        hdr->sh_type      = SHT_PROGBITS;
        hdr->sh_flags     = SHF_ALLOC | SHF_WRITE;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 16;
    }

    // .rodata
    {
        const auto size   = util::align_up(sec_rodata.data.size(), 16);
        const auto pad    = size - sec_rodata.data.size();
        const auto sh_off = out.size();
        out.insert(out.end(), sec_rodata.data.begin(), sec_rodata.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".rodata"));
        hdr->sh_name      = sec_off(".rodata");
        hdr->sh_type      = SHT_PROGBITS;
        hdr->sh_flags     = SHF_ALLOC;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 16;
    }

    // .rela.text
    write_reloc_sec(sec_text,
                    sec_idx(".rela.text"),
                    sec_off(".rela.text"),
                    sec_idx(".text"));

    // .data.rel.ro
    {
        const auto size   = util::align_up(sec_relrodata.data.size(), 16);
        const auto pad    = size - sec_relrodata.data.size();
        const auto sh_off = out.size();
        out.insert(
            out.end(), sec_relrodata.data.begin(), sec_relrodata.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".data.rel.ro"));
        hdr->sh_name      = sec_off(".data.rel.ro");
        hdr->sh_type      = SHT_PROGBITS;
        hdr->sh_flags     = SHF_ALLOC | SHF_WRITE;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 16;
    }

    // .rela.data.rel.ro
    write_reloc_sec(sec_relrodata,
                    sec_idx(".rela.data.rel.ro"),
                    sec_off(".rela.data.rel.ro"),
                    sec_idx(".data.rel.ro"));

    // .rela.data
    write_reloc_sec(sec_data,
                    sec_idx(".rela.data"),
                    sec_off(".rela.data"),
                    sec_idx(".data"));

    // .init_array
    {
        const auto size   = util::align_up(sec_init_array.data.size(), 8);
        const auto pad    = size - sec_init_array.data.size();
        const auto sh_off = out.size();
        out.insert(
            out.end(), sec_init_array.data.begin(), sec_init_array.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".init_array"));
        hdr->sh_name      = sec_off(".init_array");
        hdr->sh_type      = SHT_INIT_ARRAY;
        hdr->sh_flags     = SHF_ALLOC | SHF_WRITE;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 8;
    }

    // .rela.init_array
    write_reloc_sec(sec_init_array,
                    sec_idx(".rela.init_array"),
                    sec_off(".rela.init_array"),
                    sec_idx(".init_array"));


    // .fini_array
    {
        const auto size   = util::align_up(sec_fini_array.data.size(), 8);
        const auto pad    = size - sec_fini_array.data.size();
        const auto sh_off = out.size();
        out.insert(
            out.end(), sec_fini_array.data.begin(), sec_fini_array.data.end());
        out.resize(out.size() + pad);

        auto *hdr         = sec_hdr(sec_idx(".fini_array"));
        hdr->sh_name      = sec_off(".fini_array");
        hdr->sh_type      = SHT_INIT_ARRAY;
        hdr->sh_flags     = SHF_ALLOC | SHF_WRITE;
        hdr->sh_offset    = sh_off;
        hdr->sh_size      = size;
        hdr->sh_addralign = 8;
    }

    // .rela.fini_array
    write_reloc_sec(sec_fini_array,
                    sec_idx(".rela.fini_array"),
                    sec_off(".rela.fini_array"),
                    sec_idx(".fini_array"));

    return out;
}

template <typename Derived>
void AssemblerElf<Derived>::reloc_text(const SymRef sym,
                                       const u32    type,
                                       const u64    offset,
                                       const i64    addend) noexcept {
    reloc_sec(sec_text, sym, type, offset, addend);
}

template <typename Derived>
void AssemblerElf<Derived>::reloc_sec(DataSection &sec,
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

template <typename Derived>
void AssemblerElf<Derived>::eh_align_frame() noexcept {
    while ((sec_eh_frame.data.size() & 7) != 0) {
        sec_eh_frame.data.push_back(dwarf::DW_CFA_nop);
    }
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_inst(const u8  opcode,
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
void AssemblerElf<Derived>::eh_write_inst(const u8  opcode,
                                          const u64 first_arg,
                                          const u64 second_arg) noexcept {
    eh_write_inst(opcode, first_arg);
    eh_write_uleb(second_arg);
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_uleb(u64 value) noexcept {
    while (true) {
        u8 write   = value & 0b0111'1111;
        value    >>= 7;
        if (value == 0) {
            sec_eh_frame.data.push_back(write);
            break;
        }
        sec_eh_frame.data.push_back(write | 0b1000'0000);
    }
}

template <typename Derived>
void AssemblerElf<Derived>::eh_init_cie() noexcept {
    // write out the initial CIE
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

    // initial CIE has no personality
    auto off = data.size();
    data.resize(data.size() + 17);

    // id is 0 for CIEs

    // version is 1
    data[off + 8] = 1;

    // augmentation is "zR" for a CIE with no personality meaning there is the
    // augmentation_data_len and ptr_size field
    data[off + 9]  = 'z';
    data[off + 10] = 'R';

    // code_alignment_factor is 1
    data[off + 12] = 1;

    // data_alignment_factor is 127 representing -1
    data[off + 13] = 127;

    // return_addr_register is defined by the derived impl
    data[off + 14] = Derived::DWARF_EH_RETURN_ADDR_REGISTER;

    // augmentation_data_len is 1 when no personality is present
    data[off + 15] = 1;

    // fde_ptr_encoding is a 4-byte signed pc-relative address
    data[off + 16] = dwarf::DW_EH_PE_sdata4 | dwarf::DW_EH_PE_pcrel;

    derived()->eh_write_initial_cie_instrs();

    eh_align_frame();

    // patch size of CIE (length is not counted)
    *reinterpret_cast<u32 *>(data.data() + off) =
        data.size() - off - sizeof(u32);
}

template <typename Derived>
u32 AssemblerElf<Derived>::eh_write_fde_start() noexcept {
    auto      &data    = sec_eh_frame.data;
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

    // for now no personality
    data.resize(data.size() + 18);

    // we encode length later

    // id is the offset from the CIE to the id field
    *reinterpret_cast<u32 *>(data.data() + fde_off + 4) =
        fde_off + 4; // currently we only have one CIE

    // func_start will be relocated by the arch impl

    // func_size
    auto *func_sym                                       = sym_ptr(cur_func);
    *reinterpret_cast<i32 *>(data.data() + fde_off + 12) = func_sym->st_size;

    // augmentation_data_len is 0 with no personality

    return fde_off;
}

template <typename Derived>
void AssemblerElf<Derived>::eh_write_fde_len(const u32 fde_off) noexcept {
    eh_align_frame();

    const u32 len = sec_eh_frame.data.size() - fde_off - sizeof(u32);
    *reinterpret_cast<u32 *>(sec_eh_frame.data.data() + fde_off) = len;
}
} // namespace tpde
