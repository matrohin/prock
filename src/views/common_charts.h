#pragma once

#include "base.h"

#include <algorithm>

static const ImPlotShadedFlags CHART_FLAGS = 0;

template<class T>
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

template<class T>
void common_charts_sort_added(GrowingArray<T> &charts) {
  // FIXME: performance (no need to resort sorted part)
  std::sort(charts.data(), charts.data() + charts.size(),
      [](const auto &left, const auto &right) { return left.pid < right.pid; });
}

template<class T, class F>
void common_charts_update(GrowingArray<T> &charts, const State &state, F f) {
  const StateSnapshot &new_snapshot = state.snapshot;
  size_t external_idx = 0;
  for (size_t i = 0; i < charts.size(); ++i) {
    auto &chart = charts.data()[i];
    while (external_idx < new_snapshot.stats.size && new_snapshot.stats.data[external_idx].pid < chart.pid) {
      ++external_idx;
    }
    if (external_idx >= new_snapshot.stats.size) {
      break;
    }
    const ProcessStat &stat = new_snapshot.stats.data[external_idx];
    const ProcessDerivedStat &derived = new_snapshot.derived_stats.data[external_idx];

    if (chart.pid != stat.pid) {
      continue;
    }

    f(chart, stat, derived);
  }
}

