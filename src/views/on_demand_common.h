#pragma once

#include "base/string.h"
#include "sync.h"

#include <cerrno>
#include <mutex>

enum OnDemandViewerStatus {
  eOnDemandViewerStatus_Loading,
  eOnDemandViewerStatus_Ready,
  eOnDemandViewerStatus_Error,
};

String on_demand_viewer_title(BumpArena &frame_arena,
                              OnDemandViewerStatus status,
                              const char *viewer_name, uint32_t results_size,
                              const char *process_name, Pid pid);

// Push a request to an on-demand reader queue - under the same mutex its CV
// wait predicate is checked with - and wake the reader. Returns false when the
// queue is full and the request was dropped.
template <class Req, uint32_t N>
bool on_demand_send_request(Sync &sync, Channel<Req, N> &queue,
                            const Req &req) {
  bool pushed = false;
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    pushed = queue.push(req);
  }
  if (pushed) sync.on_demand_reader.request_read_cv.notify_one();
  return pushed;
}

// Fail a viewer window whose initial request was dropped, so it shows an error
// instead of loading forever.
template <class Win> void on_demand_mark_request_dropped(Win &win) {
  win.status = eOnDemandViewerStatus_Error;
  win.error_code = EAGAIN;
}