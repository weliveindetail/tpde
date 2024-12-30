// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde/x64/AssemblerElfX64.hpp"

namespace tpde::x64 {

namespace {

// TODO: use static constexpr array in C++23.
static constexpr auto get_cie_initial_instrs() {
  std::array<u8, 32> data{};
  // the current frame setup does not have a constant offset from the FP
  // to the CFA so we need to encode that at the end
  // for now just encode the CFA before the first sub sp

  // we always emit a frame-setup so we can encode that in the CIE

  u8 *dst = data.data();
  // def_cfa rbp, 16 (CFA = rbp + 16)
  dst += AssemblerElfX64::write_eh_inst(
      dst, dwarf::DW_CFA_def_cfa, dwarf::x64::DW_reg_rbp, 16);
  // cfa_offset ra, 8 (ra = CFA - 8)
  dst += AssemblerElfX64::write_eh_inst(
      dst, dwarf::DW_CFA_offset, dwarf::x64::DW_reg_ra, 8);
  // cfa_offset rbp, 16 (rbp = CFA - 16)
  dst += AssemblerElfX64::write_eh_inst(
      dst, dwarf::DW_CFA_offset, dwarf::x64::DW_reg_rbp, 16);
  return std::make_pair(data, dst - data.data());
}

static constexpr auto cie_instrs = get_cie_initial_instrs();

} // namespace

const AssemblerElfBase::TargetInfo AssemblerElfX64::TARGET_INFO{
    .elf_osabi = ELFOSABI_SYSV,
    .elf_machine = EM_X86_64,

    .cie_return_addr_register = dwarf::x64::DW_reg_ra,
    .cie_instrs = {cie_instrs.first.data(), cie_instrs.second},

    .reloc_pc32 = R_X86_64_PC32,
};

} // end namespace tpde::x64
