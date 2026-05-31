#pragma once

#include "process_window_flags.h"
#include "views/common_charts.h"

struct CpuChartData {
  Pid pid;
  ImGuiID dock_id;
  ProcessWindowFlags flags;
  char label[128];
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> cpu_kernel_perc;
  ChartData<double> cpu_total_perc;
  int y_axis_fitted;
};

struct CpuChartState {
  BumpArena cur_arena;
  GrowingArray<CpuChartData> charts;
  uint32_t wasted_bytes; // tracks window-list GrowingArray waste
};

void cpu_chart_update(CpuChartState &my_state, const State &state);
void cpu_chart_draw(ViewState &view_state);

void cpu_chart_add(CpuChartState &my_state, Pid pid, const char *comm,
                   ImGuiID dock_id = 0);
