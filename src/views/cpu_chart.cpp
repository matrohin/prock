#include "cpu_chart.h"

#include "views/common.h"
#include "views/common_charts.h"
#include "views/common_implot.h"
#include "views/process_window_flags.h"
#include "views/view_state.h"

#include "state.h"

#include "implot.h"
#include "tracy/Tracy.hpp"

#include <cmath>

struct CpuChartScaledData {
  const double *times;
  const double *values;
  double scale;
  uint32_t head; // ring offset, see ChartTrack
};

static ImPlotPoint cpu_chart_scaled_getter(const int idx, void *user_data) {
  const auto *data = static_cast<CpuChartScaledData *>(user_data);
  const uint32_t i = (data->head + static_cast<uint32_t>(idx)) & ChartTrack::MASK;
  return ImPlotPoint(data->times[i], data->values[i] * data->scale);
}

static ImPlotPoint cpu_chart_baseline_getter(const int idx, void *user_data) {
  const auto *data = static_cast<CpuChartScaledData *>(user_data);
  const uint32_t i = (data->head + static_cast<uint32_t>(idx)) & ChartTrack::MASK;
  return ImPlotPoint(data->times[i], 0.0);
}

void cpu_chart_update(CpuChartState &my_state, const State &state) {
  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  common_charts_update(
      my_state.charts, state,
      [&](CpuChartData &chart, const ProcessStat & /*stat*/,
          const ProcessDerivedStat &derived) {
        const uint32_t idx = chart.track.emplace_back();
        chart.times[idx] = update_at;
        chart.cpu_kernel_perc[idx] = derived.cpu_kernel_perc;
        chart.cpu_total_perc[idx] =
            derived.cpu_kernel_perc + derived.cpu_user_perc;
      });

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.charts.realloc(new_arena);

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void cpu_chart_draw(ViewState &view_state) {
  ZoneScoped;
  CpuChartState &my_state = view_state.cpu_chart_state;
  const int num_cores = view_state.system_cpu_chart_state.num_cores;
  const bool per_core = view_state.preferences_state.cpu_per_core;
  const double scale = (per_core || num_cores <= 0) ? 1.0 : 1.0 / num_cores;

  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    CpuChartData &chart = my_state.charts.data()[last];

    process_window_handle_docking_and_pos(view_state, chart.dock_id,
                                          chart.flags, chart.label);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(chart.flags);
    if (ImGui::Begin(chart.label, &should_be_opened, win_flags)) {
      process_window_check_close(chart.flags, should_be_opened);

      push_fit_with_padding();
      const bool should_fit_y =
          try_initial_y_fit(chart.y_axis_fitted, chart.track.size);
      if (ImPlot::BeginPlot("CPU Usage", ImVec2(-1, -1),
                            ImPlotFlags_Crosshairs)) {
        if (should_fit_y) {
          chart.y_axis_fitted++;
        }

        setup_chart(chart.times[chart.track.last_idx()], format_percent,
                    view_state.preferences_state.auto_follow);

        CpuChartScaledData total_data = {chart.times, chart.cpu_total_perc,
                                         scale, chart.track.head};
        CpuChartScaledData kernel_data = {chart.times, chart.cpu_kernel_perc,
                                          scale, chart.track.head};

        const ImPlotSpec fill = fill_alpha_spec();
        ImPlot::PlotShadedG(TITLE_TOTAL, cpu_chart_scaled_getter, &total_data,
                            cpu_chart_baseline_getter, &total_data,
                            chart.track.size, fill);
        ImPlot::PlotShadedG(TITLE_KERNEL, cpu_chart_scaled_getter, &kernel_data,
                            cpu_chart_baseline_getter, &kernel_data,
                            chart.track.size, fill);

        ImPlot::PlotLineG(TITLE_KERNEL, cpu_chart_scaled_getter, &kernel_data,
                          chart.track.size);
        ImPlot::PlotLineG(TITLE_TOTAL, cpu_chart_scaled_getter, &total_data,
                          chart.track.size);

        chart_add_tooltip(TITLE_TOTAL, "utime + stime from /proc/[pid]/stat");
        chart_add_tooltip(TITLE_KERNEL, "stime from /proc/[pid]/stat");

        ImPlot::EndPlot();
      }

      pop_fit_with_padding();
    }
    process_window_handle_focus(chart.flags);
    ImGui::End();

    if (should_be_opened) {
      ++last;
    }
  }
  my_state.charts.shrink_to(last);
}

void cpu_chart_add(CpuChartState &my_state, const int pid, const char *comm,
                   const ImGuiID dock_id) {
  if (process_window_focus(my_state.charts, pid)) {
    return;
  }

  CpuChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  data.dock_id = dock_id;
  data.flags |= eProcessWindowFlags_RedockRequested;
  snprintf(data.label, sizeof(data.label), "CPU Usage: %s (%d)", comm, pid);

  common_views_sort_added(my_state.charts);
}
