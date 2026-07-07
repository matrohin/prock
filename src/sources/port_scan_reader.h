#pragma once

#include "base/base.h"
#include "sources/process_stat.h"

struct PortEntry {
  SocketEntry sock;
  Pid pid;
  char name[64];
};

struct PortScanRequest {};

struct PortScanResponse {
  int error_code;
  bool permission_limited;
  BumpArena owner_arena;
  Array<PortEntry> entries;
};

PortScanResponse read_port_scan(BumpArena &temp_arena,
                                const PortScanRequest &request);
