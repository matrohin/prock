#pragma once

#include "views/common_charts.h"

struct IoChartData {
  Pid pid;
  ImGuiID dock_id;
  char label[128];
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> read_kb_per_sec;
  ChartData<double> write_kb_per_sec;
  ProcessWindowFlags flags;
  int y_axis_fitted;
};

struct IoChartState {
  BumpArena cur_arena;
  GrowingArray<IoChartData> charts;
  uint32_t wasted_bytes; // tracks window-list GrowingArray waste
};

void io_chart_update(IoChartState &my_state, const State &state);
void io_chart_draw(ViewState &view_state);

void io_chart_add(IoChartState &my_state, Pid pid, const char *comm,
                  ImGuiID dock_id = 0, ProcessWindowFlags extra_flags = 0);
