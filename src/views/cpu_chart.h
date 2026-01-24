#pragma once

#include "process_window_flags.h"

struct CpuChartData {
  int pid;
  ImGuiID dock_id;
  ProcessWindowFlags flags;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> cpu_kernel_perc;
  GrowingArray<double> cpu_total_perc;
};

struct CpuChartState {
  BumpArena cur_arena;
  GrowingArray<CpuChartData> charts;
  size_t wasted_bytes;
};

void cpu_chart_update(CpuChartState &my_state, const State &state);
void cpu_chart_draw(ViewState &view_state);

void cpu_chart_add(CpuChartState &my_state, int pid, const char *comm,
                   ImGuiID dock_id = 0);
