#include "state.h"

#include "base/containers.h"
#include "state/raw_stats.h"
#include "state/snapshot.h"

StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state,
                                    const UpdateSnapshot &snapshot) {
  const StateSnapshot &old = old_state.snapshot;
  const Array<ProcessDerivedStat> derived_stats =
      Array<ProcessDerivedStat>::create(arena, snapshot.stats.size);

  const double time_delta =
      std::chrono::duration_cast<Seconds>(snapshot.at - old.at).count();

  // Normalize per-process CPU% against the /proc/stat jiffy budget for one core
  // (same unit as utime/stime), not wall-clock time. This matches system CPU%
  // below and stays accurate when the gathering interval jitters under load.
  double per_core_ticks = 0.0;
  if (snapshot.cpu_stats.size > 1 && old.cpu_stats.size > 1) {
    const uint64_t total_delta = counter_delta(snapshot.cpu_stats.data[0].total,
                                               old.cpu_stats.data[0].total);
    const uint32_t num_cores = snapshot.cpu_stats.size - 1;
    per_core_ticks = static_cast<double>(total_delta) / num_cores;
  }

  uint32_t old_state_idx = 0;
  for (uint32_t i = 0; i < derived_stats.size; ++i) {
    ProcessDerivedStat &result = derived_stats.data[i];
    const ProcessStat &new_stat = snapshot.stats.data[i];

    // Memory is instantaneous (no previous sample needed), so derive it for
    // every process - including ones appearing for the first time.
    result.mem_resident_bytes =
        new_stat.statm_resident * old_state.system.mem_page_size;
    result.mem_virtual_bytes = new_stat.vsize;

    while (old_state_idx < old.stats.size &&
           new_stat.pid > old.stats.data[old_state_idx].pid) {
      ++old_state_idx;
    }

    // CPU and I/O are rates: they need a matching previous sample and a
    // positive divisor. Without those the zero-initialized result stands. CPU
    // is normalized by jiffies (per_core_ticks); I/O is a wall-clock byte rate.
    if (old_state_idx < old.stats.size &&
        new_stat.pid == old.stats.data[old_state_idx].pid) {
      const ProcessStat &old_stat = old.stats.data[old_state_idx];
      if (per_core_ticks > 0) {
        const double effective_ticks = effective_core_ticks(
            new_stat.read_time, old_stat.read_time, per_core_ticks, time_delta);
        result.cpu_user_perc = counter_rate(new_stat.utime, old_stat.utime,
                                            100.0, effective_ticks);
        result.cpu_kernel_perc = counter_rate(new_stat.stime, old_stat.stime,
                                              100.0, effective_ticks);
      }
      if (time_delta > 0) {
        result.io_read_kb_per_sec =
            counter_rate(new_stat.io_read_bytes, old_stat.io_read_bytes,
                         1.0 / 1024.0, time_delta);
        result.io_write_kb_per_sec =
            counter_rate(new_stat.io_write_bytes, old_stat.io_write_bytes,
                         1.0 / 1024.0, time_delta);
      }
    }
  }

  // Compute system-wide CPU usage percentages
  SystemCpuPerc cpu_perc = {
      Array<double>::create(arena, snapshot.cpu_stats.size),
      Array<double>::create(arena, snapshot.cpu_stats.size),
      Array<double>::create(arena, snapshot.cpu_stats.size),
  };
  for (uint32_t i = 0; i < snapshot.cpu_stats.size && i < old.cpu_stats.size;
       ++i) {
    const CpuCoreStat &cur = snapshot.cpu_stats.data[i];
    const CpuCoreStat &prev = old.cpu_stats.data[i];
    const uint64_t total_delta = counter_delta(cur.total, prev.total);
    const uint64_t busy_delta = counter_delta(cur.busy, prev.busy);
    const uint64_t kernel_delta = counter_delta(cur.kernel, prev.kernel);
    const uint64_t interrupts_delta =
        counter_delta(cur.interrupts, prev.interrupts);
    cpu_perc.total.data[i] =
        total_delta > 0 ? busy_delta * 100.0 / total_delta : 0.0;
    cpu_perc.kernel.data[i] =
        total_delta > 0 ? kernel_delta * 100.0 / total_delta : 0.0;
    cpu_perc.interrupts.data[i] =
        total_delta > 0 ? interrupts_delta * 100.0 / total_delta : 0.0;
  }

  // Compute disk I/O rates in MB/s
  // /proc/diskstats always uses 512-byte sectors regardless of hardware sector
  // size
  constexpr double SECTOR_SIZE = 512.0;
  constexpr double BYTES_TO_MB = 1.0 / (1024.0 * 1024.0);
  DiskIoRate disk_io_rate = {};
  if (time_delta > 0 && old.disk_io_stats.sectors_read > 0) {
    disk_io_rate.read_mb_per_sec = counter_rate(
        snapshot.disk_io_stats.sectors_read, old.disk_io_stats.sectors_read,
        SECTOR_SIZE * BYTES_TO_MB, time_delta);
    disk_io_rate.write_mb_per_sec =
        counter_rate(snapshot.disk_io_stats.sectors_written,
                     old.disk_io_stats.sectors_written,
                     SECTOR_SIZE * BYTES_TO_MB, time_delta);
  }

  // Compute network I/O rates in MB/s
  NetIoRate net_io_rate = {};
  if (time_delta > 0 && old.net_io_stats.bytes_received > 0) {
    net_io_rate.recv_mb_per_sec =
        counter_rate(snapshot.net_io_stats.bytes_received,
                     old.net_io_stats.bytes_received, BYTES_TO_MB, time_delta);
    net_io_rate.send_mb_per_sec = counter_rate(
        snapshot.net_io_stats.bytes_transmitted,
        old.net_io_stats.bytes_transmitted, BYTES_TO_MB, time_delta);
  }

  return StateSnapshot{snapshot.stats,     derived_stats,
                       snapshot.cpu_stats, cpu_perc,
                       snapshot.mem_info,  snapshot.disk_io_stats,
                       disk_io_rate,       snapshot.net_io_stats,
                       net_io_rate,        snapshot.thread_snapshots,
                       snapshot.at,        per_core_ticks,
                       time_delta};
}
