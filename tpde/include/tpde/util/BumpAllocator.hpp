// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/util/SmallVector.hpp"
#include "tpde/util/misc.hpp"

#include <algorithm>
#include <cstddef>
#include <new>

namespace tpde::util {

template <size_t SlabSize = 4096>
class BumpAllocator {
  uintptr_t cur = 0;
  uintptr_t end = 0;
  SmallVector<void *> slabs;

  static constexpr auto SLAB_ALIGNMENT =
      std::align_val_t(alignof(std::max_align_t));

public:
  BumpAllocator() = default;
  ~BumpAllocator() noexcept {
    for (void *slab : slabs) {
      ::operator delete(slab, SlabSize, SLAB_ALIGNMENT);
    }
  }

  BumpAllocator(const BumpAllocator &) = delete;

  BumpAllocator &operator=(const BumpAllocator &) = delete;

  /// Deallocate all but the first slab and reset current pointer to beginning.
  void reset() noexcept {
    if (!slabs.empty()) {
      for (size_t i = 1; i < slabs.size(); ++i) {
        ::operator delete(slabs[i], SlabSize, SLAB_ALIGNMENT);
      }
      slabs.resize(1);
      cur = reinterpret_cast<uintptr_t>(slabs[0]);
      end = cur + SlabSize;
    }
  }

  void *allocate(size_t size, size_t align) noexcept {
    assert(size > 0 && "cannot perform zero-sized allocation");
    uintptr_t aligned = align_up(cur, align);
    uintptr_t alloc_end = aligned + size;
    if (alloc_end <= end) [[likely]] {
      cur = alloc_end;
      return reinterpret_cast<void *>(aligned);
    }
    return allocate_slab(size, align);
  }

  void *allocate_slab(size_t size, [[maybe_unused]] size_t align) noexcept {
    // TODO: support this
    assert(size <= SlabSize && "cannot allocate more than slab size");
    assert(align <= alignof(std::max_align_t) && "alignment type unsupported");
    void *slab = ::operator new(SlabSize, SLAB_ALIGNMENT, std::nothrow);
    slabs.push_back(slab);
    cur = reinterpret_cast<uintptr_t>(slab) + size;
    end = reinterpret_cast<uintptr_t>(slab) + SlabSize;
    return slab;
  }
};

} // namespace tpde::util

template <size_t SlabSize>
void *operator new(size_t s, tpde::util::BumpAllocator<SlabSize> &a) noexcept {
  return a.allocate(s, std::min(s, alignof(std::max_align_t)));
}

template <size_t SlabSize>
void *operator new[](size_t s,
                     tpde::util::BumpAllocator<SlabSize> &a) noexcept {
  return a.allocate(s, std::min(s, alignof(std::max_align_t)));
}

template <size_t SlabSize>
void operator delete(void *, tpde::util::BumpAllocator<SlabSize> &) noexcept {}

template <size_t SlabSize>
void operator delete[](void *, tpde::util::BumpAllocator<SlabSize> &) noexcept {
}
