#pragma once

#include "../base/ring_buffer.h"
#include "sources/environ_reader.h"
#include "sources/font_list_reader.h"
#include "sources/library_reader.h"
#include "sources/smaps_reader.h"
#include "sources/socket_reader.h"

#include <condition_variable>

struct OnDemandReaderSync {
  Channel<LibraryRequest, 16> library_request_queue;
  Channel<LibraryResponse, 16> library_response_queue;
  Channel<EnvironRequest, 16> environ_request_queue;
  Channel<EnvironResponse, 16> environ_response_queue;
  Channel<SocketRequest, 16> socket_request_queue;
  Channel<SocketResponse, 16> socket_response_queue;
  Channel<SmapsRequest, 16> smaps_request_queue;
  Channel<SmapsResponse, 16> smaps_response_queue;
  Channel<FontListRequest, 2> font_list_request_queue;
  Channel<FontListResponse, 2> font_list_response_queue;
  std::condition_variable library_cv;
};

struct Sync;
void on_demand_reader_loop(Sync &sync);