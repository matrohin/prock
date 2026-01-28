#pragma once

struct EnvironEntry {
  const char *name;
  const char *value;
  size_t name_len;
  size_t value_len;
};

struct EnvironRequest {
  int pid;
};

struct EnvironResponse {
  int pid;
  int error_code; // 0=success, errno otherwise
  BumpArena owner_arena;
  Array<EnvironEntry> entries;
};

EnvironResponse read_process_environ(BumpArena &temp_arena,
                                     const EnvironRequest &request);
