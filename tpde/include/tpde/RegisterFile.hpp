// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {

struct AsmRegBase {
  u8 reg_id;

  explicit constexpr AsmRegBase(const u8 id) noexcept : reg_id(id) {}

  explicit constexpr AsmRegBase(const u64 id) noexcept
      : reg_id(static_cast<u8>(id)) {
    assert(id <= 255);
  }

  constexpr u8 id() const noexcept { return reg_id; }

  constexpr bool invalid() const noexcept { return reg_id == 0xFF; }

  constexpr bool valid() const noexcept { return reg_id != 0xFF; }

  constexpr static AsmRegBase make_invalid() noexcept {
    return AsmRegBase{(u8)0xFF};
  }

  constexpr bool operator==(const AsmRegBase &other) const noexcept {
    return reg_id == other.reg_id;
  }
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::RegisterFile {
  // later add the possibility for more than 64 registers
  // for architectures that require it

  using Reg = typename Config::AsmReg;
  using RegBitSet = u64;

  static constexpr size_t MAX_ID = 63;

  /// Registers that are generally allocatable and not reserved.
  RegBitSet allocatable = 0;
  /// Registers that are currently in use. Requires allocatable.
  RegBitSet used = 0;
  /// Registers that are currently unevictable. Requires used.
  RegBitSet fixed = 0;
  /// Registers that were clobbered at some point. Used to track registers that
  /// need to be saved/restored.
  RegBitSet clobbered = 0;
  u32 clocks[2] = {};

  struct Assignment {
    ValLocalIdx local_idx;
    u32 part;
    u32 lock_count; // TODO(ts): put this somewhere else since it's probably
                    // only used for a few assignments at a time
  };

  std::array<Assignment, 64> assignments;

  [[nodiscard]] bool is_used(const Reg reg) const noexcept {
    assert(reg.id() < 64);
    return (used & 1ull << reg.id()) != 0;
  }

  [[nodiscard]] bool is_fixed(const Reg reg) const noexcept {
    assert(reg.id() < 64);
    return (fixed & 1ull << reg.id()) != 0;
  }

  [[nodiscard]] bool is_clobbered(const Reg reg) const noexcept {
    assert(reg.id() < 64);
    return (clobbered & 1ull << reg.id()) != 0;
  }

  void mark_used(const Reg reg,
                 const ValLocalIdx local_idx,
                 const u32 part) noexcept {
    assert(reg.id() < 64);
    assert(!is_used(reg));
    assert(!is_fixed(reg));
    used |= (1ull << reg.id());
    assignments[reg.id()] =
        Assignment{.local_idx = local_idx, .part = part, .lock_count = 0};
  }

  void update_reg_assignment(const Reg reg,
                             const ValLocalIdx local_idx,
                             const u32 part) noexcept {
    assert(is_used(reg));
    assert(assignments[reg.id()].lock_count == 0);
    assignments[reg.id()].local_idx = local_idx;
    assignments[reg.id()].part = part;
  }

  void unmark_used(const Reg reg) noexcept {
    assert(reg.id() < 64);
    assert(is_used(reg));
    assert(!is_fixed(reg));
    used &= ~(1ull << reg.id());
  }

  void mark_fixed(const Reg reg) noexcept {
    assert(reg.id() < 64);
    assert(is_used(reg));
    fixed |= (1ull << reg.id());
  }

  void unmark_fixed(const Reg reg) noexcept {
    assert(reg.id() < 64);
    assert(is_used(reg));
    assert(is_fixed(reg));
    fixed &= ~(1ull << reg.id());
  }

  u32 inc_lock_count(const Reg reg) noexcept {
    assert(reg.id() < 64);
    assert(is_used(reg));
    return ++assignments[reg.id()].lock_count;
  }

  u32 dec_lock_count(const Reg reg) noexcept {
    assert(reg.id() < 64);
    assert(is_used(reg));
    assert(assignments[reg.id()].lock_count > 0);
    return --assignments[reg.id()].lock_count;
  }

  void mark_clobbered(const Reg reg) noexcept {
    assert(reg.id() < 64);
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
      find_first_free_excluding(const u8 bank,
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
      find_first_nonfixed_excluding(const u8 bank,
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
      find_clocked_nonfixed_excluding(const u8 bank,
                                      const u64 exclusion_mask) noexcept {
    const RegBitSet allocatable_bank = allocatable & ~fixed & bank_regs(bank);
    const RegBitSet selectable = allocatable_bank & ~exclusion_mask;
    if (selectable == 0) {
      return Reg::make_invalid();
    }

    auto clock = clocks[bank];
    const u64 reg_id = (selectable >> clock)
                           ? util::cnt_tz(selectable >> clock) + clock
                           : util::cnt_tz(selectable);

    // always move clock to after the found reg_id
    clock = reg_id + 1;
    if (clock >= 32) {
      clock = 0;
    }
    clocks[bank] = clock;

    return Reg{static_cast<u8>(reg_id)};
  }

  [[nodiscard]] static u8 reg_bank(const Reg reg) noexcept {
    return (reg.id() >> 5);
  }

  [[nodiscard]] static RegBitSet bank_regs(const u8 bank) noexcept {
    assert(bank <= 1);
    return 0xFFFF'FFFFull << (bank * 32);
  }
};
} // namespace tpde
