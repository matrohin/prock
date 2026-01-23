#include "system_mem_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"

#include <cmath>

void system_mem_chart_update(SystemMemChartState &my_state,
                             const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  const MemInfo &mem = snapshot.mem_info;
  if (mem.mem_total == 0) {
    return;
  }

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  ulong used_kb = mem.mem_total - mem.mem_available;

  *my_state.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
      update_at;
  *my_state.used.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
      used_kb;

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.used.realloc(new_arena);

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void system_mem_chart_draw(FrameContext & /*ctx*/, ViewState &view_state) {
  SystemMemChartState &my_state = view_state.system_mem_chart_state;

  ImGui::Begin("System Memory Usage", nullptr, COMMON_VIEW_FLAGS);
  if (ImGui::IsWindowFocused()) {
    view_state.focused_view = eFocusedView_SystemMemChart;
  }

  push_fit_with_padding();
  if (!my_state.y_axis_fitted && my_state.used.size() >= 2) {
    ImPlot::SetNextAxisToFit(ImAxis_Y1);
    my_state.y_axis_fitted = true;
  }
  if (ImPlot::BeginPlot("##SystemMem", ImVec2(-1, -1),
                        ImPlotFlags_Crosshairs)) {
    ImPlot::SetupAxes("Time", nullptr, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisFormat(ImAxis_Y1, format_memory_kb);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);
    ImPlot::SetupMouseText(ImPlotLocation_NorthEast);

    setup_time_scale(my_state.times);

    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
    ImPlot::PlotShaded("Used", my_state.times.data(), my_state.used.data(),
                       my_state.used.size(), 0, CHART_FLAGS);
    ImPlot::PopStyleVar();
    ImPlot::PlotLine("Used", my_state.times.data(), my_state.used.data(),
                     my_state.used.size());

    if (ImPlot::IsLegendEntryHovered("Used")) {
      ImGui::SetTooltip("Used = MemTotal - MemAvailable");
    }

    ImPlot::EndPlot();
  }

  pop_fit_with_padding();
  ImGui::End();
}
