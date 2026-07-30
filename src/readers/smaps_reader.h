#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "base/string.h"

struct SmapsSegment {
  String path;
  ulong start_addr, end_addr;
  ulong size_kb, rss_kb, pss_kb;
  ulong private_clean_kb, private_dirty_kb;
  ulong swap_kb;
  char perms[8];
};

struct SmapsRequest {
  Pid pid;
};

struct SmapsResponse {
  Pid pid;
  int error_code;
  BumpArena owner_arena;
  Array<SmapsSegment> segments;
};

SmapsResponse smaps_reader_read(BumpArena &temp_arena,
                                const SmapsRequest &request);
