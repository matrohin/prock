#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "username.h"

#include <climits>
#include <sys/types.h>

/*
Fields used from /proc/[pid]/stat (see man proc_pid_stat):
  (3) state %c, (4) ppid %d, (14) utime %lu, (15) stime %lu,
  (19) nice %ld, (20) num_threads %ld, (22) starttime %llu, (23) vsize %lu,
  (39) processor %d
Fields used from /proc/[pid]/statm:
  (2) resident
*/

struct ProcessStat {
  const char *comm;
  const char *cmdline;
  // Symbolic name of the kernel function the task is blocked in
  // (/proc/.../wchan), "" when running or unknown. Only populated for threads.
  const char *wchan;
  PersistentString username;
  ulong utime;
  ulong stime;
  long num_threads;
  long nice; // Scheduling nice value, -20 (highest prio) .. 19 (lowest)
  ulong vsize;
  ulong statm_resident;
  ulonglong starttime; // Clock ticks since boot (/proc/[pid]/stat field 22)

  // From /proc/[pid]/io
  ulonglong io_read_bytes;  // Actual bytes read from storage
  ulonglong io_write_bytes; // Actual bytes written to storage

  SteadyTimePoint read_time; // When utime/stime were sampled

  Pid pid;
  Pid ppid;
  int last_cpu; // Last CPU the task ran on (/proc/.../stat field 39), -1
                // unknown
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
  BumpArena persistent_arena; // storage that outlives a single
                              // process_stat_gather cycle
  UsernameResolver usernames;
};

struct Sync;

void process_stat_gather(GatheringState &state, Sync &sync);
