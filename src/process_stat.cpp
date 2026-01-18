#include "process_stat.h"

#include "sync.h"

#include <algorithm>

namespace {

bool read_process(int pid, ProcessStat *out) {
  constexpr size_t PATH_BUF_SIZE = 64;

  char stat_filename[PATH_BUF_SIZE];
  snprintf(stat_filename, PATH_BUF_SIZE, "/proc/%d/stat", pid);

  char statm_filename[PATH_BUF_SIZE];
  snprintf(statm_filename, PATH_BUF_SIZE, "/proc/%d/statm", pid);

  char comm_filename[PATH_BUF_SIZE];
  snprintf(comm_filename, PATH_BUF_SIZE, "/proc/%d/comm", pid);

  char io_filename[PATH_BUF_SIZE];
  snprintf(io_filename, PATH_BUF_SIZE, "/proc/%d/io", pid);

  ProcessStat &stat = *out;
  stat.pid = pid;
  stat.comm[0] = '\0';
  stat.io_read_bytes = 0;
  stat.io_write_bytes = 0;

  FILE *stat_file = fopen(stat_filename, "r");
  FILE *statm_file = fopen(statm_filename, "r");
  FILE *comm_file = fopen(comm_filename, "r");
  if (!stat_file || !statm_file || !comm_file) {
    if (stat_file) fclose(stat_file);
    if (statm_file) fclose(statm_file);
    if (comm_file) fclose(comm_file);
    return false;
  }

  // Read all files into buffers
  char stat_buf[512];
  char statm_buf[128];

  if (!fgets(stat_buf, sizeof(stat_buf), stat_file)) {
    fclose(comm_file);
    fclose(statm_file);
    fclose(stat_file);
    return false;
  }
  if (!fgets(statm_buf, sizeof(statm_buf), statm_file)) {
    fclose(comm_file);
    fclose(statm_file);
    fclose(stat_file);
    return false;
  }
  if (fgets(stat.comm, sizeof(stat.comm), comm_file)) {
    // Strip trailing newline
    size_t len = strlen(stat.comm);
    if (len > 0 && stat.comm[len - 1] == '\n') {
      stat.comm[len - 1] = '\0';
    }
  }

  fclose(comm_file);
  fclose(statm_file);
  fclose(stat_file);

  // Find last ')' - comm can contain unbalanced parens
  char *after_comm = strrchr(stat_buf, ')');
  if (!after_comm) {
    return false;
  }

  sscanf(after_comm + 1,
         " %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld "
         "%ld %ld %llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
         "%lu %d %d %u %u %llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %d",
         &stat.state, &stat.ppid, &stat.pgrp,
         &stat.session, &stat.tty_nr, &stat.tpgid, &stat.flags, &stat.minflt,
         &stat.cminflt, &stat.majflt, &stat.cmajflt, &stat.utime, &stat.stime,
         &stat.cutime, &stat.cstime, &stat.priority, &stat.nice,
         &stat.num_threads, &stat.itrealvalue, &stat.starttime, &stat.vsize,
         &stat.rss, &stat.rsslim, &stat.startcode, &stat.endcode,
         &stat.startstack, &stat.kstkesp, &stat.kstkeip, &stat.signal,
         &stat.blocked, &stat.sigignore, &stat.sigcatch, &stat.wchan,
         &stat.nswap, &stat.cnswap, &stat.exit_signal, &stat.processor,
         &stat.rt_priority, &stat.policy, &stat.delayacct_blkio_ticks,
         &stat.guest_time, &stat.cguest_time, &stat.start_data, &stat.end_data,
         &stat.start_brk, &stat.arg_start, &stat.arg_end, &stat.env_start,
         &stat.env_end, &stat.exit_code);

  ulong unused_lib = 0;
  sscanf(statm_buf,
         "%lu %lu %lu %lu %lu %lu",
         &stat.statm_size, &stat.statm_resident, &stat.statm_shared,
         &stat.statm_text, &unused_lib, &stat.statm_data);

  // Read /proc/[pid]/io (may fail due to permissions, that's OK)
  FILE *io_file = fopen(io_filename, "r");
  if (io_file) {
    char io_line[128];
    while (fgets(io_line, sizeof(io_line), io_file)) {
      char key[32];
      ulonglong value;
      if (sscanf(io_line, "%31[^:]: %llu", key, &value) == 2) {
        if (strcmp(key, "read_bytes") == 0) {
          stat.io_read_bytes = value;
        } else if (strcmp(key, "write_bytes") == 0) {
          stat.io_write_bytes = value;
        }
      }
    }
    fclose(io_file);
  }

  return true;
}

Array<ProcessStat> read_all_processes(BumpArena &result_arena) {
  DIR *proc_dir = opendir("/proc");
  if (!proc_dir) {
    printf("Couldn't get a process list");
    return {};
  }

  LinkedList<long> pids = {};
  while (true) {
    dirent *dir = readdir(proc_dir);
    if (!dir) {
      break;
    }

    const char *name = dir->d_name;
    char *str_end = nullptr;
    long parsed_pid = strtol(name, &str_end, 10);
    if (parsed_pid == 0 || parsed_pid == LONG_MAX || parsed_pid == LONG_MIN) {
      continue;
    }
    *(pids.emplace_front(result_arena)) = parsed_pid;
  }

  Array<ProcessStat> result = Array<ProcessStat>::create(result_arena, pids.size);
  LinkedNode<long> *it = pids.head;
  ProcessStat *it_result = result.data;
  while (it) {
    if (read_process(it->value, it_result)) {
      ++it_result;
    }
    it = it->next;
  }
  result.size = it_result - result.data;

  closedir(proc_dir);

  std::sort(result.data, result.data + result.size,
      [](const ProcessStat &left, const ProcessStat &right) {
        return left.pid < right.pid;
      });

  return result;
}

// Reads /proc/stat for system-wide CPU stats
// Returns array where [0] = total, [1..n] = per-core
Array<CpuCoreStat> read_cpu_stats(BumpArena &arena) {
  FILE *stat_file = fopen("/proc/stat", "r");
  if (!stat_file) {
    return {};
  }

  // Count CPU lines first (total + per-core)
  int num_cpus = 0;
  char line[256];
  while (fgets(line, sizeof(line), stat_file)) {
    if (strncmp(line, "cpu", 3) == 0 && (line[3] == ' ' || (line[3] >= '0' && line[3] <= '9'))) {
      ++num_cpus;
    } else if (num_cpus > 0) {
      break;  // CPU lines are at the top, stop when we hit non-CPU lines
    }
  }

  rewind(stat_file);

  Array<CpuCoreStat> result = Array<CpuCoreStat>::create(arena, num_cpus);
  int idx = 0;
  while (fgets(line, sizeof(line), stat_file) && idx < num_cpus) {
    if (strncmp(line, "cpu", 3) != 0) {
      continue;
    }
    // Skip "cpu" or "cpuN" prefix
    char *p = line + 3;
    while (*p && *p != ' ') ++p;

    CpuCoreStat &stat = result.data[idx];
    sscanf(p, "%lu %lu %lu %lu %lu %lu %lu",
           &stat.user, &stat.nice, &stat.system, &stat.idle,
           &stat.iowait, &stat.irq, &stat.softirq);
    ++idx;
  }

  fclose(stat_file);
  return result;
}

// Reads /proc/diskstats for system-wide disk I/O stats
// Aggregates all block devices (skips partitions by looking at device naming)
DiskIoStat read_disk_io_stats() {
  FILE *diskstats_file = fopen("/proc/diskstats", "r");
  if (!diskstats_file) {
    return {};
  }

  DiskIoStat result = {};
  char line[256];
  while (fgets(line, sizeof(line), diskstats_file)) {
    int major, minor;
    char device[64];
    ulonglong reads_completed, reads_merged, sectors_read, ms_reading;
    ulonglong writes_completed, writes_merged, sectors_written, ms_writing;

    int parsed = sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu %llu",
                        &major, &minor, device,
                        &reads_completed, &reads_merged, &sectors_read, &ms_reading,
                        &writes_completed, &writes_merged, &sectors_written, &ms_writing);
    if (parsed < 11) {
      continue;
    }

    // Skip partitions (devices ending with a digit after letters like sda1, nvme0n1p1)
    // Include: sda, sdb, nvme0n1, vda, etc.
    // Skip: sda1, sda2, nvme0n1p1, loop0, ram0, etc.
    size_t len = strlen(device);
    if (len == 0) continue;

    // Skip loop and ram devices
    if (strncmp(device, "loop", 4) == 0 || strncmp(device, "ram", 3) == 0) {
      continue;
    }

    // For nvme devices: include nvme0n1, skip nvme0n1p1
    // For sd/vd devices: include sda, skip sda1
    bool is_partition = false;
    if (strncmp(device, "nvme", 4) == 0) {
      // NVMe partitions have 'p' followed by digit
      const char *p = strrchr(device, 'p');
      if (p && p > device + 4 && p[1] >= '0' && p[1] <= '9') {
        is_partition = true;
      }
    } else {
      // Traditional devices: partitions end with digit
      char last = device[len - 1];
      if (last >= '0' && last <= '9') {
        is_partition = true;
      }
    }

    if (is_partition) {
      continue;
    }

    result.sectors_read += sectors_read;
    result.sectors_written += sectors_written;
  }

  fclose(diskstats_file);
  return result;
}

// Reads /proc/meminfo for system-wide memory stats
// Values are in kB (as reported by /proc/meminfo)
MemInfo read_mem_info() {
  FILE *meminfo_file = fopen("/proc/meminfo", "r");
  if (!meminfo_file) {
    return {};
  }

  MemInfo result = {};
  char line[256];
  while (fgets(line, sizeof(line), meminfo_file)) {
    char key[64];
    ulong value;
    // Format: "FieldName:       value kB"
    if (sscanf(line, "%63[^:]: %lu kB", key, &value) == 2) {
      if (strcmp(key, "MemTotal") == 0) {
        result.mem_total = value;
      } else if (strcmp(key, "MemFree") == 0) {
        result.mem_free = value;
      } else if (strcmp(key, "MemAvailable") == 0) {
        result.mem_available = value;
      } else if (strcmp(key, "Buffers") == 0) {
        result.buffers = value;
      } else if (strcmp(key, "Cached") == 0) {
        result.cached = value;
      } else if (strcmp(key, "SwapTotal") == 0) {
        result.swap_total = value;
      } else if (strcmp(key, "SwapFree") == 0) {
        result.swap_free = value;
      }
    }
  }

  fclose(meminfo_file);
  return result;
}

} // unnamed namespace

void gather(GatheringState &state, Sync &sync) {
  BumpArena arena = BumpArena::create();
  const auto process_stats = read_all_processes(arena);
  const auto cpu_stats = read_cpu_stats(arena);
  const auto mem_info = read_mem_info();
  const auto disk_io_stats = read_disk_io_stats();

  const Seconds period = Seconds{0.5};
  {
    std::unique_lock<std::mutex> lock(sync.quit_mutex);
    sync.quit_cv.wait_until(lock, state.last_update + period);
  }
  if (sync.quit.load()) {
    arena.destroy();
    return;
  }
  state.last_update = SteadyClock::now();
  const SystemTimePoint system_now = SystemClock::now();
  const bool pushed = sync.update_queue.push(UpdateSnapshot{
      arena,
      process_stats,
      cpu_stats,
      mem_info,
      disk_io_stats,
      state.last_update,
      system_now
  });
  if (!pushed) {
    arena.destroy();
  }
}

