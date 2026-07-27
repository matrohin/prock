#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

template <class T, uint32_t N> struct Channel {
  static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
  static constexpr uint32_t MASK = N - 1;
  static constexpr size_t CACHE_LINE = 64;

  alignas(CACHE_LINE) std::atomic<uint32_t> head;
  alignas(CACHE_LINE) std::atomic<uint32_t> tail;
  alignas(CACHE_LINE) T data[N];

  bool push(const T &item) {
    const uint32_t loaded_tail = tail.load(std::memory_order_relaxed);
    const uint32_t new_tail = (loaded_tail + 1) & MASK;
    if (new_tail == head.load(std::memory_order_acquire)) return false;
    data[loaded_tail] = item;
    tail.store(new_tail, std::memory_order_release);
    return true;
  }

  bool pop(T &out) {
    const uint32_t loaded_head = head.load(std::memory_order_relaxed);
    if (loaded_head == tail.load(std::memory_order_acquire)) return false;
    const uint32_t new_head = (loaded_head + 1) & MASK;
    out = data[loaded_head];
    head.store(new_head, std::memory_order_release);
    return true;
  }

  bool peek(T &out) const {
    const uint32_t loaded_head = head.load(std::memory_order_relaxed);
    if (loaded_head == tail.load(std::memory_order_acquire)) return false;
    out = data[loaded_head];
    return true;
  }

  bool has_data() const {
    return head.load(std::memory_order_relaxed) !=
           tail.load(std::memory_order_acquire);
  }
};
