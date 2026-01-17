#include "state.h"
#include "sync.h"


StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state, const UpdateSnapshot &snapshot) {
  const StateSnapshot &old = old_state.snapshot;
  Array<ProcessDerivedStat> derived_stats = Array<ProcessDerivedStat>::create(arena, snapshot.stats.size);

  const double ticks_passed = old_state.system.ticks_in_second *
    std::chrono::duration_cast<Seconds>(snapshot.at - old.at).count();

  size_t old_state_idx = 0;
  for (size_t i = 0; i < derived_stats.size; ++i) {
    ProcessDerivedStat &result = derived_stats.data[i];
    const ProcessStat &new_stat = snapshot.stats.data[i];

    while (old_state_idx < old.stats.size && new_stat.pid > old.stats.data[old_state_idx].pid) {
      ++old_state_idx;
    }

    if (old_state_idx < old.stats.size && new_stat.pid == old.stats.data[old_state_idx].pid) {
      const ProcessStat &old_stat = old.stats.data[old_state_idx];
      if (new_stat.utime >= old_stat.utime) {
        result.cpu_user_perc = (new_stat.utime - old_stat.utime) / ticks_passed * 100;
      }
      if (new_stat.stime >= old_stat.stime) {
        result.cpu_kernel_perc = (new_stat.stime - old_stat.stime) / ticks_passed * 100;
      }
      result.mem_resident_bytes = new_stat.statm_resident * old_state.system.mem_page_size;
    }
  }

  // Compute system-wide CPU usage percentages
  SystemCpuPerc cpu_perc = {
    Array<double>::create(arena, snapshot.cpu_stats.size),
    Array<double>::create(arena, snapshot.cpu_stats.size),
    Array<double>::create(arena, snapshot.cpu_stats.size),
  };
  for (size_t i = 0; i < snapshot.cpu_stats.size && i < old.cpu_stats.size; ++i) {
    const CpuCoreStat &cur = snapshot.cpu_stats.data[i];
    const CpuCoreStat &prev = old.cpu_stats.data[i];
    ulong total_delta = cur.total() - prev.total();
    ulong busy_delta = cur.busy() - prev.busy();
    ulong kernel_delta = cur.kernel() - prev.kernel();
    ulong interrupts_delta = cur.interrupts() - prev.interrupts();
    cpu_perc.total.data[i] = total_delta > 0 ? (busy_delta * 100.0) / total_delta : 0.0;
    cpu_perc.kernel.data[i] = total_delta > 0 ? (kernel_delta * 100.0) / total_delta : 0.0;
    cpu_perc.interrupts.data[i] = total_delta > 0 ? (interrupts_delta * 100.0) / total_delta : 0.0;
  }

  // Compute disk I/O rates in MB/s
  // /proc/diskstats always uses 512-byte sectors regardless of hardware sector size
  constexpr double SECTOR_SIZE = 512.0;
  constexpr double BYTES_TO_MB = 1.0 / (1024.0 * 1024.0);
  DiskIoRate disk_io_rate = {};
  const double time_delta = std::chrono::duration_cast<Seconds>(snapshot.at - old.at).count();
  if (time_delta > 0 && old.disk_io_stats.sectors_read > 0) {
    ulonglong read_sectors_delta = snapshot.disk_io_stats.sectors_read - old.disk_io_stats.sectors_read;
    ulonglong write_sectors_delta = snapshot.disk_io_stats.sectors_written - old.disk_io_stats.sectors_written;
    disk_io_rate.read_mb_per_sec = (read_sectors_delta * SECTOR_SIZE * BYTES_TO_MB) / time_delta;
    disk_io_rate.write_mb_per_sec = (write_sectors_delta * SECTOR_SIZE * BYTES_TO_MB) / time_delta;
  }

  return StateSnapshot{snapshot.stats, derived_stats, snapshot.cpu_stats, cpu_perc, snapshot.mem_info, snapshot.disk_io_stats, disk_io_rate, snapshot.at};
}
