#pragma once

#include "base/base.h"

struct String {
  const char *data;
  size_t len;

  static String copy_from(BumpArena &arena, const char *from,
                          const size_t len) {
    const size_t size = len + 1;
    char *dst = static_cast<char *>(arena.alloc_raw(size, 1));
    dst[len] = '\0';
    memcpy(dst, from, len);
    return String{dst, len};
  }

  static String copy_from(BumpArena &arena, const char *from) {
    return copy_from(arena, from, strlen(from));
  }

  static String copy_from(BumpArena &arena, const String &from) {
    return copy_from(arena, from.data, from.len);
  }

  size_t byte_size() const { return len + 1; }
};
