#pragma once

#include <cstdint>

template <uint32_t N> struct RingTrack {
  static constexpr uint32_t MASK = N - 1;
  uint32_t head;
  uint32_t size;

  uint32_t emplace_back() {
    const uint32_t new_item_idx = (head + size) & MASK;
    if (size == N)
      head = (head + 1) & MASK;
    else
      ++size;
    return new_item_idx;
  }

  uint32_t to_data_idx(const uint32_t idx) const { return (head + idx) & MASK; }

  // Note: zero-initialized value can be returned in case of an empty RingBuffer
  uint32_t last_idx() const { return (head + size - 1) & MASK; }
};
