#pragma once

#include "base/base.h"
#include "base/containers.h"

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
  // TCP info (only valid for TCP sockets)
  unsigned long long bytes_received;
  unsigned long long bytes_sent;
  SocketProtocol protocol;
  TcpState state;
  unsigned int local_ip;
  unsigned int remote_ip;
  unsigned int tx_queue;
  unsigned int rx_queue;
  unsigned short local_port;
  unsigned short remote_port;
  unsigned char local_ip6[16];
  unsigned char remote_ip6[16];
};

// Query all TCP/UDP sockets via netlink SOCK_DIAG
// Returns array sorted by inode for binary search
// Sets out_errno only when the query fails entirely and nothing is returned;
// partial failures (e.g. no IPv6 support) still produce usable results.
Array<SocketEntry> sock_diag_query(BumpArena &arena, int &out_errno);