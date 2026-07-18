#pragma once

#include <cstdint>

#include "base/base.h"
#include "base/const_string.h"

// From /proc/<pid>/...
struct ProcessStat {
  const char *comm;
  const char *cmdline;
  const char *wchan;
  PersistentString username;
  uint64_t utime;
  uint64_t stime;
  int32_t num_threads;
  int32_t nice;
  uint64_t vsize;
  uint64_t statm_resident;
  uint64_t starttime;

  // From /proc/[pid]/io
  uint64_t io_read_bytes;
  uint64_t io_write_bytes;

  SteadyTimeDataPoint read_time_ns; // When utime/stime were sampled

  Pid pid;
  Pid ppid;
  int32_t last_cpu;
  char state;
};

// From /proc/stat - cumulative tick aggregates
struct CpuCoreStat {
  uint64_t total;      // user+nice+system+idle+iowait+irq+softirq
  uint64_t busy;       // user+nice+system+irq+softirq
  uint64_t kernel;     // system+irq+softirq
  uint64_t interrupts; // irq+softirq
};

inline CpuCoreStat cpu_core_stat_from_ticks(ulong user, ulong nice,
                                            ulong system, ulong idle,
                                            ulong iowait, ulong irq,
                                            ulong softirq) {
  return {user + nice + system + idle + iowait + irq + softirq,
          user + nice + system + irq + softirq, system + irq + softirq,
          irq + softirq};
}

// From /proc/meminfo - values in kB
struct MemInfo {
  uint64_t mem_total;
  uint64_t mem_available;
};

// From /proc/diskstats - cumulative system-wide I/O
struct DiskIoStat {
  uint64_t sectors_read;
  uint64_t sectors_written;
};

// From /proc/net/dev - cumulative system-wide network I/O
struct NetIoStat {
  uint64_t bytes_received;
  uint64_t bytes_transmitted;
};