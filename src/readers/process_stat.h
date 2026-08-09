#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "username.h"

struct GatheringState {
  uint64_t ticks_in_second;
  SteadyTimeDataPoint last_update;
  GrowingArray<Pid> watched_pids;
  BumpArena watched_pids_arena;
  BumpArena persistent_arena;
  UsernameResolver usernames;
};

struct Sync;

void process_stat_gather(GatheringState &state, Sync &sync);
