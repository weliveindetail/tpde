// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/ValLocalIdx.hpp"
#include "tpde/base.hpp"
#include "tpde/util/misc.hpp"

namespace tpde {

struct Reg {
  u8 reg_id;

  explicit constexpr Reg(const u8 id) noexcept : reg_id(id) {}

  explicit constexpr Reg(const u64 id) noexcept : reg_id(static_cast<u8>(id)) {
    assert(id <= 255);
  }

  constexpr u8 id() const noexcept { return reg_id; }

  constexpr bool invalid() const noexcept { return reg_id == 0xFF; }

  constexpr bool valid() const noexcept { return reg_id != 0xFF; }

  constexpr static Reg make_invalid() noexcept { return Reg{(u8)0xFF}; }

  constexpr bool operator==(const Reg &other) const noexcept {
    return reg_id == other.reg_id;
  }
};

struct RegBank {
private:
  u8 bank;

public:
  constexpr RegBank() noexcept : bank(u8(-1)) {}

  constexpr explicit RegBank(u8 bank) noexcept : bank(bank) {}

  constexpr u8 id() const noexcept { return bank; }

  constexpr bool operator==(const RegBank &other) const noexcept {
    return bank == other.bank;
  }
};

template <unsigned NumBanks, unsigned RegsPerBank>
class RegisterFile {
public:
  static constexpr unsigned NumRegs = NumBanks * RegsPerBank;

  static_assert(RegsPerBank > 0 && (RegsPerBank & (RegsPerBank - 1)) == 0,
                "RegsPerBank must be a power of two");
  static_assert(NumRegs < Reg::make_invalid().id());
  static_assert(NumRegs <= 64);

  // later add the possibility for more than 64 registers
  // for architectures that require it
  using RegBitSet = u64;

  /// Registers that are generally allocatable and not reserved.
  RegBitSet allocatable = 0;
  /// Registers that are currently in use. Requires allocatable.
  RegBitSet used = 0;
  /// Registers that are currently unevictable. Requires used.
  RegBitSet fixed = 0;
  /// Registers that were clobbered at some point. Used to track registers that
  /// need to be saved/restored.
  RegBitSet clobbered = 0;
  std::array<u8, NumBanks> clocks{};

  struct Assignment {
    ValLocalIdx local_idx;
    u32 part;
  };

  std::array<Assignment, NumRegs> assignments;

  std::array<u8, NumRegs> lock_counts{};

  void reset() noexcept {
    used = {};
    fixed = {};
    clobbered = {};
    clocks = {};
    lock_counts = {};
  }

  [[nodiscard]] bool is_used(const Reg reg) const noexcept {
    assert(reg.id() < NumRegs);
    return (used & 1ull << reg.id()) != 0;
  }

  [[nodiscard]] bool is_fixed(const Reg reg) const noexcept {
    assert(reg.id() < NumRegs);
    return (fixed & 1ull << reg.id()) != 0;
  }

  [[nodiscard]] bool is_clobbered(const Reg reg) const noexcept {
    assert(reg.id() < NumRegs);
    return (clobbered & 1ull << reg.id()) != 0;
  }

  void mark_used(const Reg reg,
                 const ValLocalIdx local_idx,
                 const u32 part) noexcept {
    assert(reg.id() < NumRegs);
    assert(!is_used(reg));
    assert(!is_fixed(reg));
    assert(lock_counts[reg.id()] == 0);
    used |= (1ull << reg.id());
    assignments[reg.id()] = Assignment{.local_idx = local_idx, .part = part};
  }

  void update_reg_assignment(const Reg reg,
                             const ValLocalIdx local_idx,
                             const u32 part) noexcept {
    assert(is_used(reg));
    assignments[reg.id()].local_idx = local_idx;
    assignments[reg.id()].part = part;
  }

  void unmark_used(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    assert(is_used(reg));
    assert(!is_fixed(reg));
    assert(lock_counts[reg.id()] == 0);
    used &= ~(1ull << reg.id());
  }

  void mark_fixed(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    assert(is_used(reg));
    fixed |= (1ull << reg.id());
  }

  void unmark_fixed(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    assert(is_used(reg));
    assert(is_fixed(reg));
    fixed &= ~(1ull << reg.id());
  }

  void inc_lock_count(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    assert(is_used(reg));
    mark_fixed(reg);
    ++lock_counts[reg.id()];
  }

  /// Returns true if the last lock was released.
  bool dec_lock_count(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    assert(is_used(reg));
    assert(lock_counts[reg.id()] > 0);
    if (--lock_counts[reg.id()] == 0) {
      unmark_fixed(reg);
      return true;
    }
    return false;
  }

  /// Decrement lock count, and assert that the register is now unlocked
  void dec_lock_count_must_zero(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    assert(is_used(reg));
    assert(lock_counts[reg.id()] == 1);
    lock_counts[reg.id()] = 0;
    unmark_fixed(reg);
  }

  void mark_clobbered(const Reg reg) noexcept {
    assert(reg.id() < NumRegs);
    clobbered |= (1ull << reg.id());
  }

  [[nodiscard]] ValLocalIdx reg_local_idx(const Reg reg) const noexcept {
    assert(is_used(reg));
    return assignments[reg.id()].local_idx;
  }

  [[nodiscard]] u32 reg_part(const Reg reg) const noexcept {
    assert(is_used(reg));
    return assignments[reg.id()].part;
  }

  [[nodiscard]] util::BitSetIterator<> used_regs() const noexcept {
    return util::BitSetIterator<>{used};
  }

  [[nodiscard]] util::BitSetIterator<> used_nonfixed_regs() const noexcept {
    return util::BitSetIterator<>{used & ~fixed};
  }

  [[nodiscard]] Reg
      find_first_free_excluding(const RegBank bank,
                                const u64 exclusion_mask) const noexcept {
    // TODO(ts): implement preferred registers
    const RegBitSet free_bank = allocatable & ~used & bank_regs(bank);
    const RegBitSet selectable = free_bank & ~exclusion_mask;
    if (selectable == 0) {
      return Reg::make_invalid();
    }
    return Reg{static_cast<u8>(util::cnt_tz(selectable))};
  }

  [[nodiscard]] Reg
      find_first_nonfixed_excluding(const RegBank bank,
                                    const u64 exclusion_mask) const noexcept {
    // TODO(ts): implement preferred registers
    const RegBitSet allocatable_bank = allocatable & ~fixed & bank_regs(bank);
    const RegBitSet selectable = allocatable_bank & ~exclusion_mask;
    if (selectable == 0) {
      return Reg::make_invalid();
    }
    return Reg{util::cnt_tz(selectable)};
  }

  [[nodiscard]] Reg
      find_clocked_nonfixed_excluding(const RegBank bank,
                                      const u64 exclusion_mask) noexcept {
    const RegBitSet allocatable_bank = allocatable & ~fixed & bank_regs(bank);
    const RegBitSet selectable = allocatable_bank & ~exclusion_mask;
    if (selectable == 0) {
      return Reg::make_invalid();
    }

    auto clock = clocks[bank.id()];
    const u64 reg_id = (selectable >> clock)
                           ? util::cnt_tz(selectable >> clock) + clock
                           : util::cnt_tz(selectable);

    // always move clock to after the found reg_id
    clock = reg_id + 1;
    if (clock >= RegsPerBank) {
      clock = 0;
    }
    clocks[bank.id()] = clock;

    return Reg{static_cast<u8>(reg_id)};
  }

  [[nodiscard]] static RegBank reg_bank(const Reg reg) noexcept {
    return RegBank(reg.id() / RegsPerBank);
  }

  [[nodiscard]] static RegBitSet bank_regs(const RegBank bank) noexcept {
    assert(bank.id() <= 1);
    return ((1ull << RegsPerBank) - 1) << (bank.id() * RegsPerBank);
  }
};

} // namespace tpde
