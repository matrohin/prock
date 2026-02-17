#include "socket_reader.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

// Collect socket inodes for a specific process from /proc/<pid>/fd
static Array<unsigned long> collect_socket_inodes(BumpArena &arena,
                                                  const int pid,
                                                  int &out_errno) {
  GrowingArray<unsigned long> inodes = {};
  char fd_dir_path[64];
  snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%d/fd", pid);

  DIR *fd_dir = opendir(fd_dir_path);
  if (!fd_dir) {
    out_errno = errno;
    return inodes.to_array();
  }

  dirent *entry;
  while ((entry = readdir(fd_dir))) {
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
      *inodes.emplace_back(arena) = inode;
    }
  }
  closedir(fd_dir);
  return inodes.to_array();
}

SocketResponse read_process_sockets(BumpArena &temp_arena,
                                    const SocketRequest &request) {
  ZoneScoped;
  const int pid = request.pid;

  SocketResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  int collect_errno = 0;
  const Array<unsigned long> inodes =
      collect_socket_inodes(temp_arena, pid, collect_errno);

  if (inodes.size == 0) {
    response.sockets = Array<SocketEntry>::create(response.owner_arena, 0);
    response.error_code = collect_errno;
    return response;
  }
  std::sort(inodes.data, inodes.data + inodes.size);

  const Array<SocketEntry> all_sockets = query_sockets_netlink(temp_arena);

  response.sockets =
      Array<SocketEntry>::create(response.owner_arena, inodes.size);
  uint32_t j = 0;
  for (uint32_t i = 0; i < all_sockets.size; ++i) {
    if (std::binary_search(inodes.data, inodes.data + inodes.size,
                           all_sockets.data[i].inode)) {
      response.sockets.data[j++] = all_sockets.data[i];
    }
  }
  response.sockets.size = j;
  return response;
}
