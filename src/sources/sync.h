#pragma once

#include "base/base.h"
#include "base/ring_buffer.h"
#include "on_demand_reader.h"
#include "process_stat.h"

#include <condition_variable>
#include <mutex>

struct ThreadSnapshot {
  Pid pid;
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

  // Thread gathering: PIDs to watch/unwatch
  RingBuffer<int, 16> thread_watch_queue;
  RingBuffer<int, 16> thread_unwatch_queue;

  OnDemandReaderSync on_demand_reader;
};
