#pragma once

#include "base/base.h"
#include "sources/process_stat.h"

struct SocketRequest {
  Pid pid;
};

struct SocketResponse {
  Pid pid;
  int error_code;
  BumpArena owner_arena;
  Array<SocketEntry> sockets;
};

SocketResponse read_process_sockets(BumpArena &temp_arena,
                                    const SocketRequest &request);
