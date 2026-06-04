#include "port_scan_reader.h"

#include "sources/proc_parsers.h"
#include "sources/socket_reader.h"

#include "tracy/Tracy.hpp"

#include <cerrno>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static const SocketEntry *
find_socket_by_inode(const Array<SocketEntry> &sockets,
                     const unsigned long inode) {
  const uint32_t idx = bin_search_exact(
      sockets.size, [&](const uint32_t i) { return sockets.data[i].inode; },
      inode);
  return idx == UINT32_MAX ? nullptr : &sockets.data[idx];
}

PortScanResponse read_port_scan(BumpArena &temp_arena,
                                const PortScanRequest & /*request*/) {
  ZoneScoped;

  PortScanResponse response = {};
  response.owner_arena = BumpArena::create();

  const Array<SocketEntry> all_sockets = query_sockets_netlink(temp_arena);

  GrowingArray<PortEntry> entries = {};

  DIR *proc_dir = opendir("/proc");
  if (!proc_dir) {
    response.error_code = errno;
    response.entries = Array<PortEntry>::create(response.owner_arena, 0);
    return response;
  }

  dirent *proc_entry;
  while ((proc_entry = readdir(proc_dir))) {
    if (proc_entry->d_name[0] < '0' || proc_entry->d_name[0] > '9') continue;
    const Pid pid = atoi(proc_entry->d_name);
    if (pid <= 0) continue;

    int collect_errno = 0;
    const Array<unsigned long> inodes =
        collect_socket_inodes(temp_arena, pid, collect_errno);
    if (collect_errno == EACCES) response.permission_limited = true;

    char comm[64];
    bool comm_read = false;

    for (const unsigned long inode : inodes) {
      const SocketEntry *match = find_socket_by_inode(all_sockets, inode);
      if (!match) continue;

      if (!comm_read) {
        read_proc_comm(pid, comm, sizeof(comm));
        comm_read = true;
      }

      PortEntry *pe = entries.emplace_back(temp_arena);
      pe->sock = *match;
      pe->pid = pid;
      strncpy(pe->comm, comm, sizeof(pe->comm) - 1);
      pe->comm[sizeof(pe->comm) - 1] = '\0';
    }
  }
  closedir(proc_dir);

  response.entries =
      Array<PortEntry>::copy_from(response.owner_arena, entries.to_array());
  return response;
}
