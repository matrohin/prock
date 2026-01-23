#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

using uint = unsigned int;
using ulong = unsigned long;
using ulonglong = unsigned long long;

constexpr size_t SLAB_SIZE = 4096 * 1024; // 4KB

struct ArenaSlab {
  void *cur;
  size_t left_size;
  size_t total_size;
  ArenaSlab *prev;

  static ArenaSlab *create(size_t size, ArenaSlab *prev = nullptr);

  void *advance(const size_t size) {
    void *res = cur;
    cur = static_cast<uint8_t *>(cur) + size;
    left_size -= size;
    return res;
  }

  void reset() {
    cur = reinterpret_cast<uint8_t *>(this) + sizeof(ArenaSlab);
    left_size = total_size - sizeof(ArenaSlab);
    memset(cur, 0, left_size);
  }
};

struct SlabCache {
  std::atomic<ArenaSlab *> head{nullptr};

  void push(ArenaSlab *slab) {
    slab->reset();
    ArenaSlab *old_head = head.load(std::memory_order_relaxed);
    do {
      slab->prev = old_head;
    } while (!head.compare_exchange_weak(old_head, slab,
                                         std::memory_order_release,
                                         std::memory_order_relaxed));
  }

  ArenaSlab *pop() {
    ArenaSlab *old_head = head.load(std::memory_order_relaxed);
    do {
      if (!old_head) return nullptr;
    } while (!head.compare_exchange_weak(old_head, old_head->prev,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed));
    return old_head;
  }
};

extern SlabCache g_slab_cache;

inline ArenaSlab *ArenaSlab::create(const size_t size, ArenaSlab *prev) {
  ArenaSlab *res = nullptr;

  if (size == SLAB_SIZE) {
    res = g_slab_cache.pop();
  }

  if (!res) {
    void *slab = calloc(size, 1);
    res = static_cast<ArenaSlab *>(slab);
    res->cur = reinterpret_cast<uint8_t *>(slab) + sizeof(ArenaSlab);
    res->left_size = size - sizeof(ArenaSlab);
    res->total_size = size;
  }

  res->prev = prev;
  return res;
}

struct BumpArena {
  ArenaSlab *cur_slab = nullptr;

  static BumpArena create() { return BumpArena{}; }

  void *alloc_raw(const size_t size, const size_t alignment) {
    if (cur_slab &&
        std::align(alignment, size, cur_slab->cur, cur_slab->left_size) &&
        size <= cur_slab->left_size) {
      return cur_slab->advance(size);
    }

    cur_slab = ArenaSlab::create(std::max(SLAB_SIZE, size + sizeof(ArenaSlab)),
                                 cur_slab);
    return cur_slab->advance(size);
  }

  template <class T> T *alloc_array_of(const size_t size) {
    return static_cast<T *>(alloc_raw(size * sizeof(T), alignof(T)));
  }

  char *alloc_string(const size_t size) {
    return static_cast<char *>(alloc_raw(size, 1));
  }


  template <class T> T *alloc() {
    return static_cast<T *>(alloc_raw(sizeof(T), alignof(T)));
  }

  void destroy() {
    ArenaSlab *it = cur_slab;
    cur_slab = nullptr;
    while (it) {
      ArenaSlab *prev = it->prev;
      if (it->total_size == SLAB_SIZE) {
        g_slab_cache.push(it);
      } else {
        free(it);
      }
      it = prev;
    }
  }
};

struct String {
  char *data;
  size_t length;
};

template <class T> struct LinkedNode {
  T value;
  LinkedNode *next = nullptr;
};

template <class T> struct LinkedList {
  LinkedNode<T> *head = nullptr;
  size_t size = 0;

  T *emplace_front(BumpArena &arena) {
    LinkedNode<T> *node = arena.alloc<LinkedNode<T>>();
    node->next = head;
    head = node;
    ++size;
    return &node->value;
  }
};

template <class T> struct Array {
  T *data;
  size_t size;

  static Array<T> create(BumpArena &arena, size_t with_size) {
    T *result = static_cast<T *>(arena.alloc_raw(with_size * sizeof(T), alignof(T)));
    return Array<T>{result, with_size};
  }
};

template <class T> struct GrowingArray {
  Array<T> inner;
  size_t cur_size;

  T *emplace_back(BumpArena &arena, size_t &wasted_bytes) {
    if (cur_size >= inner.size) {
      wasted_bytes += inner.size * sizeof(T);
      realloc(arena);
    }
    return &inner.data[cur_size++];
  }

  void realloc(BumpArena &arena) {
    size_t new_size = std::max(static_cast<size_t>(4), cur_size * 2);
    Array<T> new_inner = Array<T>::create(arena, new_size);
    if (inner.data) memcpy(new_inner.data, inner.data, cur_size * sizeof(T));
    inner = new_inner;
  }

  void shrink_to(size_t size) {
    if (size >= cur_size) return;
    memset(inner.data + size, 0, (cur_size - size) * sizeof(T));
    cur_size = size;
  }

  T *data() { return inner.data; }
  const T *data() const { return inner.data; }
  size_t size() const { return cur_size; }
  size_t total_byte_size() const { return inner.size * sizeof(T); }
};

using Seconds = std::chrono::duration<double, std::chrono::seconds::period>;
using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = std::chrono::time_point<SteadyClock>;

using SystemClock = std::chrono::system_clock;
using SystemTimePoint = std::chrono::time_point<SystemClock>;
