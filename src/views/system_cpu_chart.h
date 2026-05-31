#pragma once

#include "base/ring_track.h"
#include "views/common_charts.h"

struct SystemCpuChartState {
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> total_usage;
  ChartData<double> kernel_usage;
  ChartData<double> interrupts_usage;
  ChartData<TopProcess> top_processes;

  // Per-core ring buffers, allocated (from view_state.arena) for exactly
  // `num_cores` cores. A static ChartData<double>[N] would instead reserve
  // N * sizeof(ChartData) (32KB each) regardless of the real core count.
  // core_usage[i] is the i-th core's ring buffer (double[4096]).
  ChartData<double> *core_usage; // [num_cores], nullptr until first update
  int num_cores;

  int y_axis_fitted;
  bool stacked;
};

void system_cpu_chart_update(SystemCpuChartState &my_state, BumpArena &arena,
                             InternTable &interner, const State &state);
void system_cpu_chart_draw(FrameContext &ctx, ViewState &view_state);
