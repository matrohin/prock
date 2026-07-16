#pragma once

#include "base/base.h"
#include "base/const_string.h"

// From /proc/<pid>/...
struct ProcessStat {
  const char *comm;
  const char *cmdline;
  const char *wchan;
  PersistentString username;
  ulong utime;
  ulong stime;
  long num_threads;
  long nice;
  ulong vsize;
  ulong statm_resident;
  ulonglong starttime;

  // From /proc/[pid]/io
  ulonglong io_read_bytes;
  ulonglong io_write_bytes;

  SteadyTimePoint read_time; // When utime/stime were sampled

  Pid pid;
  Pid ppid;
  int last_cpu;
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

// From /proc/diskstats - cumulative system-wide I/O
struct DiskIoStat {
  ulonglong sectors_read;
  ulonglong sectors_written;
};

// From /proc/net/dev - cumulative system-wide network I/O
struct NetIoStat {
  ulonglong bytes_received;
  ulonglong bytes_transmitted;
};