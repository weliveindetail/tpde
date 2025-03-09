// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::AssignmentPartRef {
  ValueAssignment *assignment;
  u32 part;

  // note for how parts are structured:
  // |15|14|13|12|11|10|09|08|07|06|05|04|03|02|01|00|
  // |NP|   PS   |RV|VR|IM|FA|  bank  |    reg_id    |
  //                         |      full_reg_id      |
  //
  // NP: Is there a part following this one
  // PS: 1 << PS = part size (TODO(ts): maybe swap with NP so that it can be
  //     extracted easier?)
  // RV: Register Valid
  // VR: Variable Reference (i.e. is this a reference to a stack-slot/global
  //     that does not need to be spilled)
  // IM: Is the current register value not on the stack?
  // FA: Is the assignment a fixed assignment?
  //
  // RV + IM form a unit describing the following states:
  //  - !RV +  IM: value uninitialized (default state)
  //  -  RV +  IM: register dirty, must be spilled before evicting
  //  - !RV + !IM: register invalid, value stored only in stack slot
  //  -  RV + !IM: register identical to value in stack slot

  AssignmentPartRef(ValueAssignment *assignment, const u32 part)
      : assignment(assignment), part(part) {}

  void reset() noexcept {
    assignment->parts[part] = 0;
    set_modified(true);
  }

  [[nodiscard]] u8 bank() const noexcept {
    return (assignment->parts[part] >> 5) & 0b111;
  }

  void set_bank(const u8 bank) noexcept {
    assert(bank <= 0b111);
    auto data = assignment->parts[part] & ~0b1110'0000;
    data |= bank << 5;
    assignment->parts[part] = data;
  }

  [[nodiscard]] u8 full_reg_id() const noexcept {
    return assignment->parts[part] & 0xFF;
  }

  void set_full_reg_id(const u8 id) noexcept {
    assert(bank() == ((id >> 5) & 0b111));
    assignment->parts[part] = (assignment->parts[part] & 0xFF00) | id;
  }

  [[nodiscard]] bool modified() const noexcept {
    return (assignment->parts[part] & (1u << 9)) != 0;
  }

  void set_modified(const bool val) noexcept {
    if (val) {
      assignment->parts[part] |= (1u << 9);
    } else {
      assignment->parts[part] &= ~(1u << 9);
    }
  }

  [[nodiscard]] bool fixed_assignment() const noexcept {
    return (assignment->parts[part] & (1u << 8)) != 0;
  }

  void set_fixed_assignment(const bool val) noexcept {
    if (val) {
      assignment->parts[part] |= (1u << 8);
    } else {
      assignment->parts[part] &= ~(1u << 8);
    }
  }

  [[nodiscard]] bool variable_ref() const noexcept {
    return (assignment->parts[part] & (1u << 10)) != 0;
  }

  void set_variable_ref(const bool val) noexcept {
    if (val) {
      assignment->parts[part] |= (1u << 10);
    } else {
      assignment->parts[part] &= ~(1u << 10);
    }
  }

  [[nodiscard]] bool register_valid() const noexcept {
    return (assignment->parts[part] & (1u << 11)) != 0;
  }

  void set_register_valid(const bool val) noexcept {
    if (val) {
      assignment->parts[part] |= (1u << 11);
    } else {
      assignment->parts[part] &= ~(1u << 11);
    }
  }

  [[nodiscard]] bool stack_valid() const noexcept {
    return (assignment->parts[part] & (1u << 9)) == 0;
  }

  void set_stack_valid() noexcept { set_modified(false); }

  [[nodiscard]] u32 part_size() const noexcept {
    return 1u << ((assignment->parts[part] >> 12) & 0b111);
  }

  void set_part_size(const u32 part_size) noexcept {
    assert((part_size & (part_size - 1)) == 0);
    const u32 shift = util::cnt_tz(part_size);
    assert(shift <= 0b111);
    auto data = assignment->parts[part] & ~(0b111 << 12);
    data |= (shift << 12);
    assignment->parts[part] = data;
  }

  [[nodiscard]] bool has_next_part() const noexcept {
    return (assignment->parts[part] & (1u << 15)) != 0;
  }

  void set_has_next_part(const bool val) noexcept {
    if (val) {
      assignment->parts[part] |= (1u << 15);
    } else {
      assignment->parts[part] &= ~(1u << 15);
    }
  }

  [[nodiscard]] u32 frame_off() const noexcept {
    if constexpr (Config::FRAME_INDEXING_NEGATIVE) {
      return assignment->frame_off - assignment->max_part_size * part;
    } else {
      return assignment->frame_off + assignment->max_part_size * part;
    }
  }

  [[nodiscard]] u32 part_off() const noexcept {
    return assignment->max_part_size * part;
  }

  void spill_if_needed(CompilerBase *compiler) noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::AssignmentPartRef::spill_if_needed(
    CompilerBase *compiler) noexcept {
  assert(this->register_valid());

  if (this->variable_ref()) {
    // variable references don't need to be spilled
    return;
  }

  if (!this->modified()) {
    return;
  }

  compiler->derived()->spill_reg(
      AsmReg{this->full_reg_id()}, this->frame_off(), this->part_size());

  this->set_modified(false);
}
} // namespace tpde
