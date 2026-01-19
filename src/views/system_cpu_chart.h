#pragma once

constexpr int MAX_CORES = 128;

struct SystemCpuChartState {
  BumpArena cur_arena;
  GrowingArray<double> times;
  GrowingArray<double> total_usage;
  GrowingArray<double> kernel_usage;
  GrowingArray<double> interrupts_usage;
  GrowingArray<double> core_usage[MAX_CORES];
  size_t wasted_bytes;
  int num_cores;

  bool show_per_core;
  bool stacked;
  bool auto_fit;
};

void system_cpu_chart_update(SystemCpuChartState &my_state, const State &state);
void system_cpu_chart_draw(FrameContext &ctx, ViewState &view_state);
