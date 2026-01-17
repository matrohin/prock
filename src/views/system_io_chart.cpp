#include "system_io_chart.h"

#include "views/common.h"
#include "views/common_charts.h"

#include "imgui.h"
#include "implot.h"

#include <cmath>

void system_io_chart_update(SystemIoChartState &my_state, const State &state, const StateSnapshot &old) {
  const StateSnapshot &snapshot = state.snapshot;
  const DiskIoRate &rate = snapshot.disk_io_rate;

  const double update_at = std::chrono::duration_cast<Seconds>(state.update_system_time.time_since_epoch()).count();

  *my_state.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = update_at;
  *my_state.read_mb_per_sec.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = rate.read_mb_per_sec;
  *my_state.write_mb_per_sec.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = rate.write_mb_per_sec;

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.read_mb_per_sec.realloc(new_arena);
    my_state.write_mb_per_sec.realloc(new_arena);

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void system_io_chart_draw(FrameContext &ctx, ViewState &view_state, const State &state) {
  SystemIoChartState &my_state = view_state.system_io_chart_state;

  ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.5f));
  if (!my_state.y_axis_fitted && my_state.read_mb_per_sec.size() >= 2) {
    ImPlot::SetNextAxisToFit(ImAxis_Y1);
    my_state.y_axis_fitted = true;
  }

  ImGui::Begin("System I/O", nullptr, COMMON_VIEW_FLAGS);

  if (ImPlot::BeginPlot("##SystemIO", ImVec2(-1, -1), ImPlotFlags_Crosshairs)) {
    ImPlot::SetupAxes("Time", "MB/s", ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::SetupMouseText(ImPlotLocation_NorthEast);

    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
    ImPlot::PlotShaded("Read", my_state.times.data(), my_state.read_mb_per_sec.data(), my_state.read_mb_per_sec.size(), 0, CHART_FLAGS);
    ImPlot::PlotShaded("Write", my_state.times.data(), my_state.write_mb_per_sec.data(), my_state.write_mb_per_sec.size(), 0, CHART_FLAGS);
    ImPlot::PopStyleVar();
    ImPlot::PlotLine("Read", my_state.times.data(), my_state.read_mb_per_sec.data(), my_state.read_mb_per_sec.size());
    ImPlot::PlotLine("Write", my_state.times.data(), my_state.write_mb_per_sec.data(), my_state.write_mb_per_sec.size());

    ImPlot::EndPlot();
  }

  ImGui::End();
  ImPlot::PopStyleVar();
}
