#pragma once

#include "actions/dump_writer.h"
#include "base/channel.h"

#include <condition_variable>

// Sibling of the on-demand reader: where the reader fetches /proc data on
// demand, the actions thread performs operations on processes on demand. Today
// that is just the core dump; future process-mutating actions (kill, suspend,
// set priority/affinity, ...) get their own request/response channel pair here,
// the same way the reader has one pair per viewer type.
struct OnDemandActionsSync {
  Channel<DumpRequest, 4> dump_request_queue;
  Channel<DumpResponse, 4> dump_response_queue;
  std::condition_variable request_cv;
};

struct Sync;
void on_demand_actions_loop(Sync &sync);
