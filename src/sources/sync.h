#pragma once

#include "base.h"
#include "ring_buffer.h"
#include "process_stat.h"

#include <condition_variable>
#include <mutex>

constexpr int MAX_WATCHED_PIDS = 16;

struct ThreadSnapshot {
  int pid;
  Array<ProcessStat> threads;  // Reuse ProcessStat - same format for threads
};

struct UpdateSnapshot {
  BumpArena owner_arena;
  Array<ProcessStat> stats;
  Array<CpuCoreStat> cpu_stats; // [0]=total, [1..n]=per-core
  MemInfo mem_info;
  DiskIoStat disk_io_stats;
  NetIoStat net_io_stats;
  Array<ThreadSnapshot> thread_snapshots;  // Per-watched-pid thread data
  SteadyTimePoint at;
  SystemTimePoint system_time;
};

struct LibraryEntry {
  char path[256];
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

struct EnvironEntry {
  char name[256];
  char value[4096];
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

struct Sync {
  std::atomic<bool> quit;
  std::atomic<float> update_period{0.5f};  // seconds, 0 = paused
  std::mutex quit_mutex;
  std::condition_variable quit_cv;
  RingBuffer<UpdateSnapshot, 256> update_queue;

  // Library/environ reader thread communication
  RingBuffer<LibraryRequest, 16> library_request_queue;
  RingBuffer<LibraryResponse, 16> library_response_queue;
  RingBuffer<EnvironRequest, 16> environ_request_queue;
  RingBuffer<EnvironResponse, 16> environ_response_queue;
  std::condition_variable library_cv;

  // Thread gathering: PIDs to gather threads for, 0=empty slot
  std::atomic<int> watched_pids[MAX_WATCHED_PIDS];
  std::atomic<int> watched_pids_count{0};
};
