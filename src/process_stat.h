#pragma once

#include "base.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/types.h>

/*
man proc
man proc_pid_stat
(1) pid  %d
(2) comm  %s
(3) state  %c
(4) ppid  %d
(5) pgrp  %d
(6) session  %d
(7) tty_nr  %d
(8) tpgid  %d
(9) flags  %u
(10) minflt  %lu
(11) cminflt  %lu
(12) majflt  %lu
(13) cmajflt  %lu
(14) utime  %lu
(15) stime  %lu
(16) cutime  %ld
(17) cstime  %ld
(18) priority  %ld
(19) nice  %ld
(20) num_threads  %ld
(21) itrealvalue  %ld
(22) starttime  %llu
(23) vsize  %lu
(24) rss  %ld
(25) rsslim  %lu
(26) startcode  %lu  [PT]
(27) endcode  %lu  [PT]
(28) startstack  %lu  [PT]
(29) kstkesp  %lu  [PT]
(30) kstkeip  %lu  [PT]
(31) signal  %lu
(32) blocked  %lu
(33) sigignore  %lu
(34) sigcatch  %lu
(35) wchan  %lu  [PT]
(36) nswap  %lu
(37) cnswap  %lu
(38) exit_signal  %d  (since Linux 2.1.22)
(39) processor  %d  (since Linux 2.2.8)
(40) rt_priority  %u  (since Linux 2.5.19)
(41) policy  %u  (since Linux 2.5.19)
(42) delayacct_blkio_ticks  %llu  (since Linux 2.6.18)
(43) guest_time  %lu  (since Linux 2.6.24)
(44) cguest_time  %ld  (since Linux 2.6.24)
(45) start_data  %lu  (since Linux 3.3)  [PT]
(46) end_data  %lu  (since Linux 3.3)  [PT]
(47) start_brk  %lu  (since Linux 3.3)  [PT]
(48) arg_start  %lu  (since Linux 3.5)  [PT]
(49) arg_end  %lu  (since Linux 3.5)  [PT]
(50) env_start  %lu  (since Linux 3.5)  [PT]
(51) env_end  %lu  (since Linux 3.5)  [PT]
(52) exit_code  %d  (since Linux 3.5)  [PT]

statm:
size       (1) total program size (same as VmSize in /proc/[pid]/status)
resident   (2) resident set size (same as VmRSS in /proc/[pid]/status)
shared     (3) number of resident shared pages (i.e., backed by a file) (same as
RssFile+RssShmem in /proc/[pid]/status) text       (4) text (code) lib (5)
library (unused since Linux 2.6; always 0) data       (6) data + stack
*/

struct ProcessStat {
  int pid;
  char comm[64];
  char state;
  int ppid;
  int pgrp;
  int session;
  int tty_nr;
  int tpgid;
  uint flags;
  ulong minflt;
  ulong cminflt;
  ulong majflt;
  ulong cmajflt;
  ulong utime;
  ulong stime;
  long cutime;
  long cstime;
  long priority;
  long nice;
  long num_threads;
  long itrealvalue;
  ulonglong starttime;
  ulong vsize;
  long rss;
  ulong rsslim;
  ulong startcode;
  ulong endcode;
  ulong startstack;
  ulong kstkesp;
  ulong kstkeip;
  ulong signal;
  ulong blocked;
  ulong sigignore;
  ulong sigcatch;
  ulong wchan;
  ulong nswap;
  ulong cnswap;
  int exit_signal;
  int processor;
  uint rt_priority;
  uint policy;
  ulonglong delayacct_blkio_ticks;
  ulong guest_time;
  long cguest_time;
  ulong start_data;
  ulong end_data;
  ulong start_brk;
  ulong arg_start;
  ulong arg_end;
  ulong env_start;
  ulong env_end;
  int exit_code;

  ulong statm_size;
  ulong statm_resident;
  ulong statm_shared;
  ulong statm_text;
  ulong statm_data;

  // From /proc/[pid]/io
  ulonglong io_read_bytes;  // Actual bytes read from storage
  ulonglong io_write_bytes; // Actual bytes written to storage
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

struct GatheringState {
  SteadyTimePoint last_update;
};

struct Sync;
void gather(GatheringState &state, Sync &sync);
