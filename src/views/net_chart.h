#pragma once

struct NetChartData {
  int pid;
  ImGuiID dock_id;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> recv_kb_per_sec;
  GrowingArray<double> send_kb_per_sec;
  ProcessWindowFlags flags;
  bool y_axis_fitted;
};

struct NetChartState {
  BumpArena cur_arena;
  GrowingArray<NetChartData> charts;
  size_t wasted_bytes;
};

void net_chart_update(NetChartState &my_state, const State &state);
void net_chart_draw(ViewState &view_state);

void net_chart_add(NetChartState &my_state, int pid, const char *comm,
                   ImGuiID dock_id = 0);
void net_chart_close_if_docked_in(NetChartState &my_state, int pid,
                                  ImGuiID dockspace_id);
void net_chart_restore_layout_by_pid(NetChartState &my_state, int pid);
