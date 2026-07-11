#include "environ_reader.h"

#include "base/base.h"
#include "base/containers.h"
#include "base/string.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

EnvironResponse environ_reader_read(BumpArena &temp_arena,
                                    const EnvironRequest &request) {
  ZoneScoped;

  const Pid pid = request.pid;

  EnvironResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  const String path = String::sprintf(temp_arena, "/proc/%d/environ", pid);

  FILE *file = fopen(path.data, "r");
  if (!file) {
    response.error_code = errno;
    return response;
  }

  // Read the whole file (environment variables are null-separated) into a
  // geometrically growing buffer in temp_arena. Growing here keeps the
  // accumulation O(n) and avoids leaving intermediate copies in owner_arena.
  GrowingArray<EnvironEntry> entries = {};
  uint32_t wasted = 0;

  char buf[4096];
  char *contents = nullptr;
  size_t contents_size = 0;
  size_t capacity = 0;

  for (;;) {
    const size_t n = fread(buf, 1, sizeof(buf), file);
    if (n > 0) {
      if (contents_size + n + 1 > capacity) {
        capacity = std::max(capacity * 2, contents_size + n + 1);
        char *grown = temp_arena.alloc_string(capacity);
        if (contents) memcpy(grown, contents, contents_size);
        contents = grown;
      }
      memcpy(contents + contents_size, buf, n);
      contents_size += n;
    }
    if (n < sizeof(buf)) break; // short read: EOF or error, don't re-read
  }
  fclose(file);

  if (!contents || contents_size == 0) {
    response.error_code = 0;
    response.entries = {};
    return response;
  }
  // contents spans `capacity` >= contents_size + 1 bytes (see growth
  // condition), so this terminator is in bounds; the index taint is opaque to
  // the analyzer. NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
  contents[contents_size] = '\0'; // guard strlen on a non-null-terminated tail

  // Parse null-separated entries
  const char *ptr = contents;
  const char *end = contents + contents_size;

  while (ptr < end) {
    const size_t len = strlen(ptr);
    if (len == 0) {
      ++ptr;
      continue;
    }

    // Find the '=' separator
    const char *eq = strchr(ptr, '=');
    if (eq) {
      EnvironEntry *entry = entries.emplace_back(temp_arena, wasted);

      const size_t name_len = eq - ptr;
      entry->name = String::copy_from(response.owner_arena, ptr, name_len);

      const char *value_start = eq + 1;
      const size_t value_len = len - (value_start - ptr);
      entry->value =
          String::copy_from(response.owner_arena, value_start, value_len);
    }

    ptr += len + 1;
  }

  // Sort alphabetically by name
  std::sort(entries.begin(), entries.end(),
            [](const EnvironEntry &a, const EnvironEntry &b) {
              return strcmp(a.name.data, b.name.data) < 0;
            });

  response.entries = Array<EnvironEntry>::copy_from(
      response.owner_arena, entries.data(), entries.size());
  response.error_code = 0;
  return response;
}
