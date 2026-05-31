#pragma once

#include "base/ring_track.h"
#include "views/common_charts.h"

struct SystemMemChartState {
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> used;
  ChartData<double> available;
  ChartData<TopProcess> top_processes;
  int y_axis_fitted;
};

void system_mem_chart_update(SystemMemChartState &my_state, BumpArena &persistent_arena, const State &state);
void system_mem_chart_draw(FrameContext &ctx, ViewState &view_state);
