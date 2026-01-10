#pragma once

constexpr int MAX_CORES = 128;

struct CoreChartState {
  BumpArena cur_arena;
  GrowingArray<double> times;
  GrowingArray<double> total_usage;
  GrowingArray<double> core_usage[MAX_CORES];
  int num_cores = 0;
  size_t wasted_bytes = 0;

  bool show_per_core = false;
  bool stacked = false;
};

void core_chart_update(CoreChartState &my_state, const State &state, const StateSnapshot &old);
void core_chart_draw(FrameContext &ctx, CoreChartState &my_state, const State &state);
