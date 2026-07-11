#pragma once

#include "base/base.h"
#include "sock_diag.h"

struct PortEntry {
  SocketEntry sock;
  Pid pid;
  char name[64];
};

struct PortScanRequest {};

struct PortScanResponse {
  int error_code;
  int netlink_error_code;
  bool permission_limited;
  BumpArena owner_arena;
  Array<PortEntry> entries;
};

PortScanResponse port_scan_reader_read(BumpArena &temp_arena,
                                       const PortScanRequest &request);
