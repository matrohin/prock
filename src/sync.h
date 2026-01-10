#pragma once

#include "base.h"
#include "process_stat.h"

#include <condition_variable>
#include <mutex>

struct UpdateSnapshot {
  BumpArena owner_arena;
  Array<ProcessStat> stats;
  SteadyTimePoint at;
  SystemTimePoint system_time;
};

struct Sync {
  std::atomic<bool> quit;
  std::mutex quit_mutex;
  std::condition_variable quit_cv;
  RingBuffer<UpdateSnapshot, 256> update_queue;
};
