#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "state/raw_stats.h"
#include "state/system_info.h"

struct State;
struct UpdateSnapshot;

// Delta between two samples of a monotonically increasing kernel counter. A
// "current" below "previous" means the counter was reset (e.g. a network
// interface bouncing, CPU hot(un)plug shifting /proc/stat lines, or a reboot),
// so clamp the delta to zero instead of letting the unsigned subtraction wrap
// into a huge spike.
inline uint64_t counter_delta(const uint64_t cur, const uint64_t prev) {
  return cur >= prev ? cur - prev : 0;
}

// Rate form of counter_delta. The caller guarantees divisor > 0.
inline double counter_rate(const uint64_t cur, const uint64_t prev,
                           const double scale, const double divisor) {
  return static_cast<double>(counter_delta(cur, prev)) * scale / divisor;
}

// Rescale the one-core jiffy budget to a task's own sampling window, since
// tasks are read at different instants within a process_stat_gather pass. Falls
// back to the shared budget when either interval is unusable.
inline double effective_core_ticks(const SteadyTimeDataPoint cur_read,
                                   const SteadyTimeDataPoint prev_read,
                                   const double per_core_ticks,
                                   const double interval_secs) {
  const double dt = cur_read.elapsed_seconds(prev_read);
  return dt > 0 && interval_secs > 0 ? per_core_ticks * dt / interval_secs
                                     : per_core_ticks;
}

struct ProcessDerivedStat {
  double cpu_user_perc;
  double cpu_kernel_perc;
  double mem_resident_bytes;
  double mem_virtual_bytes;
  double io_read_kb_per_sec;
  double io_write_kb_per_sec;
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

struct ThreadSnapshot;

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
  Array<ThreadSnapshot> thread_snapshots;
  SteadyTimeDataPoint at;
  // /proc/stat jiffy budget for one core over this interval; the basis for
  // per-process and per-thread CPU% (see state_snapshot_update).
  double per_core_ticks;
  // Wall-clock seconds per_core_ticks was measured over, so per-task CPU% can
  // rescale that budget to each task's own sampling interval.
  double interval_secs;
};

struct State {
  SystemInfo system;

  BumpArena snapshot_arena;        // destroyed after every update
  BumpArena frozen_snapshot_arena; // used when the view is paused
  StateSnapshot snapshot;

  uint32_t update_count;
  SystemTimePoint update_system_time;
};

StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state,
                                    const UpdateSnapshot &snapshot);
