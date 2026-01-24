#pragma once

struct MemChartData {
  int pid;
  ImGuiID dock_id;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> mem_resident_kb;
  ProcessWindowFlags flags;
  bool y_axis_fitted;
};

struct MemChartState {
  BumpArena cur_arena;
  GrowingArray<MemChartData> charts;
  size_t wasted_bytes;
};

void mem_chart_update(MemChartState &my_state, const State &state);
void mem_chart_draw(ViewState &view_state);

void mem_chart_add(MemChartState &my_state, int pid, const char *comm,
                   ImGuiID dock_id = 0);
void mem_chart_close_if_docked_in(MemChartState &my_state, int pid,
                                  ImGuiID dockspace_id);
void mem_chart_restore_layout_by_pid(MemChartState &my_state, int pid);
