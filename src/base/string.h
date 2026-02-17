#pragma once

#include "base/base.h"

struct String {
  const char *data;
  uint32_t len;

  static String copy_from(BumpArena &arena, const char *from,
                          const uint32_t len) {
    const uint32_t size = len + 1;
    char *dst = static_cast<char *>(arena.alloc_raw(size, 1));
    dst[len] = '\0';
    memcpy(dst, from, len);
    return String{dst, len};
  }

  static String copy_from(BumpArena &arena, const char *from) {
    return copy_from(arena, from, static_cast<uint32_t>(strlen(from)));
  }

  static String copy_from(BumpArena &arena, const String &from) {
    return copy_from(arena, from.data, from.len);
  }

  uint32_t byte_size() const { return len + 1; }
};
