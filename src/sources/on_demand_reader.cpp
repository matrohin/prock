#include "on_demand_reader.h"

#include "environ_reader.h"
#include "smaps_reader.h"
#include "sync.h"

#include "tracy/Tracy.hpp"

#include <mutex>

void on_demand_reader_loop(Sync &sync) {
  OnDemandReaderSync &my_sync = sync.on_demand_reader;
  BumpArena temp_arena;
  while (!sync.quit.load()) {
    LibraryRequest lib_request;
    EnvironRequest env_request;
    SocketRequest sock_request;
    SmapsRequest smaps_request;
    FontListRequest font_list_request;
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      my_sync.library_cv.wait(lock, [&] {
        return sync.quit.load() ||
               my_sync.library_request_queue.peek(lib_request) ||
               my_sync.environ_request_queue.peek(env_request) ||
               my_sync.socket_request_queue.peek(sock_request) ||
               my_sync.smaps_request_queue.peek(smaps_request) ||
               my_sync.font_list_request_queue.peek(font_list_request);
      });
    }
    if (sync.quit.load()) break;

    while (my_sync.library_request_queue.pop(lib_request)) {
      ZoneScopedN("library_request");
      ZoneValue(lib_request.pid);
      LibraryResponse response =
          read_process_libraries(temp_arena, lib_request);
      if (!my_sync.library_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.environ_request_queue.pop(env_request)) {
      ZoneScopedN("environ_request");
      ZoneValue(env_request.pid);
      EnvironResponse response = read_process_environ(temp_arena, env_request);
      if (!my_sync.environ_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.socket_request_queue.pop(sock_request)) {
      ZoneScopedN("socket_request");
      ZoneValue(sock_request.pid);
      SocketResponse response = read_process_sockets(temp_arena, sock_request);
      if (!my_sync.socket_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.smaps_request_queue.pop(smaps_request)) {
      ZoneScopedN("smaps_request");
      ZoneValue(smaps_request.pid);
      SmapsResponse response = read_process_smaps(temp_arena, smaps_request);
      if (!my_sync.smaps_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.font_list_request_queue.pop(font_list_request)) {
      ZoneScopedN("font_list_request");
      FontListResponse response = read_font_list(temp_arena);
      if (!my_sync.font_list_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    notify_data_ready(sync);
    if (temp_arena.cur_slab &&
        (temp_arena.cur_slab->prev ||
         temp_arena.cur_slab->left_size < SLAB_SIZE / 10)) {
      temp_arena.destroy();
    }
  }
}
