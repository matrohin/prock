#pragma once

#include "base/base.h"
#include "base/ring_track.h"
#include "state.h"

constexpr uint32_t CHART_HISTORY_SIZE = 4096;
using ChartTrack = RingTrack<CHART_HISTORY_SIZE>;
template <class T> using ChartData = T[CHART_HISTORY_SIZE];

struct TopProcess {
  Pid pid;
  ConstString name;
  double value;
};

// Returns nullptr if cmdline is empty or has no useful basename.
inline ConstString cmdline_display_name(const char *cmdline,
                                        InternTable &interner) {
  if (!cmdline || !cmdline[0]) return ConstString{nullptr};
  const char *end = cmdline;
  while (*end && *end != ' ')
    ++end;
  const char *base = cmdline;
  for (const char *p = cmdline; p < end; ++p)
    if (*p == '/') base = p + 1;
  if (base < end) return interner.intern(base, end - base);
  return ConstString{nullptr};
}

// Find top process by a given metric extractor. Name is placed in the interner.
template <class F>
TopProcess find_top_process(const StateSnapshot &snapshot,
                            InternTable &interner, F get_value) {
  TopProcess top = {};
  for (uint32_t i = 0; i < snapshot.stats.size; ++i) {
    const double val = get_value(snapshot.derived_stats.data[i]);
    if (val > top.value) {
      top.pid = snapshot.stats.data[i].pid;
      top.value = val;
      ConstString display =
          cmdline_display_name(snapshot.stats.data[i].cmdline, interner);
      top.name =
          display.data ? display : interner.intern(snapshot.stats.data[i].comm);
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

template <class T, class F>
void common_charts_update(GrowingArray<T> &charts, const State &state, F f) {
  const StateSnapshot &new_snapshot = state.snapshot;
  uint32_t external_idx = 0;
  for (T &chart : charts) {
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
