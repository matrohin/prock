#include "system_cpu_chart.h"

#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"

#include <cmath>

void system_cpu_chart_update(SystemCpuChartState &my_state,
                             const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  if (snapshot.cpu_perc.total.size == 0) {
    return;
  }

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  *my_state.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
      update_at;
  *my_state.total_usage.emplace_back(my_state.cur_arena,
                                     my_state.wasted_bytes) =
      snapshot.cpu_perc.total.data[0];
  *my_state.kernel_usage.emplace_back(my_state.cur_arena,
                                      my_state.wasted_bytes) =
      snapshot.cpu_perc.kernel.data[0];
  *my_state.interrupts_usage.emplace_back(my_state.cur_arena,
                                          my_state.wasted_bytes) =
      snapshot.cpu_perc.interrupts.data[0];

  // Per-core data (skip index 0 which is aggregate)
  int num_cores = static_cast<int>(snapshot.cpu_perc.total.size) - 1;
  if (num_cores > MAX_CORES) num_cores = MAX_CORES;
  my_state.num_cores = num_cores;

  for (int i = 0; i < num_cores; ++i) {
    *my_state.core_usage[i].emplace_back(my_state.cur_arena,
                                         my_state.wasted_bytes) =
        snapshot.cpu_perc.total.data[i + 1];
  }

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.total_usage.realloc(new_arena);
    my_state.kernel_usage.realloc(new_arena);
    my_state.interrupts_usage.realloc(new_arena);
    for (int i = 0; i < my_state.num_cores; ++i) {
      my_state.core_usage[i].realloc(new_arena);
    }

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void system_cpu_chart_draw(FrameContext &ctx, ViewState &view_state) {
  SystemCpuChartState &my_state = view_state.system_cpu_chart_state;
  ImGui::Begin("System CPU Usage", nullptr, COMMON_VIEW_FLAGS);
  if (ImGui::IsWindowFocused()) {
    view_state.focused_view = eFocusedView_SystemCpuChart;
  }

  ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.5f));
  if (my_state.auto_fit) {
    ImPlot::SetNextAxesToFit();
    my_state.auto_fit = false;
  }
  if (ImPlot::BeginPlot("##SystemCPU", ImVec2(-1, -1),
                        ImPlotFlags_Crosshairs)) {
    ImPlot::SetupAxes("Time", "%", ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 100, ImPlotCond_Once);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
    ImPlot::SetupMouseText(ImPlotLocation_NorthEast);

    if (!my_state.show_per_core) {
      ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
      ImPlot::PlotShaded("Total", my_state.times.data(),
                         my_state.total_usage.data(),
                         my_state.total_usage.size(), 0, CHART_FLAGS);
      ImPlot::PlotShaded("Kernel", my_state.times.data(),
                         my_state.kernel_usage.data(),
                         my_state.kernel_usage.size(), 0, CHART_FLAGS);
      ImPlot::PlotShaded("Interrupts", my_state.times.data(),
                         my_state.interrupts_usage.data(),
                         my_state.interrupts_usage.size(), 0, CHART_FLAGS);
      ImPlot::PopStyleVar();
      ImPlot::PlotLine("Interrupts", my_state.times.data(),
                       my_state.interrupts_usage.data(),
                       my_state.interrupts_usage.size());
      ImPlot::PlotLine("Kernel", my_state.times.data(),
                       my_state.kernel_usage.data(),
                       my_state.kernel_usage.size());
      ImPlot::PlotLine("Total", my_state.times.data(),
                       my_state.total_usage.data(),
                       my_state.total_usage.size());
    } else if (my_state.stacked) {
      // Stacked per-core view
      size_t n = my_state.core_usage[0].size();
      if (n > 0 && my_state.num_cores > 0) {
        Array<double> prev = Array<double>::create(ctx.frame_arena, n);
        Array<double> curr = Array<double>::create(ctx.frame_arena, n);
        memset(prev.data, 0, n * sizeof(double));

        ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.7f);

        // Call SetupLock manually to get correct GetItem id
        // for the first line if it was hidden by the user:
        ImPlot::SetupLock();
        for (int i = 0; i < my_state.num_cores; ++i) {
          char label[16];
          snprintf(label, sizeof(label), "Core %d", i);

          ImPlotItem *item = ImPlot::GetCurrentPlot()->Items.GetItem(label);
          const bool is_hidden = item && !item->Show;

          if (is_hidden) {
            std::swap(prev.data, curr.data);
          } else {
            const double *core_data = my_state.core_usage[i].data();
            for (size_t j = 0; j < n; ++j) {
              curr.data[j] = prev.data[j] + core_data[j];
            }
          }

          ImPlot::PlotShaded(label, my_state.times.data(), prev.data, curr.data,
                             n, CHART_FLAGS);

          std::swap(prev.data, curr.data);
        }
        ImPlot::PopStyleVar();
      }
    } else {
      // Separate lines per-core view
      for (int i = 0; i < my_state.num_cores; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "Core %d", i);
        ImPlot::PlotLine(label, my_state.times.data(),
                         my_state.core_usage[i].data(),
                         my_state.core_usage[i].size());
      }
    }

    ImPlot::EndPlot();
  }

  ImPlot::PopStyleVar();
  ImGui::End();
}
