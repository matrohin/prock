#pragma once

#include "base/ring_track.h"
#include "views/common_charts.h"

struct SystemIoChartState {
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> read_mb_per_sec;  // Read throughput in MB/s
  ChartData<double> write_mb_per_sec; // Write throughput in MB/s
  int y_axis_fitted;
};

void system_io_chart_update(SystemIoChartState &my_state, const State &state);
void system_io_chart_draw(FrameContext &ctx, ViewState &view_state);
