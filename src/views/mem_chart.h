#pragma once

#include "views/common_charts.h"

struct MemChartData {
  Pid pid;
  ImGuiID dock_id;
  char label[128];
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> mem_resident_kb;
  ProcessWindowFlags flags;
  int y_axis_fitted;
};

struct MemChartState {
  BumpArena cur_arena;
  GrowingArray<MemChartData> charts;
  uint32_t wasted_bytes; // tracks window-list GrowingArray waste
};

void mem_chart_update(MemChartState &my_state, const State &state);
void mem_chart_draw(ViewState &view_state);

void mem_chart_add(MemChartState &my_state, Pid pid, const char *comm,
                   ImGuiID dock_id = 0, ProcessWindowFlags extra_flags = 0);
void mem_chart_close_if_docked_in(MemChartState &my_state, Pid pid,
                                  ImGuiID dockspace_id);
void mem_chart_restore_layout_by_pid(MemChartState &my_state, Pid pid);
