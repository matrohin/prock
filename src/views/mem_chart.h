#pragma once

struct MemChartData {
  int pid;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> mem_resident_kb;
  bool y_axis_fitted;
};

struct MemChartState {
  BumpArena cur_arena; // TODO: gets changed every now and then via Array<T>::realloc
  GrowingArray<MemChartData> charts;
  size_t wasted_bytes;
};

void mem_chart_update(MemChartState &my_state, const State &state, const StateSnapshot &old);
void mem_chart_draw(ViewState &view_state, const State &state);

void mem_chart_add(MemChartState &my_state, int pid, const char *comm);
