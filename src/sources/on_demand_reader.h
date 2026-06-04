#pragma once

#include "base/channel.h"
#include "sources/environ_reader.h"
#include "sources/font_list_reader.h"
#include "sources/library_reader.h"
#include "sources/port_scan_reader.h"
#include "sources/smaps_reader.h"
#include "sources/socket_reader.h"

#include <condition_variable>

struct OnDemandReaderSync {
  Channel<LibraryRequest, 8> library_request_queue;
  Channel<LibraryResponse, 8> library_response_queue;
  Channel<EnvironRequest, 8> environ_request_queue;
  Channel<EnvironResponse, 8> environ_response_queue;
  Channel<SocketRequest, 8> socket_request_queue;
  Channel<SocketResponse, 8> socket_response_queue;
  Channel<SmapsRequest, 8> smaps_request_queue;
  Channel<SmapsResponse, 8> smaps_response_queue;
  Channel<PortScanRequest, 2> port_scan_request_queue;
  Channel<PortScanResponse, 2> port_scan_response_queue;
  Channel<FontListRequest, 2> font_list_request_queue;
  Channel<FontListResponse, 2> font_list_response_queue;
  std::condition_variable request_read_cv;
};

struct Sync;
void on_demand_reader_loop(Sync &sync);