#include "net_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "implot.h"
#include "tracy/Tracy.hpp"

void net_chart_update(NetChartState &my_state, const State &state) {
  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  common_charts_update(
      my_state.charts, state,
      [&](NetChartData &chart, const ProcessStat & /*stat*/,
          const ProcessDerivedStat &derived) {
        *chart.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
            update_at;
        *chart.recv_kb_per_sec.emplace_back(my_state.cur_arena,
                                            my_state.wasted_bytes) =
            derived.net_recv_kb_per_sec;
        *chart.send_kb_per_sec.emplace_back(my_state.cur_arena,
                                            my_state.wasted_bytes) =
            derived.net_send_kb_per_sec;
      });

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.charts.realloc(new_arena);
    for (NetChartData &chart : my_state.charts) {
      chart.times.realloc(new_arena);
      chart.recv_kb_per_sec.realloc(new_arena);
      chart.send_kb_per_sec.realloc(new_arena);
    }

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void net_chart_draw(ViewState &view_state) {
  ZoneScoped;
  NetChartState &my_state = view_state.net_chart_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    NetChartData &chart = my_state.charts.data()[last];
    process_window_handle_docking_and_pos(view_state, chart.dock_id,
                                          chart.flags, chart.label);
    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(chart.flags);
    if (ImGui::Begin(chart.label, &should_be_opened, win_flags)) {
      process_window_check_close(chart.flags, should_be_opened);

      push_fit_with_padding();
      const bool should_fit_y =
          try_initial_y_fit(chart.y_axis_fitted, chart.recv_kb_per_sec.size());
      if (ImPlot::BeginPlot("Network Usage", ImVec2(-1, -1),
                            ImPlotFlags_Crosshairs)) {
        if (should_fit_y) {
          chart.y_axis_fitted++;
        }

        setup_chart(chart.times, format_io_rate_kb);

        const ImPlotSpec fill = fill_alpha_spec();
        ImPlot::PlotShaded(TITLE_RECV, chart.times.data(),
                           chart.recv_kb_per_sec.data(),
                           chart.recv_kb_per_sec.size(), 0, fill);
        ImPlot::PlotShaded(TITLE_SEND, chart.times.data(),
                           chart.send_kb_per_sec.data(),
                           chart.send_kb_per_sec.size(), 0, fill);

        ImPlot::PlotLine(TITLE_RECV, chart.times.data(),
                         chart.recv_kb_per_sec.data(),
                         chart.recv_kb_per_sec.size());
        ImPlot::PlotLine(TITLE_SEND, chart.times.data(),
                         chart.send_kb_per_sec.data(),
                         chart.send_kb_per_sec.size());

        chart_add_tooltip(TITLE_RECV, "Socket stats via netlink (INET_DIAG)");
        chart_add_tooltip(TITLE_SEND, "Socket stats via netlink (INET_DIAG)");

        ImPlot::EndPlot();
      }

      pop_fit_with_padding();
    }
    process_window_handle_focus(chart.flags);
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += chart.times.total_byte_size();
      my_state.wasted_bytes += chart.recv_kb_per_sec.total_byte_size();
      my_state.wasted_bytes += chart.send_kb_per_sec.total_byte_size();
    }
  }
  my_state.charts.shrink_to(last);
}

void net_chart_add(NetChartState &my_state, const Pid pid, const char *comm,
                   const ImGuiID dock_id,
                   const ProcessWindowFlags extra_flags) {
  if (process_window_focus(my_state.charts, pid)) {
    return;
  }

  NetChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  data.dock_id = dock_id;
  data.flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  snprintf(data.label, sizeof(data.label), "Network Usage: %s (%d)", comm, pid);

  common_views_sort_added(my_state.charts);
}
