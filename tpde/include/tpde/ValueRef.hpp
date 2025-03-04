// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <array>
#include <cstring>
#include <span>

namespace tpde {

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
struct CompilerBase<Adaptor, Derived, Config>::ValueRef {
  struct AssignmentData {
    bool is_special = false;
    ValLocalIdx local_idx = INVALID_VAL_LOCAL_IDX;
    ValueAssignment *assignment = nullptr;
  };
  static_assert(ValRefSpecialStruct<AssignmentData>);

  union {
    AssignmentData a;
    Derived::ValRefSpecial s;
  } state;

  CompilerBase *compiler;

  ValueRef(CompilerBase *compiler) noexcept
      : state{AssignmentData{}}, compiler(compiler) {}

  ValueRef(CompilerBase *compiler, ValLocalIdx local_idx) noexcept
      : state{AssignmentData{
                           .local_idx = local_idx,
                           .assignment = compiler->val_assignment(local_idx),
                           }
  }, compiler(compiler) {}

  template <typename... T>
  ValueRef(CompilerBase *compiler, T &&...args) noexcept
      : state{.s = typename Derived::ValRefSpecial(std::forward<T>(args)...)},
        compiler(compiler) {
    assert(state.a.is_special);
  }

  explicit ValueRef(const ValueRef &) = delete;

  ValueRef(ValueRef &&other) noexcept
      : state{other.state}, compiler(other.compiler) {
    other.state.a = AssignmentData{};
  }

  ~ValueRef() noexcept { reset(); }

  ValueRef &operator=(const ValueRef &) = delete;

  ValueRef &operator=(ValueRef &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    reset();
    assert(compiler == other.compiler);
    this->state = other.state;
    other.state.a = AssignmentData{};
    return *this;
  }

  bool has_assignment() const noexcept { return !state.a.is_special; }

  [[nodiscard]] ValueAssignment *assignment() const noexcept {
    assert(has_assignment());
    assert(state.a.assignment != nullptr);
    return state.a.assignment;
  }

  /// Increment the reference count artificially
  void inc_ref_count() noexcept {
    if (has_assignment()) {
      ++state.a.assignment->references_left;
    }
  }

  ValLocalIdx local_idx() const noexcept {
    assert(has_assignment());
    return state.a.local_idx;
  }

  u32 ref_count() const noexcept {
    assert(has_assignment());
    return state.a.assignment->references_left;
  }

  ValuePartRef part(unsigned part) noexcept {
    if (has_assignment()) {
      return ValuePartRef{compiler, local_idx(), part};
    }
    return compiler->derived()->val_part_ref_special(state.s, part);
  }

  /// Reset the reference to the value part
  void reset() noexcept;

  void reset_without_refcount() noexcept;
};

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValueRef::reset() noexcept {
  // TODO: decrement reference count when ValuePartRef doesn't track it.
  state.a = AssignmentData{};
}

template <IRAdaptor Adaptor, typename Derived, CompilerConfig Config>
void CompilerBase<Adaptor, Derived, Config>::ValueRef::
    reset_without_refcount() noexcept {
  state.a = AssignmentData{};
}
} // namespace tpde
