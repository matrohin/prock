#pragma once

#include "base/base.h"
#include "readers/sock_diag.h"

struct SocketRequest {
  Pid pid;
};

struct SocketResponse {
  Pid pid;
  int error_code;
  int netlink_error_code;
  BumpArena owner_arena;
  Array<SocketEntry> sockets;
};

// Scan /proc/<pid>/fd and collect the inodes of all socket file descriptors.
// Sets out_errno when the fd directory cannot be opened.
Array<unsigned long> socket_reader_collect_inodes(BumpArena &arena, Pid pid,
                                                  int &out_errno);

SocketResponse socket_reader_read(BumpArena &temp_arena,
                                  const SocketRequest &request);
