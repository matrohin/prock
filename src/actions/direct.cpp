#include "direct.h"

#include "sync.h"
#include "views/common.h"

#include <cerrno>
#include <cstring>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>

static bool direct_blocked(Sync &sync, Notifications &notifications) {
  if (!sync.replay_mode) return false;
  notify_error(notifications, 0, "Not available while replaying a recording");
  return true;
}

bool direct_kill(Sync &sync, Notifications &notifications, const Pid pid,
                 const int sig, const char *action) {
  if (direct_blocked(sync, notifications)) return false;
  if (kill(pid, sig) != 0) {
    const int err = errno;
    notify_error(notifications, err, "Failed to %s %d: %s", action, pid,
                 strerror(err));
    return false;
  }
  return true;
}

void direct_kill_many(Sync &sync, Notifications &notifications, const Pid *pids,
                      const int count, const int sig, const char *action) {
  if (direct_blocked(sync, notifications)) return;
  for (int i = 0; i < count; ++i) {
    if (kill(pids[i], sig) != 0) {
      const int err = errno;
      notify_error(notifications, err, "Failed to %s %d: %s", action, pids[i],
                   strerror(err));
    }
  }
}

bool direct_set_nice(Sync &sync, Notifications &notifications, const Pid pid,
                     const int nice_val) {
  if (direct_blocked(sync, notifications)) return false;
  if (setpriority(PRIO_PROCESS, pid, nice_val) != 0) {
    const int err = errno;
    notify_error(notifications, err, "Failed to set priority for PID %d: %s",
                 pid, strerror(err));
    return false;
  }
  return true;
}

bool direct_get_affinity(const Pid pid, uint64_t &mask, const int num_cpus) {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  if (sched_getaffinity(pid, sizeof(cpu_set), &cpu_set) != 0) return false;
  mask = 0;
  for (int i = 0; i < num_cpus && i < 64; ++i) {
    if (CPU_ISSET(i, &cpu_set)) mask |= 1ULL << i;
  }
  return true;
}

bool direct_set_affinity(Sync &sync, Notifications &notifications,
                         const Pid pid, const uint64_t mask) {
  if (direct_blocked(sync, notifications)) return false;
  if (mask == 0) {
    notify_error(notifications, 0, "At least one CPU must be selected");
    return false;
  }
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  for (int i = 0; i < 64; ++i) {
    if (mask & (1ULL << i)) CPU_SET(i, &cpu_set);
  }
  if (sched_setaffinity(pid, sizeof(cpu_set), &cpu_set) != 0) {
    const int err = errno;
    notify_error(notifications, err, "Failed to set affinity for PID %d: %s",
                 pid, strerror(err));
    return false;
  }
  return true;
}
