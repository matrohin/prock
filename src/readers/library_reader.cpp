#include "library_reader.h"

#include "base/base.h"
#include "base/containers.h"
#include "base/string.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

LibraryResponse library_reader_read(BumpArena &temp_arena,
                                    const LibraryRequest &request) {
  ZoneScoped;

  const Pid pid = request.pid;

  LibraryResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  const String path = String::sprintf(temp_arena, "/proc/%d/maps", pid);

  FILE *file = fopen(path.data, "r");
  if (!file) {
    response.error_code = errno;
    return response;
  }

  // First pass: count unique .so files
  GrowingArray<LibraryEntry> entries = {};
  uint32_t wasted = 0;

  char line[PATH_MAX + 256];
  while (fgets(line, sizeof(line), file)) {
    // Parse line format: addr_start-addr_end perms offset dev inode pathname.
    unsigned long addr_start, addr_end;
    char perms[8] = {};
    unsigned long offset;
    char dev[16] = {};
    unsigned long inode;
    int chars_read = 0;

    if (sscanf(line, "%lx-%lx %7s %lx %15s %lu%n", &addr_start, &addr_end,
               perms, &offset, dev, &inode, &chars_read) != 6) {
      continue;
    }

    char *pathname = line + chars_read;
    // chars_read is sscanf's %n, bounded by the matched prefix of line.
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
    while (*pathname == ' ')
      ++pathname;
    uint32_t plen = static_cast<uint32_t>(strlen(pathname));
    while (plen > 0 &&
           (pathname[plen - 1] == '\n' || pathname[plen - 1] == '\r'))
      --plen;
    pathname[plen] = '\0';

    // Skip non-.so files and special entries
    if (pathname[0] != '/') continue;
    const char *ext = strstr(pathname, ".so");
    if (!ext) continue;

    // Check if already in list (deduplicate)
    bool found = false;
    for (const LibraryEntry &entry : entries) {
      if (strcmp(entry.path.data, pathname) == 0) {
        found = true;
        break;
      }
    }
    if (found) continue;

    LibraryEntry *entry = entries.emplace_back(temp_arena, wasted);
    entry->path = String::copy_from(response.owner_arena, pathname);
    entry->addr_start = addr_start;
    entry->addr_end = addr_end;

    // Get file size via /proc/<pid>/root to handle different mount namespaces
    const String proc_path =
        String::sprintf(temp_arena, "/proc/%d/root%s", pid, pathname);
    struct stat st;
    if (stat(proc_path.data, &st) == 0) {
      entry->file_size = st.st_size;
    } else {
      entry->file_size = -1;
    }
  }
  fclose(file);

  // Sort alphabetically by path
  std::sort(entries.begin(), entries.end(),
            [](const LibraryEntry &a, const LibraryEntry &b) {
              return strcmp(a.path.data, b.path.data) < 0;
            });

  response.libraries = Array<LibraryEntry>::copy_from(
      response.owner_arena, entries.data(), entries.size());
  response.error_code = 0;
  return response;
}
