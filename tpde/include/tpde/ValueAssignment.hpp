// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/ValLocalIdx.hpp"
#include "tpde/base.hpp"

#include <cassert>

namespace tpde {

// disable Wpedantic here since these are compiler extensions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

// GCC < 15 does not support the flexible array member in the union
// see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53548
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ < 15
  #define GNU_ZERO 0
#else
  #define GNU_ZERO
#endif

// TODO(ts): add option to use fixed size assignments if there is no need
// for arbitrarily many parts since that comes with a 1-3% cost in
// compile-time
struct ValueAssignment {
  using Part = u16;

  union {
    /// Offset from the frame pointer.
    i32 frame_off;
    /// For variable-references, frame_off is unused so it can be used
    /// to store an index into a custom structure in case there is
    /// special handling for variable references
    u32 var_ref_custom_idx;
  };

  u32 part_count;

  // we want tight packing and with this construct we get a base size of
  // 16 bytes for values with only one part (the majority)
  union {
    ValueAssignment *next_free_list_entry;

    struct {
      union {
        ValLocalIdx next_delayed_free_entry;
        u32 references_left;
      };
      u8 max_part_size;

      /// Whether the assignment is in a delayed free list and the reference
      /// count is therefore invalid (zero). Set/used in debug builds only to
      /// catch use-after-frees.
      bool pending_free : 1;

      /// Whether the assignment is a single-part variable reference.
      bool variable_ref : 1;

      /// Whether to delay the free when the reference count reaches zero.
      /// (This is liveness.last_full, copied here for faster access).
      bool delay_free : 1;

      // TODO: get the type of parts from Derived
      union {
        Part first_part;
        Part parts[GNU_ZERO];
      };
    };
  };

  u32 size() const noexcept {
    assert(!variable_ref && "variable-ref has no allocation size");
    return part_count * max_part_size;
  }
};

#undef GNU_ZERO
#pragma GCC diagnostic pop

} // namespace tpde
