#include "mem_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "implot.h"
#include "tracy/Tracy.hpp"

void mem_chart_update(MemChartState &my_state, const State &state) {
  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  common_charts_update(my_state.charts, state,
                       [&](MemChartData &chart, const ProcessStat & /*stat*/,
                           const ProcessDerivedStat &derived) {
                         const uint32_t idx = chart.track.emplace_back();
                         chart.times[idx] = update_at;
                         chart.mem_resident_kb[idx] =
                             derived.mem_resident_bytes / 1024;
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

void mem_chart_draw(ViewState &view_state) {
  ZoneScoped;
  MemChartState &my_state = view_state.mem_chart_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    MemChartData &chart = my_state.charts.data()[last];
    process_window_handle_docking_and_pos(view_state, chart.dock_id,
                                          chart.flags, chart.label);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(chart.flags);
    if (ImGui::Begin(chart.label, &should_be_opened, win_flags)) {
      process_window_check_close(chart.flags, should_be_opened);

      push_fit_with_padding();
      const bool should_fit_y =
          try_initial_y_fit(chart.y_axis_fitted, chart.track.size);
      if (ImPlot::BeginPlot("Memory Usage", ImVec2(-1, -1),
                            ImPlotFlags_Crosshairs)) {
        if (should_fit_y) {
          chart.y_axis_fitted++;
        }

        setup_chart(chart.times[chart.track.last_idx()],
                    common_format_memory_kb,
                    view_state.preferences_state.auto_follow,
                    view_state.preferences_state.y_auto_fit);

        ImPlotSpec spec = {};
        spec.FillAlpha = FILL_ALPHA_LOW;
        spec.Offset = chart.track.head;
        ImPlot::PlotShaded(TITLE_USED, chart.times, chart.mem_resident_kb,
                           chart.track.size, 0, spec);

        spec.FillAlpha = FILL_ALPHA_FULL;
        ImPlot::PlotLine(TITLE_USED, chart.times, chart.mem_resident_kb,
                         chart.track.size, spec);

        chart_add_tooltip(TITLE_USED, "resident from /proc/[pid]/statm");

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

void mem_chart_add(MemChartState &my_state, const Pid pid, const char *comm,
                   const ImGuiID dock_id,
                   const ProcessWindowFlags extra_flags) {
  if (process_window_focus(my_state.charts, pid)) {
    return;
  }

  MemChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  data.dock_id = dock_id;
  data.flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  snprintf(data.label, sizeof(data.label), "Memory - %s (%d)###MemChart%d",
           comm, pid, pid);

  common_views_sort_added(my_state.charts);
}
