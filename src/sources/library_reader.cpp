#include "library_reader.h"

#include "base.h"
#include "environ_reader.h"
#include "sync.h"
#include "tracy/Tracy.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/stat.h>

static LibraryResponse read_process_libraries(const int pid) {
  ZoneScoped;
  LibraryResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/maps", pid);

  FILE *file = fopen(path, "r");
  if (!file) {
    response.error_code = errno;
    return response;
  }

  // First pass: count unique .so files
  GrowingArray<LibraryEntry> entries = {};
  size_t wasted = 0;

  char line[512];
  while (fgets(line, sizeof(line), file)) {
    // Parse line format: addr_start-addr_end perms offset dev inode pathname
    unsigned long addr_start, addr_end;
    char perms[8] = {};
    unsigned long offset;
    char dev[16] = {};
    unsigned long inode;
    char pathname[256] = {};

    int n = sscanf(line, "%lx-%lx %7s %lx %15s %lu %255s", &addr_start,
                   &addr_end, perms, &offset, dev, &inode, pathname);

    if (n < 7 || pathname[0] == '\0') continue;

    // Skip non-.so files and special entries
    if (pathname[0] != '/') continue;
    const char *ext = strstr(pathname, ".so");
    if (!ext) continue;

    // Check if already in list (deduplicate)
    bool found = false;
    for (size_t i = 0; i < entries.size(); ++i) {
      if (strcmp(entries.data()[i].path, pathname) == 0) {
        found = true;
        break;
      }
    }
    if (found) continue;

    LibraryEntry *entry = entries.emplace_back(response.owner_arena, wasted);
    size_t path_len = strlen(pathname);
    entry->path = response.owner_arena.alloc_string_copy(pathname, path_len);
    entry->path_len = path_len;
    entry->addr_start = addr_start;
    entry->addr_end = addr_end;

    // Get file size
    struct stat st;
    if (stat(pathname, &st) == 0) {
      entry->file_size = st.st_size;
    } else {
      entry->file_size = -1;
    }
  }
  fclose(file);

  // Sort alphabetically by path
  std::sort(entries.data(), entries.data() + entries.size(),
            [](const LibraryEntry &a, const LibraryEntry &b) {
              return strcmp(a.path, b.path) < 0;
            });

  // Copy to final array
  response.libraries =
      Array<LibraryEntry>::create(response.owner_arena, entries.size());
  memcpy(response.libraries.data, entries.data(),
         entries.size() * sizeof(LibraryEntry));

  response.error_code = 0;
  return response;
}

void library_reader_thread(Sync &sync) {
  while (!sync.quit.load()) {
    LibraryRequest lib_request;
    EnvironRequest env_request;
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      sync.library_cv.wait(lock, [&] {
        return sync.quit.load() ||
               sync.library_request_queue.peek(lib_request) ||
               sync.environ_request_queue.peek(env_request);
      });
    }
    if (sync.quit.load()) break;

    while (sync.library_request_queue.pop(lib_request)) {
      LibraryResponse response = read_process_libraries(lib_request.pid);
      if (!sync.library_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
      glfwPostEmptyEvent();
    }

    while (sync.environ_request_queue.pop(env_request)) {
      EnvironResponse response = read_process_environ(env_request.pid);
      if (!sync.environ_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
      glfwPostEmptyEvent();
    }
  }
}
