#pragma once

#include "base/base.h"

struct String {
  const char *data;
  size_t len;

  static String copy_from(BumpArena &arena, const char *from) {
    const size_t len = strlen(from);
    const size_t size = len + 1;
    char *dst = static_cast<char *>(arena.alloc_raw(size, 1));
    memcpy(dst, from, size);
    return String{dst, len};
  }

  static String copy_from(BumpArena &arena, const String &from) {
    const size_t size = from.len + 1;
    char *dst = static_cast<char *>(arena.alloc_raw(size, 1));
    memcpy(dst, from.data, size);
    return String{dst, from.len};
  }

  size_t byte_size() const { return len + 1; }
};
