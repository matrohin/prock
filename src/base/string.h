#pragma once

#include "base/base.h"

#include <cstdio>
#include <stdarg.h>

// Null-terminated strings, but with a length
struct String {
  const char *data;
  uint32_t len;

  static String copy_from(BumpArena &arena, const char *from,
                          const uint32_t len) {
    const uint32_t size = len + 1;
    char *dst = static_cast<char *>(arena.alloc_raw(size, 1));
    // dst spans size == len + 1 bytes; alloc_raw's size is opaque to the
    // analyzer. NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
    dst[len] = '\0';
    memcpy(dst, from, len);
    return String{dst, len};
  }

  static String vsprintf(BumpArena &arena, const char *format, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    const int size = vsnprintf(nullptr, 0, format, args);
    const int full_size = size + 1;
    char *res = static_cast<char *>(arena.alloc_raw(full_size, 1));
    vsnprintf(res, full_size, format, args_copy);
    va_end(args_copy);
    return String{res, static_cast<uint32_t>(size)};
  }

  static String sprintf(BumpArena &arena, const char *format, ...) {
    va_list args;
    va_start(args, format);
    String result = vsprintf(arena, format, args);
    va_end(args);
    return result;
  }

  static String copy_from(BumpArena &arena, const char *from) {
    return copy_from(arena, from, static_cast<uint32_t>(strlen(from)));
  }

  static String copy_from(BumpArena &arena, const String &from) {
    return copy_from(arena, from.data, from.len);
  }

  static String static_string(const char *from) {
    return String{from, static_cast<uint32_t>(strlen(from))};
  }

  uint32_t byte_size() const { return len + 1; }
};
