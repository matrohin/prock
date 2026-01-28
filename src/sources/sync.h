#pragma once

#include "base.h"
#include "on_demand_reader.h"
#include "process_stat.h"
#include "ring_buffer.h"

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

struct Sync {
  std::atomic<bool> quit;
  std::atomic<float> update_period{0.5f};  // seconds, 0 = paused
  std::mutex quit_mutex;
  std::condition_variable quit_cv;
  RingBuffer<UpdateSnapshot, 256> update_queue;

  // Thread gathering: PIDs to gather threads for, 0=empty slot
  std::atomic<int> watched_pids[MAX_WATCHED_PIDS];
  std::atomic<int> watched_pids_count{0};

  OnDemandReaderSync on_demand_reader;
};
