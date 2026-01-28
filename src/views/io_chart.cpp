#include "io_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "implot.h"
#include "tracy/Tracy.hpp"

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
  ZoneScoped;
  IoChartState &my_state = view_state.io_chart_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    IoChartData &chart = my_state.charts.data()[last];
    process_window_handle_docking_and_pos(view_state, chart.dock_id,
                                          chart.flags, chart.label);

    bool should_be_opened = true;
    ImGuiWindowFlags win_flags = COMMON_VIEW_FLAGS;
    if (chart.flags & eProcessWindowFlags_NoFocusOnAppearing) {
      win_flags |= ImGuiWindowFlags_NoFocusOnAppearing;
      chart.flags &= ~eProcessWindowFlags_NoFocusOnAppearing;
    }
    if (ImGui::Begin(chart.label, &should_be_opened, win_flags)) {
      process_window_check_close(chart.flags, should_be_opened);

      push_fit_with_padding();
      const bool should_fit_y =
          !chart.y_axis_fitted && chart.read_kb_per_sec.size() >= 2;
      if (should_fit_y) {
        ImPlot::SetNextAxisToFit(ImAxis_Y1);
      }
      if (ImPlot::BeginPlot("I/O Usage", ImVec2(-1, -1),
                            ImPlotFlags_Crosshairs)) {
        if (should_fit_y) {
          chart.y_axis_fitted = true;
        }

        setup_chart(chart.times, format_io_rate_kb);

        push_fill_alpha();
        ImPlot::PlotShaded(TITLE_READ, chart.times.data(),
                           chart.read_kb_per_sec.data(),
                           chart.read_kb_per_sec.size());
        ImPlot::PlotShaded(TITLE_WRITE, chart.times.data(),
                           chart.write_kb_per_sec.data(),
                           chart.write_kb_per_sec.size());
        pop_fill_alpha();

        ImPlot::PlotLine(TITLE_READ, chart.times.data(),
                         chart.read_kb_per_sec.data(),
                         chart.read_kb_per_sec.size());
        ImPlot::PlotLine(TITLE_WRITE, chart.times.data(),
                         chart.write_kb_per_sec.data(),
                         chart.write_kb_per_sec.size());

        ImPlot::EndPlot();
      }

      pop_fit_with_padding();
    }
    ImGui::End();

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

void io_chart_add(IoChartState &my_state, const int pid, const char *comm,
                  const ImGuiID dock_id, const ProcessWindowFlags extra_flags) {
  if (common_charts_contains_pid(my_state.charts, pid)) {
    return;
  }

  IoChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  data.dock_id = dock_id;
  data.flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  snprintf(data.label, sizeof(data.label), "I/O Usage: %s (%d)", comm, pid);

  common_views_sort_added(my_state.charts);
}
