#pragma once

struct NetChartData {
  Pid pid;
  ImGuiID dock_id;
  char label[128];
  GrowingArray<double> times;
  GrowingArray<double> recv_kb_per_sec;
  GrowingArray<double> send_kb_per_sec;
  ProcessWindowFlags flags;
  int y_axis_fitted;
};

struct NetChartState {
  BumpArena cur_arena;
  GrowingArray<NetChartData> charts;
  uint32_t wasted_bytes;
};

void net_chart_update(NetChartState &my_state, const State &state);
void net_chart_draw(ViewState &view_state);

void net_chart_add(NetChartState &my_state, Pid pid, const char *comm,
                   ImGuiID dock_id = 0, ProcessWindowFlags extra_flags = 0);
void net_chart_close_if_docked_in(NetChartState &my_state, Pid pid,
                                  ImGuiID dockspace_id);
void net_chart_restore_layout_by_pid(NetChartState &my_state, Pid pid);
