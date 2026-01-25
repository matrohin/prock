#include "environ_reader.h"

#include "base.h"
#include "sync.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

EnvironResponse read_process_environ(const int pid) {
  ZoneScoped;
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
  size_t wasted = 0;

  char buf[4096];
  size_t total_read = 0;
  char *accumulated = nullptr;
  size_t accumulated_size = 0;

  while ((total_read = fread(buf, 1, sizeof(buf), file)) > 0) {
    size_t new_size = accumulated_size + total_read;
    char *new_buf = response.owner_arena.alloc_string(new_size + 1);
    if (accumulated) {
      memcpy(new_buf, accumulated, accumulated_size);
    }
    memcpy(new_buf + accumulated_size, buf, total_read);
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
    size_t len = strlen(ptr);
    if (len == 0) {
      ++ptr;
      continue;
    }

    // Find the '=' separator
    const char *eq = strchr(ptr, '=');
    if (eq) {
      EnvironEntry *entry = entries.emplace_back(response.owner_arena, wasted);

      size_t name_len = eq - ptr;
      entry->name = response.owner_arena.alloc_string_copy(ptr, name_len);
      entry->name_len = name_len;

      const char *value_start = eq + 1;
      size_t value_len = len - (value_start - ptr);
      entry->value = response.owner_arena.alloc_string_copy(value_start, value_len);
      entry->value_len = value_len;
    }

    ptr += len + 1;
  }

  // Sort alphabetically by name
  std::sort(entries.data(), entries.data() + entries.size(),
            [](const EnvironEntry &a, const EnvironEntry &b) {
              return strcmp(a.name, b.name) < 0;
            });

  // Copy to final array
  response.entries =
      Array<EnvironEntry>::create(response.owner_arena, entries.size());
  memcpy(response.entries.data, entries.data(),
         entries.size() * sizeof(EnvironEntry));

  response.error_code = 0;
  return response;
}
