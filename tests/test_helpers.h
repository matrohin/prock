#pragma once

#include "base.h"
#include "state.h"

#include <cstring>

// Helper to create a ProcessStat with minimal required fields
inline ProcessStat make_process_stat(int pid, int ppid, const char *comm,
                                     char state = 'S') {
  ProcessStat stat = {};
  stat.pid = pid;
  stat.ppid = ppid;
  stat.state = state;
  strncpy(stat.comm, comm, sizeof(stat.comm) - 1);
  return stat;
}

// Helper to create a ProcessDerivedStat with specified values
inline ProcessDerivedStat make_derived_stat(double cpu_user = 0.0,
                                            double cpu_kernel = 0.0,
                                            double mem_bytes = 0.0,
                                            double io_read = 0.0,
                                            double io_write = 0.0) {
  ProcessDerivedStat derived = {};
  derived.cpu_user_perc = cpu_user;
  derived.cpu_kernel_perc = cpu_kernel;
  derived.mem_resident_bytes = mem_bytes;
  derived.io_read_kb_per_sec = io_read;
  derived.io_write_kb_per_sec = io_write;
  return derived;
}

// Builder for creating test StateSnapshots
struct SnapshotBuilder {
  BumpArena &arena;
  GrowingArray<ProcessStat> stats;
  GrowingArray<ProcessDerivedStat> derived;
  size_t wasted = 0;

  explicit SnapshotBuilder(BumpArena &a) : arena(a), stats{}, derived{} {}

  SnapshotBuilder &add(int pid, int ppid, const char *comm, char state = 'S',
                       double cpu_user = 0.0, double cpu_kernel = 0.0,
                       double mem_bytes = 0.0) {
    *stats.emplace_back(arena, wasted) =
        make_process_stat(pid, ppid, comm, state);
    *derived.emplace_back(arena, wasted) =
        make_derived_stat(cpu_user, cpu_kernel, mem_bytes);
    return *this;
  }

  // Build the snapshot - stats must be sorted by PID
  StateSnapshot build() {
    // Sort by PID (required by binary_search_pid)
    for (size_t i = 1; i < stats.size(); ++i) {
      for (size_t j = i; j > 0 && stats.data()[j].pid < stats.data()[j - 1].pid;
           --j) {
        ProcessStat tmp_s = stats.data()[j];
        stats.data()[j] = stats.data()[j - 1];
        stats.data()[j - 1] = tmp_s;
        ProcessDerivedStat tmp_d = derived.data()[j];
        derived.data()[j] = derived.data()[j - 1];
        derived.data()[j - 1] = tmp_d;
      }
    }

    StateSnapshot snapshot = {};
    snapshot.stats.data = stats.data();
    snapshot.stats.size = stats.size();
    snapshot.derived_stats.data = derived.data();
    snapshot.derived_stats.size = derived.size();
    return snapshot;
  }
};
