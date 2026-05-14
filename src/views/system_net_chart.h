#pragma once

struct SystemNetChartState {
  BumpArena cur_arena;
  GrowingArray<double> times;
  GrowingArray<double> recv_mb_per_sec;
  GrowingArray<double> send_mb_per_sec;
  uint32_t wasted_bytes;
  int y_axis_fitted;
};

void system_net_chart_update(SystemNetChartState &my_state, const State &state);
void system_net_chart_draw(FrameContext &ctx, ViewState &view_state);
