#pragma once

#include "ring_buffer.h"
#include "sources/environ_reader.h"
#include "sources/library_reader.h"
#include "sources/socket_reader.h"

#include <condition_variable>

struct OnDemandReaderSync {
  RingBuffer<LibraryRequest, 16> library_request_queue;
  RingBuffer<LibraryResponse, 16> library_response_queue;
  RingBuffer<EnvironRequest, 16> environ_request_queue;
  RingBuffer<EnvironResponse, 16> environ_response_queue;
  RingBuffer<SocketRequest, 16> socket_request_queue;
  RingBuffer<SocketResponse, 16> socket_response_queue;
  std::condition_variable library_cv;
};

struct Sync;
void on_demand_reader_loop(Sync &sync);