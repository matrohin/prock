#pragma once

#include "base.h"

#include <atomic>

template <class T, size_t N> struct Channel {
  static constexpr uint32_t MASK = N - 1;
  std::atomic<uint32_t> head;
  std::atomic<uint32_t> tail;
  T data[N];

  bool push(T item) {
    uint32_t loaded_tail = tail.load();
    const uint32_t new_tail = (loaded_tail + 1) & MASK;
    if (new_tail == head.load()) return false;
    data[loaded_tail] = item;
    tail.store(new_tail);
    return true;
  }

  bool pop(T &out) {
    uint32_t loaded_head = head.load();
    if (loaded_head == tail.load()) return false;
    const uint32_t new_head = (loaded_head + 1) & MASK;
    out = data[loaded_head];
    head.store(new_head);
    return true;
  }

  bool peek(T &out) const {
    uint32_t loaded_head = head.load();
    if (loaded_head == tail.load()) return false;
    out = data[loaded_head];
    return true;
  }
};
