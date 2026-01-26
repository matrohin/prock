#include "library_reader.h"

#include "base.h"
#include "environ_reader.h"
#include "process_stat.h"
#include "sync.h"
#include "tracy/Tracy.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

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
    char pathname[PATH_MAX] = {};

    int n = sscanf(line, "%lx-%lx %7s %lx %15s %lu %4095s", &addr_start,
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

// Collect socket inodes for a specific process from /proc/<pid>/fd
static size_t collect_socket_inodes(const int pid, unsigned long *inodes,
                                    const size_t max_inodes) {
  char fd_dir_path[64];
  snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);

  DIR *fd_dir = opendir(fd_dir_path);
  if (!fd_dir) return 0;

  size_t count = 0;
  struct dirent *entry;
  while ((entry = readdir(fd_dir)) && count < max_inodes) {
    if (entry->d_name[0] == '.') continue;

    char link_path[PATH_MAX];
    snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir_path, entry->d_name);

    char link_target[128];
    const ssize_t len = readlink(link_path, link_target, sizeof(link_target) - 1);
    if (len <= 0) continue;
    link_target[len] = '\0';

    // Check if it's a socket: "socket:[12345]"
    if (strncmp(link_target, "socket:[", 8) != 0) continue;

    unsigned long inode = 0;
    if (sscanf(link_target + 8, "%lu]", &inode) == 1) {
      inodes[count++] = inode;
    }
  }
  closedir(fd_dir);
  return count;
}

static SocketResponse read_process_sockets(const int pid) {
  ZoneScoped;
  SocketResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  // Collect socket inodes for this process
  constexpr size_t MAX_INODES = 4096;
  unsigned long *inodes =
      response.owner_arena.alloc_array_of<unsigned long>(MAX_INODES);
  const size_t inode_count = collect_socket_inodes(pid, inodes, MAX_INODES);

  if (inode_count == 0) {
    // No sockets found (or can't read /proc/<pid>/fd)
    response.sockets = Array<SocketEntry>::create(response.owner_arena, 0);
    response.error_code = 0;
    return response;
  }

  // Sort inodes for binary search
  std::sort(inodes, inodes + inode_count);

  // Query all sockets via netlink (sorted by inode)
  BumpArena temp_arena = BumpArena::create();
  const Array<SocketEntry> all_sockets = query_sockets_netlink(temp_arena);

  // Filter to only sockets belonging to this process
  size_t match_count = 0;
  for (size_t i = 0; i < all_sockets.size; ++i) {
    if (std::binary_search(inodes, inodes + inode_count,
                           all_sockets.data[i].inode)) {
      ++match_count;
    }
  }

  response.sockets =
      Array<SocketEntry>::create(response.owner_arena, match_count);
  size_t j = 0;
  for (size_t i = 0; i < all_sockets.size; ++i) {
    if (std::binary_search(inodes, inodes + inode_count,
                           all_sockets.data[i].inode)) {
      response.sockets.data[j++] = all_sockets.data[i];
    }
  }

  temp_arena.destroy();
  response.error_code = 0;
  return response;
}

void library_reader_thread(Sync &sync) {
  while (!sync.quit.load()) {
    LibraryRequest lib_request;
    EnvironRequest env_request;
    SocketRequest sock_request;
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      sync.library_cv.wait(lock, [&] {
        return sync.quit.load() ||
               sync.library_request_queue.peek(lib_request) ||
               sync.environ_request_queue.peek(env_request) ||
               sync.socket_request_queue.peek(sock_request);
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

    while (sync.socket_request_queue.pop(sock_request)) {
      SocketResponse response = read_process_sockets(sock_request.pid);
      if (!sync.socket_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
      glfwPostEmptyEvent();
    }
  }
}
