#pragma once

#include "base.h"
#include "sources/process_stat.h"

struct State;
struct UpdateSnapshot;

struct SystemInfo {
  uint64_t ticks_in_second;
  uint64_t mem_page_size;
};

struct ProcessDerivedStat {
  double cpu_user_perc;
  double cpu_kernel_perc;
  double mem_resident_bytes;
  double io_read_kb_per_sec;
  double io_write_kb_per_sec;
  double net_recv_kb_per_sec;
  double net_send_kb_per_sec;
};

// Computed CPU percentages: [0]=aggregate, [1..n]=per-core
struct SystemCpuPerc {
  Array<double> total;
  Array<double> kernel;
  Array<double> interrupts;
};

// Computed disk I/O rates in MB/s
struct DiskIoRate {
  double read_mb_per_sec;
  double write_mb_per_sec;
};

// Computed network I/O rates in MB/s
struct NetIoRate {
  double recv_mb_per_sec;
  double send_mb_per_sec;
};

struct StateSnapshot {
  Array<ProcessStat> stats;
  Array<ProcessDerivedStat> derived_stats;
  Array<CpuCoreStat> cpu_stats; // Raw ticks from /proc/stat
  SystemCpuPerc cpu_perc;
  MemInfo mem_info;
  DiskIoStat disk_io_stats;
  DiskIoRate disk_io_rate;
  NetIoStat net_io_stats;
  NetIoRate net_io_rate;
  SteadyTimePoint at;
};

struct State {
  SystemInfo system;

  BumpArena snapshot_arena; // destroyed after every update
  StateSnapshot snapshot;

  uint update_count;
  SystemTimePoint update_system_time;
};

StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state,
                                    const UpdateSnapshot &snapshot);
