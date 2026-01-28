#pragma once

struct IoChartData {
  int pid;
  ImGuiID dock_id;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> read_kb_per_sec;
  GrowingArray<double> write_kb_per_sec;
  ProcessWindowFlags flags;
  bool y_axis_fitted;
};

struct IoChartState {
  BumpArena cur_arena;
  GrowingArray<IoChartData> charts;
  size_t wasted_bytes;
};

void io_chart_update(IoChartState &my_state, const State &state);
void io_chart_draw(ViewState &view_state);

void io_chart_add(IoChartState &my_state, int pid, const char *comm,
                  ImGuiID dock_id = 0, ProcessWindowFlags extra_flags = 0);
