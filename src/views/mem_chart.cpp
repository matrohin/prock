#include "mem_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "implot.h"

#include <cmath>

void mem_chart_update(MemChartState &my_state, const State &state) {
  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  common_charts_update(my_state.charts, state,
                       [&](MemChartData &chart, const ProcessStat & /*stat*/,
                           const ProcessDerivedStat &derived) {
                         *chart.times.emplace_back(my_state.cur_arena,
                                                   my_state.wasted_bytes) =
                             update_at;
                         *chart.mem_resident_kb.emplace_back(
                             my_state.cur_arena, my_state.wasted_bytes) =
                             derived.mem_resident_bytes / 1024;
                       });

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.charts.realloc(new_arena);
    for (size_t i = 0; i < my_state.charts.size(); ++i) {
      MemChartData &chart = my_state.charts.data()[i];
      chart.times.realloc(new_arena);
      chart.mem_resident_kb.realloc(new_arena);
    }

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void mem_chart_draw(ViewState &view_state) {
  MemChartState &my_state = view_state.mem_chart_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    MemChartData &chart = my_state.charts.data()[last];
    bool should_be_opened = true;
    view_state.cascade.next_if_new(chart.label);

    ImGui::Begin(chart.label, &should_be_opened, COMMON_VIEW_FLAGS);
    if (ImGui::IsWindowFocused()) {
      view_state.focused_view = eFocusedView_MemChart;
    }

    push_fit_with_padding();
    if (!chart.y_axis_fitted && chart.mem_resident_kb.size() >= 2) {
      ImPlot::SetNextAxisToFit(ImAxis_Y1);
      chart.y_axis_fitted = true;
    }
    if (ImPlot::BeginPlot("Memory Usage", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      ImPlot::SetupAxes("Time", nullptr, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxisFormat(ImAxis_Y1, format_memory_kb);
      ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);
      ImPlot::SetupMouseText(ImPlotLocation_NorthEast);

      setup_time_scale(chart.times);

      ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
      ImPlot::PlotShaded("Memory Usage", chart.times.data(),
                         chart.mem_resident_kb.data(),
                         chart.mem_resident_kb.size(), 0, CHART_FLAGS);
      ImPlot::PopStyleVar();

      ImPlot::PlotLine("Memory Usage", chart.times.data(),
                       chart.mem_resident_kb.data(),
                       chart.mem_resident_kb.size());

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
    ImGui::End();

    // TODO: consider continuing supporting it with a member "opened"
    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += chart.times.total_byte_size();
      my_state.wasted_bytes += chart.mem_resident_kb.total_byte_size();
    }
  }
  my_state.charts.shrink_to(last);
}

void mem_chart_add(MemChartState &my_state, const int pid, const char *comm) {
  if (common_charts_contains_pid(my_state.charts, pid)) {
    return;
  }

  MemChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  snprintf(data.label, sizeof(data.label), "Memory Usage: %s (%d)", comm, pid);

  common_charts_sort_added(my_state.charts);
}
