#include "on_demand_reader.h"

#include "environ_reader.h"
#include "sync.h"

#include "GLFW/glfw3.h"

#include <mutex>

void on_demand_reader_loop(Sync &sync) {
  OnDemandReaderSync &my_sync = sync.on_demand_reader;
  BumpArena temp_arena;
  while (!sync.quit.load()) {
    LibraryRequest lib_request;
    EnvironRequest env_request;
    SocketRequest sock_request;
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      my_sync.library_cv.wait(lock, [&] {
        return sync.quit.load() ||
               my_sync.library_request_queue.peek(lib_request) ||
               my_sync.environ_request_queue.peek(env_request) ||
               my_sync.socket_request_queue.peek(sock_request);
      });
    }
    if (sync.quit.load()) break;

    while (my_sync.library_request_queue.pop(lib_request)) {
      LibraryResponse response =
          read_process_libraries(temp_arena, lib_request);
      if (!my_sync.library_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.environ_request_queue.pop(env_request)) {
      EnvironResponse response = read_process_environ(temp_arena, env_request);
      if (!my_sync.environ_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    while (my_sync.socket_request_queue.pop(sock_request)) {
      SocketResponse response = read_process_sockets(temp_arena, sock_request);
      if (!my_sync.socket_response_queue.push(response)) {
        response.owner_arena.destroy();
      }
    }

    glfwPostEmptyEvent();
    if (temp_arena.cur_slab &&
        (temp_arena.cur_slab->prev ||
         temp_arena.cur_slab->left_size < SLAB_SIZE / 10)) {
      temp_arena.destroy();
    }
  }
}
