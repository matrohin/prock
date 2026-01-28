#include "socket_reader.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <dirent.h>
#include <unistd.h>

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
    const ssize_t len =
        readlink(link_path, link_target, sizeof(link_target) - 1);
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

SocketResponse read_process_sockets(const SocketRequest &request) {
  ZoneScoped;

  const int pid = request.pid;

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
