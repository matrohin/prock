#pragma once

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
  ArenaSlab *prev;

  static ArenaSlab *create(size_t size, ArenaSlab *prev = nullptr) {
    void *slab = (void *)calloc(size, 1);
    void *cur = (void *)((uint8_t *)(slab) + sizeof(ArenaSlab));
    ArenaSlab *res = (ArenaSlab *)slab;
    res->cur = cur;
    res->left_size = size - sizeof(ArenaSlab);
    res->prev = prev;
    return res;
  }

  void *advance(size_t size) {
    void *res = cur;
    cur = ((uint8_t *) cur) + size;
    left_size -= size;
    return res;
  }
};

struct BumpArena {
  ArenaSlab *cur_slab = nullptr;

  static BumpArena create() { return BumpArena{}; }

  void *alloc_raw(size_t size, size_t alignment) {
    if (cur_slab &&
        std::align(alignment, size, cur_slab->cur, cur_slab->left_size) &&
        size <= cur_slab->left_size
    ) {
      return cur_slab->advance(size);
    }

    cur_slab = ArenaSlab::create(std::max(SLAB_SIZE, size + sizeof(ArenaSlab)), cur_slab);
    return cur_slab->advance(size);
  }

  template<class T>
  T *alloc() {
    return (T *)alloc_raw(sizeof(T), alignof(T));
  }

  void destroy() {
    ArenaSlab *it = cur_slab;
    cur_slab = nullptr;
    while (it) {
      ArenaSlab *prev = it->prev;
      free((void *) it);
      it = prev;
    }
  }
};


struct String {
  char *data;
  size_t length;
};

template<class T>
struct LinkedNode {
  T value;
  LinkedNode *next = nullptr;
};

template<class T>
struct LinkedList {
  LinkedNode<T> *head = nullptr;
  size_t size = 0;

  T* emplace_front(BumpArena &arena) {
    LinkedNode<T> *node = arena.alloc<LinkedNode<T>>();
    node->next = head;
    head = node;
    ++size;
    return &node->value;
  }
};

template<class T>
struct Array {
  T *data;
  size_t size;

  static Array<T> create(BumpArena &arena, size_t with_size) {
    T *result = (T*)arena.alloc_raw(with_size * sizeof(T), alignof(T));
    return Array<T>{result, with_size};
  }
};

template<class T>
struct GrowingArray {
  Array<T> inner;
  size_t cur_size;

  T* emplace_back(BumpArena &arena, size_t &wasted_bytes) {
    if (cur_size >= inner.size) {
      wasted_bytes += inner.size * sizeof(T);
      realloc(arena);
    }
    return &inner.data[cur_size++];
  }

  void realloc(BumpArena &arena) {
    size_t new_size = std::max((size_t) 4, cur_size * 2);
    Array<T> new_inner = Array<T>::create(arena, new_size);
    memcpy(new_inner.data, inner.data, cur_size * sizeof(T));
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
