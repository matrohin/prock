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

  SteadyTimePoint read_time; // When utime/stime were sampled

  Pid pid;
  Pid ppid;
  int32_t last_cpu;
  char state;
};

// From /proc/stat - all values are cumulative ticks
struct CpuCoreStat {
  uint64_t user;
  uint64_t nice;
  uint64_t system;
  uint64_t idle;
  uint64_t iowait;
  uint64_t irq;
  uint64_t softirq;

  uint64_t total() const {
    return user + nice + system + idle + iowait + irq + softirq;
  }
  uint64_t busy() const { return user + nice + system + irq + softirq; }
  uint64_t kernel() const { return system + irq + softirq; }
  uint64_t interrupts() const { return irq + softirq; }
};

// From /proc/meminfo - values in kB
struct MemInfo {
  uint64_t mem_total;
  uint64_t mem_free;
  uint64_t mem_available;
  uint64_t buffers;
  uint64_t cached;
  uint64_t swap_total;
  uint64_t swap_free;
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