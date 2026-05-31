#pragma once

#include "base/ring_track.h"
#include "views/common_charts.h"

struct SystemNetChartState {
  ChartTrack track;
  ChartData<double> times;
  ChartData<double> recv_mb_per_sec;
  ChartData<double> send_mb_per_sec;
  int y_axis_fitted;
};

void system_net_chart_update(SystemNetChartState &my_state, const State &state);
void system_net_chart_draw(FrameContext &ctx, ViewState &view_state);
