#include "socket_reader.h"

#include "base/containers.h"
#include "base/string.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

Array<unsigned long> collect_socket_inodes(BumpArena &arena, const Pid pid,
                                           int &out_errno) {
  GrowingArray<unsigned long> inodes = {};
  const String fd_dir_path = String::sprintf(arena, "/proc/%d/fd", pid);

  DIR *fd_dir = opendir(fd_dir_path.data);
  if (!fd_dir) {
    out_errno = errno;
    return inodes.to_array();
  }

  dirent *entry;
  while ((entry = readdir(fd_dir))) {
    if (entry->d_name[0] == '.') continue;

    const String link_path =
        String::sprintf(arena, "%s/%s", fd_dir_path.data, entry->d_name);

    char link_target[128];
    const ssize_t len =
        readlink(link_path.data, link_target, sizeof(link_target) - 1);
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
  const Pid pid = request.pid;

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

  const Array<SocketEntry> all_sockets =
      query_sockets_netlink(temp_arena, response.netlink_error_code);

  response.sockets =
      Array<SocketEntry>::create(response.owner_arena, inodes.size);
  uint32_t j = 0;
  for (const SocketEntry &socket : all_sockets) {
    if (std::binary_search(inodes.data, inodes.data + inodes.size,
                           socket.inode)) {
      response.sockets.data[j++] = socket;
    }
  }
  response.sockets.size = j;
  return response;
}
