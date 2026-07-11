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
    OpenFilesRequest open_files_request;
    SmapsRequest smaps_request;
    PortScanRequest port_scan_request;
    FontListRequest font_list_request;
    PropertiesRequest properties_request;
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      my_sync.request_read_cv.wait(lock, [&] {
        return sync.quit.load() ||
               my_sync.library_request_queue.peek(lib_request) ||
               my_sync.environ_request_queue.peek(env_request) ||
               my_sync.socket_request_queue.peek(sock_request) ||
               my_sync.open_files_request_queue.peek(open_files_request) ||
               my_sync.smaps_request_queue.peek(smaps_request) ||
               my_sync.port_scan_request_queue.peek(port_scan_request) ||
               my_sync.font_list_request_queue.peek(font_list_request) ||
               my_sync.properties_request_queue.peek(properties_request);
      });
    }
    if (sync.quit.load()) break;

    while (my_sync.library_request_queue.pop(lib_request)) {
      ZoneScopedN("library_request");
      ZoneValue(lib_request.pid);
      LibraryResponse response = library_reader_read(temp_arena, lib_request);
      if (!my_sync.library_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.environ_request_queue.pop(env_request)) {
      ZoneScopedN("environ_request");
      ZoneValue(env_request.pid);
      EnvironResponse response = environ_reader_read(temp_arena, env_request);
      if (!my_sync.environ_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.socket_request_queue.pop(sock_request)) {
      ZoneScopedN("socket_request");
      ZoneValue(sock_request.pid);
      SocketResponse response = socket_reader_read(temp_arena, sock_request);
      if (!my_sync.socket_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.open_files_request_queue.pop(open_files_request)) {
      ZoneScopedN("open_files_request");
      ZoneValue(open_files_request.pid);
      OpenFilesResponse response =
          open_files_reader_read(temp_arena, open_files_request);
      if (!my_sync.open_files_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.smaps_request_queue.pop(smaps_request)) {
      ZoneScopedN("smaps_request");
      ZoneValue(smaps_request.pid);
      SmapsResponse response = smaps_reader_read(temp_arena, smaps_request);
      if (!my_sync.smaps_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.port_scan_request_queue.pop(port_scan_request)) {
      ZoneScopedN("port_scan_request");
      PortScanResponse response =
          port_scan_reader_read(temp_arena, port_scan_request);
      if (!my_sync.port_scan_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.font_list_request_queue.pop(font_list_request)) {
      ZoneScopedN("font_list_request");
      FontListResponse response = font_list_reader_read(temp_arena);
      if (!my_sync.font_list_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.properties_request_queue.pop(properties_request)) {
      ZoneScopedN("properties_request");
      ZoneValue(properties_request.pid);
      PropertiesResponse response =
          properties_reader_read(temp_arena, properties_request);
      if (!my_sync.properties_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    sock_notify_data_ready(sync);
    if (temp_arena.cur_slab &&
        (temp_arena.cur_slab->prev ||
         temp_arena.cur_slab->left_size < SLAB_SIZE / 10)) {
      temp_arena.destroy();
    }
  }
}
