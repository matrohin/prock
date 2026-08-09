#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "state/raw_stats.h"

struct ThreadSnapshot {
  Pid pid;
  Array<ProcessStat> threads; // Reuse ProcessStat - same format for threads
};

struct UpdateSnapshot {
  BumpArena owner_arena;
  Array<ProcessStat> stats;
  Array<CpuCoreStat> cpu_stats; // [0]=total, [1..n]=per-core
  MemInfo mem_info;
  DiskIoStat disk_io_stats;
  NetIoStat net_io_stats;
  uint64_t uptime_ticks;
  Array<ThreadSnapshot> thread_snapshots; // Per-watched-pid thread data
  SteadyTimeDataPoint at;
  SystemTimePoint system_time;
};
