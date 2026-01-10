#pragma once

#include "base.h"
#include "process_stat.h"

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
};

struct StateSnapshot {
  Array<ProcessStat> stats;
  Array<ProcessDerivedStat> derived_stats;
  SteadyTimePoint at;
};

struct State {
  SystemInfo system;

  BumpArena snapshot_arena; // destroyed after every update
  StateSnapshot snapshot;

  uint update_count;
  SystemTimePoint update_system_time;
};

StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state, const UpdateSnapshot &snapshot);

