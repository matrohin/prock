#include "io_chart.h"

#include "views/common.h"
#include "views/common_charts.h"

#include "implot.h"

#include <cmath>

void io_chart_update(IoChartState &my_state, const State &state) {
  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  common_charts_update(
      my_state.charts, state,
      [&](IoChartData &chart, const ProcessStat & /*stat*/,
          const ProcessDerivedStat &derived) {
        *chart.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
            update_at;
        *chart.read_kb_per_sec.emplace_back(my_state.cur_arena,
                                            my_state.wasted_bytes) =
            derived.io_read_kb_per_sec;
        *chart.write_kb_per_sec.emplace_back(my_state.cur_arena,
                                             my_state.wasted_bytes) =
            derived.io_write_kb_per_sec;
      });

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.charts.realloc(new_arena);
    for (size_t i = 0; i < my_state.charts.size(); ++i) {
      IoChartData &chart = my_state.charts.data()[i];
      chart.times.realloc(new_arena);
      chart.read_kb_per_sec.realloc(new_arena);
      chart.write_kb_per_sec.realloc(new_arena);
    }

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void io_chart_draw(ViewState &view_state) {
  IoChartState &my_state = view_state.io_chart_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    IoChartData &chart = my_state.charts.data()[last];
    bool should_be_opened = true;
    view_state.cascade.next_if_new(chart.label);

    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.5f));
    if (!chart.y_axis_fitted && chart.read_kb_per_sec.size() >= 2) {
      ImPlot::SetNextAxisToFit(ImAxis_Y1);
      chart.y_axis_fitted = true;
    }

    ImGui::Begin(chart.label, &should_be_opened, COMMON_VIEW_FLAGS);
    if (ImPlot::BeginPlot("I/O Usage", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      ImPlot::SetupAxes("Time", "KB/s", ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);
      ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
      ImPlot::SetupMouseText(ImPlotLocation_NorthEast);

      ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
      ImPlot::PlotShaded("Read", chart.times.data(),
                         chart.read_kb_per_sec.data(),
                         chart.read_kb_per_sec.size(), 0, CHART_FLAGS);
      ImPlot::PlotShaded("Write", chart.times.data(),
                         chart.write_kb_per_sec.data(),
                         chart.write_kb_per_sec.size(), 0, CHART_FLAGS);
      ImPlot::PopStyleVar();

      ImPlot::PlotLine("Read", chart.times.data(), chart.read_kb_per_sec.data(),
                       chart.read_kb_per_sec.size());
      ImPlot::PlotLine("Write", chart.times.data(),
                       chart.write_kb_per_sec.data(),
                       chart.write_kb_per_sec.size());

      ImPlot::EndPlot();
    }
    ImGui::End();

    ImPlot::PopStyleVar();

    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += chart.times.total_byte_size();
      my_state.wasted_bytes += chart.read_kb_per_sec.total_byte_size();
      my_state.wasted_bytes += chart.write_kb_per_sec.total_byte_size();
    }
  }
  my_state.charts.shrink_to(last);
}

void io_chart_add(IoChartState &my_state, int pid, const char *comm) {
  if (common_charts_contains_pid(my_state.charts, pid)) {
    return;
  }

  IoChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  snprintf(data.label, sizeof(data.label), "I/O Usage: %s (%d)", comm, pid);

  common_charts_sort_added(my_state.charts);
}
