#pragma once

struct SystemMemChartState {
  BumpArena cur_arena;
  GrowingArray<double> times;
  GrowingArray<double> used; // Used memory in GB (Total - Available)
  size_t wasted_bytes;
  bool y_axis_fitted;
};

void system_mem_chart_update(SystemMemChartState &my_state, const State &state);
void system_mem_chart_draw(FrameContext &ctx, ViewState &view_state);
