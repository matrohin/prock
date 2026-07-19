#pragma once

#include "base/base.h" // Pid

#include <cstdint>

struct Sync;
struct Notifications;

// Synchronous, UI-thread process-mutating actions ("direct" actions)

bool direct_kill(Sync &sync, Notifications &notifications, Pid pid, int sig,
                 const char *action);

void direct_kill_many(Sync &sync, Notifications &notifications, const Pid *pids,
                      int count, int sig, const char *action);

bool direct_set_nice(Sync &sync, Notifications &notifications, Pid pid,
                     int nice_val);

bool direct_get_affinity(Pid pid, uint64_t &mask, int num_cpus);
bool direct_set_affinity(Sync &sync, Notifications &notifications, Pid pid,
                         uint64_t mask);
