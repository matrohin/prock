#include "core_chart.h"

#include "views/common.h"
#include "views/common_charts.h"

#include "imgui.h"
#include "implot.h"

void core_chart_update(CoreChartState &my_state, const State &state, const StateSnapshot &old) {
  const StateSnapshot &snapshot = state.snapshot;
  if (snapshot.cpu_usage_perc.size == 0) {
    return;
  }

  const double update_at = std::chrono::duration_cast<Seconds>(state.update_system_time.time_since_epoch()).count();

  *my_state.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = update_at;
  *my_state.total_usage.emplace_back(my_state.cur_arena, my_state.wasted_bytes) = snapshot.cpu_usage_perc.data[0];

  // Per-core data (skip index 0 which is total)
  int num_cores = (int)snapshot.cpu_usage_perc.size - 1;
  if (num_cores > MAX_CORES) num_cores = MAX_CORES;
  my_state.num_cores = num_cores;

  for (int i = 0; i < num_cores; ++i) {
    *my_state.core_usage[i].emplace_back(my_state.cur_arena, my_state.wasted_bytes) = snapshot.cpu_usage_perc.data[i + 1];
  }

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.total_usage.realloc(new_arena);
    for (int i = 0; i < my_state.num_cores; ++i) {
      my_state.core_usage[i].realloc(new_arena);
    }

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void core_chart_draw(FrameContext &ctx, CoreChartState &my_state, const State &state) {
  ImGui::Begin("System CPU Usage", nullptr, COMMON_VIEW_FLAGS);

  // UI controls
  ImGui::Checkbox("Per-core", &my_state.show_per_core);
  if (my_state.show_per_core) {
    ImGui::SameLine();
    ImGui::Checkbox("Stacked", &my_state.stacked);
  }

  if (ImPlot::BeginPlot("##SystemCPU", ImVec2(-1, -1))) {
    ImPlot::SetupAxes("Time", "%", ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 100, ImPlotCond_Once);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

    if (!my_state.show_per_core) {
      // Total usage only
      ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
      ImPlot::PlotShaded("Total", my_state.times.data(), my_state.total_usage.data(), my_state.total_usage.size(), 0, CHART_FLAGS);
      ImPlot::PopStyleVar();
      ImPlot::PlotLine("Total", my_state.times.data(), my_state.total_usage.data(), my_state.total_usage.size());
    } else if (my_state.stacked) {
      // Stacked per-core view
      int n = (int)my_state.core_usage[0].size();
      if (n > 0 && my_state.num_cores > 0) {
        // Allocate two buffers for prev/curr cumulative values
        Array<double> prev = Array<double>::create(ctx.frame_arena, n);
        Array<double> curr = Array<double>::create(ctx.frame_arena, n);
        memset(prev.data, 0, n * sizeof(double));

        ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.7f);
        for (int i = 0; i < my_state.num_cores; ++i) {
          // curr = prev + core_usage[i]
          const double *core_data = my_state.core_usage[i].data();
          for (int j = 0; j < n; ++j) {
            curr.data[j] = prev.data[j] + core_data[j];
          }

          char label[16];
          snprintf(label, sizeof(label), "Core %d", i);
          ImPlot::PlotShaded(label, my_state.times.data(), prev.data, curr.data, n, CHART_FLAGS);

          // Swap buffers
          double *tmp = prev.data;
          prev.data = curr.data;
          curr.data = tmp;
        }
        ImPlot::PopStyleVar();
      }
    } else {
      // Separate lines per-core view
      for (int i = 0; i < my_state.num_cores; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "Core %d", i);
        ImPlot::PlotLine(label, my_state.times.data(), my_state.core_usage[i].data(), my_state.core_usage[i].size());
      }
    }

    ImPlot::EndPlot();
  }

  ImGui::End();
}
