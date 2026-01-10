#include "process_stat.h"

#include "sync.h"

#include <algorithm>

namespace {

void read_process(int pid, ProcessStat *out) {
  constexpr size_t BUF_SIZE = 64;

  char stat_filename[BUF_SIZE];
  snprintf(stat_filename, BUF_SIZE, "/proc/%d/stat", pid);

  char statm_filename[BUF_SIZE];
  snprintf(statm_filename, BUF_SIZE, "/proc/%d/statm", pid);

  char comm_filename[BUF_SIZE];
  snprintf(comm_filename, BUF_SIZE, "/proc/%d/comm", pid);

  ProcessStat &stat = *out;

  FILE *stat_file = fopen(stat_filename, "r");
  FILE *statm_file = fopen(statm_filename, "r");
  FILE *comm_file = fopen(comm_filename, "r");
  if (!stat_file || !statm_file || !comm_file) {
    stat.pid = pid;
    return;
  }

  // FIXME? The limit of comm is 15 characters but should be we more safe about it?
  fscanf(stat_file,
         "%d %s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld "
         "%ld %ld %llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
         "%lu %d %d %u %u %llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %d",
         &stat.pid, stat.comm, &stat.state, &stat.ppid, &stat.pgrp,
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
  fscanf(statm_file,
         "%lu %lu %lu %lu %lu %lu",
         &stat.statm_size, &stat.statm_resident, &stat.statm_shared,
         &stat.statm_text, &unused_lib, &stat.statm_data);

  fscanf(comm_file, "%s", stat.comm);

  fclose(comm_file);
  fclose(statm_file);
  fclose(stat_file);
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
    read_process(it->value, it_result);
    ++it_result;
    it = it->next;
  }

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

} // unnamed namespace

void gather(GatheringState &state, Sync &sync) {
  BumpArena arena = BumpArena::create();
  const auto process_stats = read_all_processes(arena);
  const auto cpu_stats = read_cpu_stats(arena);

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
  sync.update_queue.push(UpdateSnapshot{
      arena,
      process_stats,
      cpu_stats,
      state.last_update,
      system_now
  });
}

