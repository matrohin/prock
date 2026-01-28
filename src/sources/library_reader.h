#pragma once

struct LibraryEntry {
  const char *path;
  size_t path_len;
  unsigned long addr_start;
  unsigned long addr_end;
  long file_size; // -1 if stat failed
};

struct LibraryRequest {
  int pid;
};

struct LibraryResponse {
  int pid;
  int error_code; // 0=success, errno otherwise
  BumpArena owner_arena;
  Array<LibraryEntry> libraries;
};

LibraryResponse read_process_libraries(const LibraryRequest &request);
