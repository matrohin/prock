#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "base/vm.h"

using uint = unsigned int;
using ulong = unsigned long;
using ulonglong = unsigned long long;
using Pid = int32_t;
using Seconds = std::chrono::duration<double, std::chrono::seconds::period>;
using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = std::chrono::time_point<SteadyClock>;

using SystemClock = std::chrono::system_clock;
using SystemTimePoint = std::chrono::time_point<SystemClock>;

constexpr size_t SLAB_SIZE = 256UL * 1024; // 256KB
constexpr const char *ARENA_SLAB_POOL = "arena slabs";

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
  // Cached slabs are page-aligned, so the low 12 bits are always zero and carry
  // an ABA counter bumped on every successful head update (a stale CAS that
  // sees the pointer recycled back still fails on the tag).
  static constexpr uintptr_t TAG_MASK = 0xFFF;
  std::atomic<uintptr_t> head{0};

  static uintptr_t pack(const ArenaSlab *slab, const uintptr_t tag) {
    const uintptr_t bits = reinterpret_cast<uintptr_t>(slab);
    assert((bits & TAG_MASK) == 0);
    return bits | (tag & TAG_MASK);
  }
  static ArenaSlab *ptr_of(const uintptr_t value) {
    // Tagged-pointer stack: this int->ptr round-trip is the whole design.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<ArenaSlab *>(value & ~TAG_MASK);
  }

  void push(ArenaSlab *slab) {
    slab->reset();
    uintptr_t old_head = head.load(std::memory_order_relaxed);
    uintptr_t new_head;
    do {
      slab->prev = ptr_of(old_head);
      new_head = pack(slab, (old_head & TAG_MASK) + 1);
    } while (!head.compare_exchange_weak(old_head, new_head,
                                         std::memory_order_release,
                                         std::memory_order_relaxed));
  }

  ArenaSlab *pop() {
    uintptr_t old_head = head.load(std::memory_order_relaxed);
    ArenaSlab *node;
    uintptr_t new_head;
    do {
      node = ptr_of(old_head);
      if (!node) return nullptr;
      new_head = pack(node->prev, (old_head & TAG_MASK) + 1);
    } while (!head.compare_exchange_weak(old_head, new_head,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed));
    return node;
  }
};

extern SlabCache g_slab_cache;

inline ArenaSlab *ArenaSlab::create(const size_t size, ArenaSlab *prev) {
  ArenaSlab *res = nullptr;

  if (size == SLAB_SIZE) {
    res = g_slab_cache.pop();
  }

  if (!res) {
    void *slab = vm_alloc(size);
    res = static_cast<ArenaSlab *>(slab);
    res->cur = static_cast<uint8_t *>(slab) + sizeof(ArenaSlab);
    res->left_size = size - sizeof(ArenaSlab);
    res->total_size = size;
  }

  res->prev = prev;
  TracyAllocN(res, res->total_size, ARENA_SLAB_POOL);
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

  // Allocate and copy a string (null-terminated)
  const char *alloc_string_copy(const char *src, const size_t len) {
    char *dst = alloc_string(len + 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
  }

  const char *alloc_string_copy(const char *src) {
    return alloc_string_copy(src, strlen(src));
  }

  template <class T> T *alloc() {
    return static_cast<T *>(alloc_raw(sizeof(T), alignof(T)));
  }

  void destroy() {
    ArenaSlab *it = cur_slab;
    cur_slab = nullptr;
    while (it) {
      ArenaSlab *prev = it->prev;
      TracyFreeN(it, ARENA_SLAB_POOL);
      if (it->total_size == SLAB_SIZE) {
        g_slab_cache.push(it);
      } else {
        vm_free(it, it->total_size);
      }
      it = prev;
    }
  }
};

template <class T> T *create_with_arena() {
  BumpArena arena = {};
  void *memory = arena.alloc<T>();
  // TODO: ViewState is not zero-initializable, so we need this now:
  T *res = new (memory) T{};
  res->arena = arena;
  return res;
}
