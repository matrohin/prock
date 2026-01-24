#pragma once

struct NetChartData {
  int pid;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> recv_kb_per_sec;
  GrowingArray<double> send_kb_per_sec;
  bool y_axis_fitted;
};

struct NetChartState {
  BumpArena cur_arena;
  GrowingArray<NetChartData> charts;
  size_t wasted_bytes;
};

void net_chart_update(NetChartState &my_state, const State &state);
void net_chart_draw(ViewState &view_state);

void net_chart_add(NetChartState &my_state, int pid, const char *comm);
