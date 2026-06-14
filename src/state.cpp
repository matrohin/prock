#include "state.h"
#include "sources/sync.h"

// Per-second rate between two samples of a monotonically increasing kernel
// counter. A "current" below "previous" means the counter was reset (e.g. a
// network interface bouncing or a reboot), so clamp the delta to zero instead
// of letting the unsigned subtraction wrap into a huge spike. The caller
// guarantees divisor > 0.
static double counter_rate(const ulonglong cur, const ulonglong prev,
                           const double scale, const double divisor) {
  const ulonglong delta = cur >= prev ? cur - prev : 0;
  return static_cast<double>(delta) * scale / divisor;
}

StateSnapshot state_snapshot_update(BumpArena &arena, const State &old_state,
                                    const UpdateSnapshot &snapshot) {
  const StateSnapshot &old = old_state.snapshot;
  const Array<ProcessDerivedStat> derived_stats =
      Array<ProcessDerivedStat>::create(arena, snapshot.stats.size);

  const double time_delta =
      std::chrono::duration_cast<Seconds>(snapshot.at - old.at).count();
  const double ticks_passed = old_state.system.ticks_in_second * time_delta;

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

    // CPU and I/O are rates: they need both a matching previous sample and a
    // positive interval (which keeps ticks_passed > 0). Without those the
    // zero-initialized result stands.
    if (old_state_idx < old.stats.size &&
        new_stat.pid == old.stats.data[old_state_idx].pid && time_delta > 0) {
      const ProcessStat &old_stat = old.stats.data[old_state_idx];
      result.cpu_user_perc =
          counter_rate(new_stat.utime, old_stat.utime, 100.0, ticks_passed);
      result.cpu_kernel_perc =
          counter_rate(new_stat.stime, old_stat.stime, 100.0, ticks_passed);
      result.io_read_kb_per_sec =
          counter_rate(new_stat.io_read_bytes, old_stat.io_read_bytes,
                       1.0 / 1024.0, time_delta);
      result.io_write_kb_per_sec =
          counter_rate(new_stat.io_write_bytes, old_stat.io_write_bytes,
                       1.0 / 1024.0, time_delta);
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
    const ulong total_delta = cur.total() - prev.total();
    const ulong busy_delta = cur.busy() - prev.busy();
    const ulong kernel_delta = cur.kernel() - prev.kernel();
    const ulong interrupts_delta = cur.interrupts() - prev.interrupts();
    cpu_perc.total.data[i] =
        total_delta > 0 ? (busy_delta * 100.0) / total_delta : 0.0;
    cpu_perc.kernel.data[i] =
        total_delta > 0 ? (kernel_delta * 100.0) / total_delta : 0.0;
    cpu_perc.interrupts.data[i] =
        total_delta > 0 ? (interrupts_delta * 100.0) / total_delta : 0.0;
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
                       snapshot.at};
}
