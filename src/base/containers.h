#pragma once

#include "base/base.h"

#include <cstdint>

template <class T> struct LinkedNode {
  T value;
  LinkedNode *next = nullptr;
};

template <class T> struct LinkedList {
  LinkedNode<T> *head = nullptr;
  uint32_t size = 0;

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
  uint32_t size;

  static Array<T> create(BumpArena &arena, uint32_t with_size) {
    T *result =
        static_cast<T *>(arena.alloc_raw(with_size * sizeof(T), alignof(T)));
    return Array<T>{result, with_size};
  }

  static Array<T> copy_from(BumpArena &arena, const T *from,
                            const uint32_t size) {
    Array<T> dst = create(arena, size);
    if (size > 0) {
      memcpy(dst.data, from, size * sizeof(T));
    }
    return dst;
  }

  static Array<T> copy_from(BumpArena &arena, const Array<T> &from) {
    return copy_from(arena, from.data, from.size);
  }

  void inplace_copy_from(const T *from, const uint32_t from_size) {
    assert(from_size <= size);
    memcpy(data, from, from_size * sizeof(T));
    size = from_size;
  }

  size_t byte_size() const { return size * sizeof(T); }

  T *begin() const { return data; }
  T *end() const { return data + size; }
};

template <class T> struct GrowingArray {
  Array<T> inner;
  uint32_t cur_size;

  T *emplace_back(BumpArena &arena) {
    uint32_t wasted = 0;
    return emplace_back(arena, wasted);
  }

  T *emplace_back(BumpArena &arena, uint32_t &wasted_bytes) {
    if (cur_size >= inner.size) {
      wasted_bytes += inner.byte_size();
      realloc(arena);
    }
    return &inner.data[cur_size++];
  }

  void realloc(BumpArena &arena) {
    uint32_t new_size = std::max(4u, cur_size * 2);
    Array<T> new_inner = Array<T>::create(arena, new_size);
    if (inner.data) memcpy(new_inner.data, inner.data, cur_size * sizeof(T));
    inner = new_inner;
  }

  void shrink_to(uint32_t size) {
    if (size >= cur_size) return;
    memset(inner.data + size, 0, (cur_size - size) * sizeof(T));
    cur_size = size;
  }

  T *data() { return inner.data; }
  const T *data() const { return inner.data; }
  uint32_t size() const { return cur_size; }
  size_t total_byte_size() const { return inner.size * sizeof(T); }

  T *begin() { return data(); }
  T *end() { return data() + size(); }

  Array<T> to_array() { return {data(), size()}; }
};
