// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>

#include "tpde/base.hpp"
#include "tpde/util/misc.hpp"

namespace tpde::util {

template <typename VecT>
  requires std::same_as<typename VecT::value_type, u8>
class VectorWriter {
  VecT *vector = nullptr;
  u8 *cur;
  u8 *end;

public:
  VectorWriter() noexcept = default;
  VectorWriter(VecT &vector) noexcept
      : vector(&vector),
        cur(vector.data() + vector.size()),
        end(vector.data() + vector.size()) {}
  VectorWriter(VecT &vector, size_t off) noexcept
      : vector(&vector),
        cur(vector.data() + off),
        end(vector.data() + vector.size()) {}
  ~VectorWriter() { flush(); }

  VectorWriter(const VectorWriter &) = delete;
  VectorWriter(VectorWriter &&other) noexcept { *this = std::move(other); }

  VectorWriter &operator=(const VectorWriter &) = delete;
  VectorWriter &operator=(VectorWriter &&other) noexcept {
    flush();
    vector = other.vector;
    cur = other.cur;
    end = other.end;
    other.vector = nullptr;
    return *this;
  }

  void reserve(size_t extra) {
    assert(end >= cur);
    if (size_t(end - cur) < extra) [[unlikely]] {
      size_t off = size();
      // First try to use the available capacity before growing furiously.
      if (vector->capacity() >= off + extra) {
        vector->resize(vector->capacity());
        assert(cur == vector->data() + off &&
               "in-capacity resize reallocated?");
      } else {
        vector->resize(vector->size() * 2 + extra);
      }
      cur = vector->data() + off;
      end = vector->data() + vector->size();
    }
  }

  void flush() noexcept {
    if (vector && cur != end) {
      vector->resize(size());
      end = cur;
    }
  }

  size_t size() const noexcept { return cur - vector->data(); }
  size_t capacity() const noexcept { return vector->size(); }

  u8 *data() noexcept { return vector->data(); }

  void skip_unchecked(size_t size) noexcept {
    assert(size_t(end - cur) >= size);
    cur += size;
  }

  void skip(size_t size) noexcept {
    reserve(size);
    skip_unchecked(size);
  }

  void write_unchecked(std::span<const u8> data) noexcept {
    assert(size_t(end - cur) >= data.size());
    std::memcpy(cur, data.data(), data.size());
    cur += data.size();
  }

  void write(std::span<const u8> data) noexcept {
    reserve(data.size());
    write_unchecked(data);
  }

  template <std::integral T>
  void write_unchecked(T t) noexcept {
    assert(size_t(end - cur) >= sizeof(T));
    std::memcpy(cur, &t, sizeof(T));
    cur += sizeof(T);
  }

  template <std::integral T>
  void write(T t) noexcept {
    reserve(sizeof(T));
    write_unchecked<T>(t);
  }

  void write_uleb_unchecked(uint64_t value) noexcept {
    assert(size_t(end - cur) >= uleb_len(value));
    cur += uleb_write(cur, value);
  }

  void write_uleb(uint64_t value) noexcept {
    reserve(10);
    write_uleb_unchecked(value);
  }

  void write_sleb_unchecked(int64_t value) noexcept {
    // TODO: implement and use sleb_len
    assert(size_t(end - cur) >= 10);
    cur += sleb_write(cur, value);
  }

  void write_sleb(int64_t value) noexcept {
    reserve(10);
    write_sleb_unchecked(value);
  }
};

} // end namespace tpde::util
