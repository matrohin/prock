#pragma once

#include "actions/on_demand_actions.h"
#include "base/channel.h"
#include "playback/recorder.h"
#include "readers/on_demand_reader.h"
#include "readers/process_stat.h"
#include "state/snapshot.h"

#include <condition_variable>
#include <mutex>

struct Sync {
  std::atomic<bool> quit;
  std::atomic<bool> data_ready;
  std::atomic<float> update_period{0.5f}; // seconds, 0 = paused
  std::mutex quit_mutex;
  std::condition_variable quit_cv;
  Channel<UpdateSnapshot, 256> update_queue;

  // Thread gathering: PIDs to watch/unwatch
  Channel<int, 16> thread_watch_queue;
  Channel<int, 16> thread_unwatch_queue;

  OnDemandReaderSync on_demand_reader;
  OnDemandActionsSync on_demand_actions;
  RecorderSync recorder;
};

void sock_notify_data_ready(Sync &sync);
