#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "state/raw_stats.h"
#include "state/serialize.h"
#include "state/system_info.h"

// Builds a synthetic .prck recording through the production serializer
struct FakeRecording {
  BumpArena arena;
  SerializeBuffer buffer;
  SerializeControl control;
  SystemInfo system;
  GrowingArray<ProcessStat> processes;
  Array<CpuCoreStat> cpu_stats; // [0] = total, [1..n] = per-core
  uint64_t uptime_ticks;
  int64_t at_ns;

  // Writes the header and the SystemInfo preamble
  static FakeRecording create(const SystemInfo &system, uint32_t num_cores);

  ProcessStat &add(const ProcessStat &proc);
  ProcessStat *find(Pid pid);
  void remove(Pid pid);

  // Bump every process's counters, so per-process rates are non-zero.
  void advance(uint64_t utime, uint64_t stime, uint64_t io_read,
               uint64_t io_write);

  void record(int64_t record_at_ns);

  bool write(const char *path);
  void destroy();
};
