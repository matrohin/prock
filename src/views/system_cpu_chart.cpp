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

void system_cpu_chart_update(SystemCpuChartState &my_state, BumpArena &arena,
                             InternTable &interner, const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  if (snapshot.cpu_perc.total.size == 0) {
    return;
  }

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  // Per-core data (skip index 0 which is aggregate). total.size >= 1 here
  // (checked above), so num_cores >= 0.
  const int num_cores = static_cast<int>(snapshot.cpu_perc.total.size) - 1;

  // (Re)allocate the per-core ring buffers only when the core count changes
  // (first sample, or CPU hot(un)plug), sizing for the actual core count.
  // Backed by the app-lifetime view_state.arena; arena memory is zero-filled,
  // so a buffer grown for a newly-appeared core reads 0 for past samples. On a
  // core-count change the previous buffer is left in the arena (rare event).
  if (num_cores != my_state.num_cores) {
    my_state.core_usage =
        num_cores > 0 ? arena.alloc_array_of<ChartData<double>>(num_cores)
                      : nullptr;
    my_state.num_cores = num_cores;
  }

  const uint32_t idx = my_state.track.emplace_back();
  my_state.times[idx] = update_at;
  my_state.total_usage[idx] = snapshot.cpu_perc.total.data[0];
  my_state.kernel_usage[idx] = snapshot.cpu_perc.kernel.data[0];
  my_state.interrupts_usage[idx] = snapshot.cpu_perc.interrupts.data[0];

  for (int i = 0; i < num_cores; ++i) {
    my_state.core_usage[i][idx] = snapshot.cpu_perc.total.data[i + 1];
  }

  // Find top CPU process
  my_state.top_processes[idx] =
      find_top_process(snapshot, interner, [](const ProcessDerivedStat &d) {
        return d.cpu_user_perc + d.cpu_kernel_perc;
      });
}

void system_cpu_chart_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  SystemCpuChartState &my_state = view_state.system_cpu_chart_state;
  if (ImGui::Begin("System CPU Usage", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y =
        try_initial_y_fit(my_state.y_axis_fitted, my_state.track.size);
    if (ImPlot::BeginPlot("##SystemCPU", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted++;
      }

      setup_chart(my_state.times[my_state.track.last_idx()], format_percent,
                  view_state.preferences_state.auto_follow);

      const bool per_core = view_state.preferences_state.cpu_per_core;
      if (!per_core) {
        ImPlotSpec spec = {};
        spec.FillAlpha = FILL_ALPHA_LOW;
        spec.Offset = my_state.track.head;
        ImPlot::PlotShaded(TITLE_TOTAL, my_state.times, my_state.total_usage,
                           my_state.track.size, 0, spec);
        ImPlot::PlotShaded(TITLE_KERNEL, my_state.times, my_state.kernel_usage,
                           my_state.track.size, 0, spec);
        ImPlot::PlotShaded(TITLE_INTERRUPTS, my_state.times,
                           my_state.interrupts_usage, my_state.track.size, 0,
                           spec);

        spec.FillAlpha = FILL_ALPHA_FULL;
        ImPlot::PlotLine(TITLE_INTERRUPTS, my_state.times,
                         my_state.interrupts_usage, my_state.track.size, spec);
        ImPlot::PlotLine(TITLE_KERNEL, my_state.times, my_state.kernel_usage,
                         my_state.track.size, spec);
        ImPlot::PlotLine(TITLE_TOTAL, my_state.times, my_state.total_usage,
                         my_state.track.size, spec);

        chart_add_tooltip(TITLE_TOTAL,
                          "user + system + irq + softirq from /proc/stat");
        chart_add_tooltip(TITLE_KERNEL, "system from /proc/stat");
        chart_add_tooltip(TITLE_INTERRUPTS, "irq + softirq from /proc/stat");
      } else if (my_state.stacked) {
        // Stacked per-core view. prev/curr are indexed by physical ring slot
        // (same as the Offset trick), so they pair with `times` under the same
        // spec.Offset = track.head.
        const uint32_t n = my_state.track.size;
        if (n > 0 && my_state.num_cores > 0) {
          Array<double> prev = Array<double>::create(ctx.frame_arena, n);
          Array<double> curr = Array<double>::create(ctx.frame_arena, n);
          memset(prev.data, 0, n * sizeof(double));

          ImPlotSpec fill = {};
          fill.FillAlpha = FILL_ALPHA_HIGH;
          fill.Offset = my_state.track.head;
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
              const double *core_data = my_state.core_usage[i];
              for (uint32_t j = 0; j < n; ++j) {
                curr.data[j] = prev.data[j] + core_data[j];
              }
            }

            ImPlot::PlotShaded(label, my_state.times, prev.data, curr.data, n,
                               fill);

            std::swap(prev.data, curr.data);
          }
        }
      } else {
        // Separate lines per-core view
        ImPlotSpec spec = {};
        spec.Offset = my_state.track.head;
        for (int i = 0; i < my_state.num_cores; ++i) {
          char label[16];
          snprintf(label, sizeof(label), "Core %d", i);
          ImPlot::PlotLine(label, my_state.times, my_state.core_usage[i],
                           my_state.track.size, spec);
        }
      }

      // Show tooltip with top process on hover
      // Per-core view: system * num_cores (to match stacked scale), process
      // as-is Total view: system as-is, process / num_cores (to normalize to
      // 0-100%)
      const int num_cores = my_state.num_cores;
      show_top_process_tooltip(
          my_state.track, my_state.times, my_state.total_usage,
          my_state.top_processes, "Total",
          [num_cores, per_core](const double val, char *buf, const int size,
                                void *user_data) {
            const bool is_system = user_data && *static_cast<bool *>(user_data);
            double cpu_perc = val;
            if (num_cores > 0) {
              if (is_system && per_core) {
                cpu_perc = val * num_cores;
              } else if (!is_system && !per_core) {
                cpu_perc = val / num_cores;
              }
            }
            snprintf(buf, size, "%.1f%%", cpu_perc);
          });

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
