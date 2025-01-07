// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde/arm64/AssemblerElfA64.hpp"

namespace tpde::a64 {

namespace {

// TODO: use static constexpr array in C++23.
static constexpr auto get_cie_initial_instrs() {
  std::array<u8, 32> data{};
  // the current frame setup does not have a constant offset from the FP
  // to the CFA so we need to encode that at the end
  // for now just encode the CFA before the first sub sp

  // def_cfa sp, 0
  unsigned len = AssemblerElfA64::write_eh_inst(
      data.data(), dwarf::DW_CFA_def_cfa, dwarf::a64::DW_reg_sp, 0);
  return std::make_pair(data, len);
}

static constexpr auto cie_instrs = get_cie_initial_instrs();

} // namespace

const AssemblerElfBase::TargetInfo AssemblerElfA64::TARGET_INFO{
    .elf_osabi = ELFOSABI_SYSV,
    .elf_machine = EM_AARCH64,

    .cie_return_addr_register = dwarf::a64::DW_reg_lr,
    .cie_instrs = {cie_instrs.first.data(), cie_instrs.second},

    .reloc_pc32 = R_AARCH64_PREL32,
    .reloc_abs64 = R_AARCH64_ABS64,
};

} // end namespace tpde::a64
