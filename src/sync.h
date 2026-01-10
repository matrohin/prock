#pragma once

#include "base.h"
#include "process_stat.h"

struct UpdateSnapshot {
  BumpArena owner_arena;
  Array<ProcessStat> stats;
  SteadyTimePoint at;
  SystemTimePoint system_time;
};

struct Sync {
  std::atomic<bool> quit;
  RingBuffer<UpdateSnapshot, 256> update_queue;
};
