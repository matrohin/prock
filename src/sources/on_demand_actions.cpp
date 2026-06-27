#include "on_demand_actions.h"

#include "sync.h"

#include "tracy/Tracy.hpp"

#include <mutex>

void on_demand_actions_loop(Sync &sync) {
  OnDemandActionsSync &my_sync = sync.on_demand_actions;
  while (!sync.quit.load()) {
    DumpRequest dump_request;
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      my_sync.request_cv.wait(lock, [&] {
        return sync.quit.load() ||
               my_sync.dump_request_queue.peek(dump_request);
      });
    }
    if (sync.quit.load()) break;

    while (my_sync.dump_request_queue.pop(dump_request)) {
      ZoneScopedN("dump_request");
      ZoneValue(dump_request.pid);
      DumpResponse response = write_process_dump(dump_request, sync.quit);
      response.id = dump_request.id;
      // Can't drop: the response queue capacity matches the request queue and
      // actions run serially, so every popped request leaves room for its
      // reply.
      my_sync.dump_response_queue.push(response);
    }

    notify_data_ready(sync);
  }
}
