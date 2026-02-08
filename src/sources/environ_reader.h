#pragma once

#include "base/base.h"
#include "base/string.h"

struct EnvironEntry {
  String name;
  String value;
};

struct EnvironRequest {
  int pid;
};

struct EnvironResponse {
  int pid;
  int error_code;
  BumpArena owner_arena;
  Array<EnvironEntry> entries;
};

EnvironResponse read_process_environ(BumpArena &temp_arena,
                                     const EnvironRequest &request);
