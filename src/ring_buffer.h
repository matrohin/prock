#pragma once

#include "base.h"

#include <atomic>


template<class T, size_t N>
struct RingBuffer {
  static constexpr size_t MASK = N - 1;
  std::atomic<size_t> head;
  std::atomic<size_t> tail;
  T data[N];

  bool push(T item) {
    size_t loaded_tail = tail.load();
    size_t new_tail = (loaded_tail + 1) & MASK;
    if (new_tail == head.load()) return false;
    data[loaded_tail] = item;
    tail.store(new_tail);
    return true;
  }

  bool pop(T &out) {
    size_t loaded_head = head.load();
    if (loaded_head == tail.load()) return false;
    size_t new_head = (loaded_head + 1) & MASK;
    out = data[loaded_head];
    head.store(new_head);
    return true;
  }
};
