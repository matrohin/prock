#pragma once

struct SystemIoChartState {
  BumpArena cur_arena;
  GrowingArray<double> times;
  GrowingArray<double> read_mb_per_sec;   // Read throughput in MB/s
  GrowingArray<double> write_mb_per_sec;  // Write throughput in MB/s
  size_t wasted_bytes;
  bool y_axis_fitted;
};

void system_io_chart_update(SystemIoChartState &my_state, const State &state, const StateSnapshot &old);
void system_io_chart_draw(FrameContext &ctx, ViewState &view_state, const State &state);
