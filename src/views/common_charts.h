#pragma once

#include "base.h"
#include "state.h"

#include <algorithm>
#include <cstring>

struct TopProcess {
  int pid;
  char comm[16];  // Linux limits to 15 chars + null
  double value;
};

// Find top process by a given metric extractor
template <class F>
TopProcess find_top_process(const StateSnapshot &snapshot, F get_value) {
  TopProcess top = {0, {'\0'}, 0.0};
  for (size_t i = 0; i < snapshot.stats.size; ++i) {
    const double val = get_value(snapshot.derived_stats.data[i]);
    if (val > top.value) {
      top.pid = snapshot.stats.data[i].pid;
      top.value = val;
      strncpy(top.comm, snapshot.stats.data[i].comm, sizeof(top.comm) - 1);
      top.comm[sizeof(top.comm) - 1] = '\0';
    }
  }
  return top;
}

// CPU chart titles:
constexpr const char *TITLE_TOTAL = "Total";
constexpr const char *TITLE_KERNEL = "Kernel";
constexpr const char *TITLE_INTERRUPTS = "Interrupts";

// IO chart titles
constexpr const char *TITLE_READ = "Read";
constexpr const char *TITLE_WRITE = "Write";

// Memory chart titles
constexpr const char *TITLE_USED = "Used";
constexpr const char *TITLE_AVAILABLE = "Available";

// Net chart titles
constexpr const char *TITLE_RECV = "Recv";
constexpr const char *TITLE_SEND = "Send";

template <class T>
bool common_charts_contains_pid(const GrowingArray<T> &charts, int pid) {
  const auto data = charts.data();
  size_t left = 0;
  size_t right = charts.size();
  while (right - left > 1) {
    const size_t middle = (left + right) / 2;
    const auto &chart = data[middle];
    if (chart.pid > pid) {
      right = middle;
    } else {
      left = middle;
    }
  }
  return left < right && data[left].pid == pid;
}

template <class T, class F>
void common_charts_update(GrowingArray<T> &charts, const State &state, F f) {
  const StateSnapshot &new_snapshot = state.snapshot;
  size_t external_idx = 0;
  for (size_t i = 0; i < charts.size(); ++i) {
    auto &chart = charts.data()[i];
    while (external_idx < new_snapshot.stats.size &&
           new_snapshot.stats.data[external_idx].pid < chart.pid) {
      ++external_idx;
    }
    if (external_idx >= new_snapshot.stats.size) {
      break;
    }
    const ProcessStat &stat = new_snapshot.stats.data[external_idx];
    const ProcessDerivedStat &derived =
        new_snapshot.derived_stats.data[external_idx];

    if (chart.pid != stat.pid) {
      continue;
    }

    f(chart, stat, derived);
  }
}
