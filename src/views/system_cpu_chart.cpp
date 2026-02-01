#include "system_cpu_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include "tracy/Tracy.hpp"

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

  // Find top CPU process
  TopCpuProcess top = {0, {'\0'}, 0.0};
  for (size_t i = 0; i < snapshot.stats.size; ++i) {
    const ProcessDerivedStat &derived = snapshot.derived_stats.data[i];
    double cpu = derived.cpu_user_perc + derived.cpu_kernel_perc;
    if (cpu > top.cpu_perc) {
      top.pid = snapshot.stats.data[i].pid;
      top.cpu_perc = cpu;
      strncpy(top.comm, snapshot.stats.data[i].comm, sizeof(top.comm) - 1);
      top.comm[sizeof(top.comm) - 1] = '\0';
    }
  }
  *my_state.top_processes.emplace_back(my_state.cur_arena,
                                       my_state.wasted_bytes) = top;

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
    my_state.top_processes.realloc(new_arena);

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void system_cpu_chart_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  SystemCpuChartState &my_state = view_state.system_cpu_chart_state;
  if (ImGui::Begin("System CPU Usage", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y =
        try_initial_y_fit(my_state.y_axis_fitted, my_state.total_usage.size());
    if (ImPlot::BeginPlot("##SystemCPU", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted = true;
      }

      setup_chart(my_state.times, format_percent);

      if (!my_state.show_per_core) {
        push_fill_alpha();
        ImPlot::PlotShaded(TITLE_TOTAL, my_state.times.data(),
                           my_state.total_usage.data(),
                           my_state.total_usage.size());
        ImPlot::PlotShaded(TITLE_KERNEL, my_state.times.data(),
                           my_state.kernel_usage.data(),
                           my_state.kernel_usage.size());
        ImPlot::PlotShaded(TITLE_INTERRUPTS, my_state.times.data(),
                           my_state.interrupts_usage.data(),
                           my_state.interrupts_usage.size());
        pop_fill_alpha();

        ImPlot::PlotLine(TITLE_INTERRUPTS, my_state.times.data(),
                         my_state.interrupts_usage.data(),
                         my_state.interrupts_usage.size());
        ImPlot::PlotLine(TITLE_KERNEL, my_state.times.data(),
                         my_state.kernel_usage.data(),
                         my_state.kernel_usage.size());
        ImPlot::PlotLine(TITLE_TOTAL, my_state.times.data(),
                         my_state.total_usage.data(),
                         my_state.total_usage.size());

        chart_add_tooltip(TITLE_TOTAL, "user + system + irq + softirq from /proc/stat");
        chart_add_tooltip(TITLE_KERNEL, "system from /proc/stat");
        chart_add_tooltip(TITLE_INTERRUPTS, "irq + softirq from /proc/stat");
      } else if (my_state.stacked) {
        // Stacked per-core view
        const size_t n = my_state.core_usage[0].size();
        if (n > 0 && my_state.num_cores > 0) {
          Array<double> prev = Array<double>::create(ctx.frame_arena, n);
          Array<double> curr = Array<double>::create(ctx.frame_arena, n);
          memset(prev.data, 0, n * sizeof(double));

          push_fill_alpha(0.7f);

          // Call SetupLock manually to get correct GetItem id
          // for the first line if it was hidden by the user:
          ImPlot::SetupLock();
          for (int i = 0; i < my_state.num_cores; ++i) {
            char label[16];
            snprintf(label, sizeof(label), "Core %d", i);

            const ImPlotItem *item =
                ImPlot::GetCurrentPlot()->Items.GetItem(label);
            const bool is_hidden = item && !item->Show;

            if (is_hidden) {
              std::swap(prev.data, curr.data);
            } else {
              const double *core_data = my_state.core_usage[i].data();
              for (size_t j = 0; j < n; ++j) {
                curr.data[j] = prev.data[j] + core_data[j];
              }
            }

            ImPlot::PlotShaded(label, my_state.times.data(), prev.data,
                               curr.data, n);

            std::swap(prev.data, curr.data);
          }
          pop_fill_alpha();
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

      // Show tooltip with top process on hover
      if (ImPlot::IsPlotHovered() && my_state.times.size() > 0) {
        ImPlotPoint mouse = ImPlot::GetPlotMousePos();
        size_t idx = lower_bound(
            my_state.times.size(),
            [&my_state](size_t i) { return my_state.times.data()[i]; }, mouse.x);
        if (idx < my_state.times.size()) {
          const TopCpuProcess &top = my_state.top_processes.data()[idx];
          if (top.pid > 0) {
            double cpu_perc = top.cpu_perc;
            if (!my_state.show_per_core && my_state.num_cores > 0) {
              cpu_perc /= my_state.num_cores;
            }
            ImGui::BeginTooltip();
            ImGui::Text("Total: %.1f%%", my_state.total_usage.data()[idx]);
            ImGui::Text("Top: %s (PID %d) %.1f%%", top.comm, top.pid, cpu_perc);
            ImGui::EndTooltip();
          }
        }
      }

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
