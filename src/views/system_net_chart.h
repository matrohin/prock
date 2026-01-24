#pragma once

struct SystemNetChartState {
  BumpArena cur_arena;
  GrowingArray<double> times;
  GrowingArray<double> recv_mb_per_sec;  // Receive throughput in MB/s
  GrowingArray<double> send_mb_per_sec;  // Send throughput in MB/s
  size_t wasted_bytes;
  bool y_axis_fitted;
};

void system_net_chart_update(SystemNetChartState &my_state, const State &state);
void system_net_chart_draw(FrameContext &ctx, ViewState &view_state);
