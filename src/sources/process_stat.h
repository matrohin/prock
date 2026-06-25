#pragma once

#include "base/base.h"
#include "username.h"

#include <climits>
#include <sys/types.h>

/*
Fields used from /proc/[pid]/stat (see man proc_pid_stat):
  (3) state %c, (4) ppid %d, (14) utime %lu, (15) stime %lu,
  (20) num_threads %ld, (23) vsize %lu
Fields used from /proc/[pid]/statm:
  (2) resident
*/

struct ProcessStat {
  const char *comm;
  const char *cmdline;
  PersistentString username;
  ulong utime;
  ulong stime;
  long num_threads;
  ulong vsize;
  ulong statm_resident;

  // From /proc/[pid]/io
  ulonglong io_read_bytes;  // Actual bytes read from storage
  ulonglong io_write_bytes; // Actual bytes written to storage

  Pid pid;
  Pid ppid;
  char state;
};

// From /proc/stat - all values are cumulative ticks
struct CpuCoreStat {
  ulong user;
  ulong nice;
  ulong system;
  ulong idle;
  ulong iowait;
  ulong irq;
  ulong softirq;

  ulong total() const {
    return user + nice + system + idle + iowait + irq + softirq;
  }
  ulong busy() const { return user + nice + system + irq + softirq; }
  ulong kernel() const { return system + irq + softirq; }
  ulong interrupts() const { return irq + softirq; }
};

// From /proc/meminfo - values in kB
struct MemInfo {
  ulong mem_total;
  ulong mem_free;
  ulong mem_available;
  ulong buffers;
  ulong cached;
  ulong swap_total;
  ulong swap_free;
};

// From /proc/diskstats - aggregated system-wide I/O
// Sector size is typically 512 bytes
struct DiskIoStat {
  ulonglong sectors_read;    // Cumulative sectors read
  ulonglong sectors_written; // Cumulative sectors written
};

// From /proc/net/dev - aggregated system-wide network I/O
struct NetIoStat {
  ulonglong bytes_received;    // Cumulative bytes received
  ulonglong bytes_transmitted; // Cumulative bytes transmitted
};

struct GatheringState {
  SteadyTimePoint last_update;
  GrowingArray<Pid> watched_pids;
  BumpArena watched_pids_arena;
  BumpArena persistent_arena; // storage that outlives a single gather cycle
  UsernameResolver usernames;
};

struct Sync;

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

void gather(GatheringState &state, Sync &sync);

// Query all TCP/UDP sockets via netlink SOCK_DIAG
// Returns array sorted by inode for binary search
Array<SocketEntry> query_sockets_netlink(BumpArena &arena);
