#pragma once

#include "base.h"
#include "process_stat.h"

#include <condition_variable>
#include <mutex>

struct UpdateSnapshot {
  BumpArena owner_arena;
  Array<ProcessStat> stats;
  Array<CpuCoreStat> cpu_stats;  // [0]=total, [1..n]=per-core
  MemInfo mem_info;
  SteadyTimePoint at;
  SystemTimePoint system_time;
};

struct LibraryEntry {
  char path[256];
  unsigned long addr_start;
  unsigned long addr_end;
  long file_size;  // -1 if stat failed
};

struct LibraryRequest {
  int pid;
};

struct LibraryResponse {
  int pid;
  int error_code;  // 0=success, errno otherwise
  BumpArena owner_arena;
  Array<LibraryEntry> libraries;
};

struct Sync {
  std::atomic<bool> quit;
  std::mutex quit_mutex;
  std::condition_variable quit_cv;
  RingBuffer<UpdateSnapshot, 256> update_queue;

  // Library reader thread communication
  RingBuffer<LibraryRequest, 16> library_request_queue;
  RingBuffer<LibraryResponse, 16> library_response_queue;
  std::condition_variable library_cv;
};
