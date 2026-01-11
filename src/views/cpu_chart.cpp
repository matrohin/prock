#include "cpu_chart.h"

#include "views/common.h"
#include "views/common_charts.h"

#include "implot.h"

#include <cmath>

void cpu_chart_update(
    CpuChartState &my_state, const State &state, const StateSnapshot &old) {
  const double update_at = std::chrono::duration_cast<Seconds>(state.update_system_time.time_since_epoch()).count();

  common_charts_update(my_state.charts, state,
    [&](CpuChartData &chart, const ProcessStat &stat, const ProcessDerivedStat &derived) {
      // TODO: add reallocs
      *chart.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = update_at;
      *chart.cpu_kernel_perc.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = derived.cpu_kernel_perc;
      *chart.cpu_total_perc.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = derived.cpu_kernel_perc + derived.cpu_user_perc;
    });

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.charts.realloc(new_arena);
    for (size_t i = 0; i < my_state.charts.size(); ++i) {
      CpuChartData &chart = my_state.charts.data()[i];
      chart.times.realloc(new_arena);
      chart.cpu_total_perc.realloc(new_arena);
      chart.cpu_kernel_perc.realloc(new_arena);
    }

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void cpu_chart_draw(ViewState &view_state, const State &state) {
  CpuChartState &my_state = view_state.cpu_chart_state;

  size_t last = 0;

  for (size_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    const CpuChartData &chart = my_state.charts.data()[last];
    bool should_be_opened = true;
    view_state.cascade.next_if_new(chart.label);
    ImGui::Begin(chart.label, &should_be_opened, COMMON_VIEW_FLAGS);
    if (ImPlot::BeginPlot("CPU Usage", ImVec2(-1, -1), ImPlotFlags_Crosshairs)) {
      ImPlot::SetupAxes("Time","%", ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 100, ImPlotCond_Once);
      ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);
      ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
      ImPlot::SetupMouseText(ImPlotLocation_NorthEast);

      ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
      ImPlot::PlotShaded("CPU Total Usage", chart.times.data(), chart.cpu_total_perc.data(), chart.cpu_total_perc.size(), 0, CHART_FLAGS);
      ImPlot::PlotShaded("CPU Kernel Usage", chart.times.data(), chart.cpu_kernel_perc.data(), chart.cpu_kernel_perc.size(), 0, CHART_FLAGS);
      ImPlot::PopStyleVar();

      ImPlot::PlotLine("CPU Kernel Usage", chart.times.data(), chart.cpu_kernel_perc.data(), chart.cpu_kernel_perc.size());
      ImPlot::PlotLine("CPU Total Usage", chart.times.data(), chart.cpu_total_perc.data(), chart.cpu_total_perc.size());

      ImPlot::EndPlot();
    }
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += chart.times.total_byte_size();
      my_state.wasted_bytes += chart.cpu_total_perc.total_byte_size();
      my_state.wasted_bytes += chart.cpu_kernel_perc.total_byte_size();
    }
  }
  my_state.charts.shrink_to(last);
}

void cpu_chart_add(CpuChartState &my_state, int pid, const char *comm) {
  if (common_charts_contains_pid(my_state.charts, pid)) {
    return;
  }

  CpuChartData &data = *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  snprintf(data.label, sizeof(data.label), "CPU Usage: %s (%d)", comm, pid);

  common_charts_sort_added(my_state.charts);
}

