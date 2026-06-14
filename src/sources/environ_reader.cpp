#include "environ_reader.h"

#include "base/base.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

EnvironResponse read_process_environ(BumpArena &temp_arena,
                                     const EnvironRequest &request) {
  ZoneScoped;

  const Pid pid = request.pid;

  EnvironResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/environ", pid);

  FILE *file = fopen(path, "r");
  if (!file) {
    response.error_code = errno;
    return response;
  }

  // Read entire file (environment variables are null-separated)
  GrowingArray<EnvironEntry> entries = {};
  uint32_t wasted = 0;

  char buf[4096];
  size_t total_read = 0;
  const char *accumulated = nullptr;
  size_t accumulated_size = 0;

  while ((total_read = fread(buf, 1, sizeof(buf), file)) > 0) {
    const size_t new_size = accumulated_size + total_read;
    char *new_buf = response.owner_arena.alloc_string(new_size + 1);
    if (accumulated) {
      memcpy(new_buf, accumulated, accumulated_size);
    }
    memcpy(new_buf + accumulated_size, buf, total_read);
    // new_buf spans new_size + 1 bytes; alloc_string's size is opaque to the
    // analyzer. NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
    new_buf[new_size] = '\0';
    accumulated = new_buf;
    accumulated_size = new_size;
  }
  fclose(file);

  if (!accumulated || accumulated_size == 0) {
    response.error_code = 0;
    response.entries = {};
    return response;
  }

  // Parse null-separated entries
  const char *ptr = accumulated;
  const char *end = accumulated + accumulated_size;

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
