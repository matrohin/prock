#pragma once

#include "base.h"
#include "ring_buffer.h"
#include "process_stat.h"

#include <condition_variable>
#include <mutex>

constexpr int MAX_WATCHED_PIDS = 16;

struct ThreadSnapshot {
  int pid;
  Array<ProcessStat> threads;  // Reuse ProcessStat - same format for threads
};

struct UpdateSnapshot {
  BumpArena owner_arena;
  Array<ProcessStat> stats;
  Array<CpuCoreStat> cpu_stats; // [0]=total, [1..n]=per-core
  MemInfo mem_info;
  DiskIoStat disk_io_stats;
  NetIoStat net_io_stats;
  Array<ThreadSnapshot> thread_snapshots;  // Per-watched-pid thread data
  SteadyTimePoint at;
  SystemTimePoint system_time;
};

struct LibraryEntry {
  const char *path;
  size_t path_len;
  unsigned long addr_start;
  unsigned long addr_end;
  long file_size; // -1 if stat failed
};

struct LibraryRequest {
  int pid;
};

struct LibraryResponse {
  int pid;
  int error_code; // 0=success, errno otherwise
  BumpArena owner_arena;
  Array<LibraryEntry> libraries;
};

struct EnvironEntry {
  const char *name;
  const char *value;
  size_t name_len;
  size_t value_len;
};

struct EnvironRequest {
  int pid;
};

struct EnvironResponse {
  int pid;
  int error_code; // 0=success, errno otherwise
  BumpArena owner_arena;
  Array<EnvironEntry> entries;
};

enum SocketProtocol {
  eSocketProtocol_TCP,
  eSocketProtocol_UDP,
  eSocketProtocol_TCP6,
  eSocketProtocol_UDP6,
};

enum TcpState {
  eTcpState_ESTABLISHED = 1,
  eTcpState_SYN_SENT = 2,
  eTcpState_SYN_RECV = 3,
  eTcpState_FIN_WAIT1 = 4,
  eTcpState_FIN_WAIT2 = 5,
  eTcpState_TIME_WAIT = 6,
  eTcpState_CLOSE = 7,
  eTcpState_CLOSE_WAIT = 8,
  eTcpState_LAST_ACK = 9,
  eTcpState_LISTEN = 10,
  eTcpState_CLOSING = 11,
};

struct SocketEntry {
  unsigned long inode;
  int protocol;  // SocketProtocol
  int state;     // TcpState
  unsigned int local_ip;
  unsigned short local_port;
  unsigned int remote_ip;
  unsigned short remote_port;
  unsigned int tx_queue;
  unsigned int rx_queue;
  unsigned char local_ip6[16];
  unsigned char remote_ip6[16];
  // TCP info (only valid for TCP sockets)
  unsigned long long bytes_received;
  unsigned long long bytes_sent;
};

struct SocketRequest {
  int pid;
};

struct SocketResponse {
  int pid;
  int error_code;
  BumpArena owner_arena;
  Array<SocketEntry> sockets;
};

struct Sync {
  std::atomic<bool> quit;
  std::atomic<float> update_period{0.5f};  // seconds, 0 = paused
  std::mutex quit_mutex;
  std::condition_variable quit_cv;
  RingBuffer<UpdateSnapshot, 256> update_queue;

  // Library/environ/socket reader thread communication
  RingBuffer<LibraryRequest, 16> library_request_queue;
  RingBuffer<LibraryResponse, 16> library_response_queue;
  RingBuffer<EnvironRequest, 16> environ_request_queue;
  RingBuffer<EnvironResponse, 16> environ_response_queue;
  RingBuffer<SocketRequest, 16> socket_request_queue;
  RingBuffer<SocketResponse, 16> socket_response_queue;
  std::condition_variable library_cv;

  // Thread gathering: PIDs to gather threads for, 0=empty slot
  std::atomic<int> watched_pids[MAX_WATCHED_PIDS];
  std::atomic<int> watched_pids_count{0};
};
