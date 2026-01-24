#include "net_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "implot.h"

#include <cmath>

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
    for (size_t i = 0; i < my_state.charts.size(); ++i) {
      NetChartData &chart = my_state.charts.data()[i];
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
  NetChartState &my_state = view_state.net_chart_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.charts.size(); ++i) {
    if (last != i) {
      my_state.charts.data()[last] = my_state.charts.data()[i];
    }
    NetChartData &chart = my_state.charts.data()[last];
    bool should_be_opened = true;
    view_state.cascade.next_if_new(chart.label);

    ImGui::Begin(chart.label, &should_be_opened, COMMON_VIEW_FLAGS);
    if (ImGui::IsWindowFocused()) {
      view_state.focused_view = eFocusedView_NetChart;
    }

    push_fit_with_padding();
    const bool should_fit_y =
        !chart.y_axis_fitted && chart.recv_kb_per_sec.size() >= 2;
    if (should_fit_y) {
      ImPlot::SetNextAxisToFit(ImAxis_Y1);
    }
    if (ImPlot::BeginPlot("Network Usage", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        chart.y_axis_fitted = true;
      }

      setup_chart(chart.times, format_io_rate_kb);

      ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
      ImPlot::PlotShaded("Recv", chart.times.data(),
                         chart.recv_kb_per_sec.data(),
                         chart.recv_kb_per_sec.size(), 0, CHART_FLAGS);
      ImPlot::PlotShaded("Send", chart.times.data(),
                         chart.send_kb_per_sec.data(),
                         chart.send_kb_per_sec.size(), 0, CHART_FLAGS);
      ImPlot::PopStyleVar();

      ImPlot::PlotLine("Recv", chart.times.data(), chart.recv_kb_per_sec.data(),
                       chart.recv_kb_per_sec.size());
      ImPlot::PlotLine("Send", chart.times.data(), chart.send_kb_per_sec.data(),
                       chart.send_kb_per_sec.size());

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
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

void net_chart_add(NetChartState &my_state, const int pid, const char *comm) {
  if (common_charts_contains_pid(my_state.charts, pid)) {
    return;
  }

  NetChartData &data =
      *my_state.charts.emplace_back(my_state.cur_arena, my_state.wasted_bytes);
  data.pid = pid;
  snprintf(data.label, sizeof(data.label), "Network Usage: %s (%d)", comm, pid);

  common_charts_sort_added(my_state.charts);
}
