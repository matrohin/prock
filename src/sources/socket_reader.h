#pragma once

#include "base.h"
#include "sources/process_stat.h"

struct SocketRequest {
  int pid;
};

struct SocketResponse {
  int pid;
  int error_code;
  BumpArena owner_arena;
  Array<SocketEntry> sockets;
};

SocketResponse read_process_sockets(const SocketRequest &request);
