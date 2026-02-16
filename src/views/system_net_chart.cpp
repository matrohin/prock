#include "system_net_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"
#include "tracy/Tracy.hpp"

void system_net_chart_update(SystemNetChartState &my_state,
                             const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  const NetIoRate &rate = snapshot.net_io_rate;

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  *my_state.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
      update_at;
  *my_state.recv_mb_per_sec.emplace_back(
      my_state.cur_arena, my_state.wasted_bytes) = rate.recv_mb_per_sec;
  *my_state.send_mb_per_sec.emplace_back(
      my_state.cur_arena, my_state.wasted_bytes) = rate.send_mb_per_sec;
  *my_state.total_mb_per_sec.emplace_back(my_state.cur_arena,
                                          my_state.wasted_bytes) =
      rate.recv_mb_per_sec + rate.send_mb_per_sec;

  // Find top network process (store in MB/s to match system values)
  *my_state.top_processes.emplace_back(my_state.cur_arena,
                                       my_state.wasted_bytes) =
      find_top_process(snapshot, [](const ProcessDerivedStat &d) {
        return (d.net_recv_kb_per_sec + d.net_send_kb_per_sec) / 1024.0;
      });

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.recv_mb_per_sec.realloc(new_arena);
    my_state.send_mb_per_sec.realloc(new_arena);
    my_state.total_mb_per_sec.realloc(new_arena);
    my_state.top_processes.realloc(new_arena);

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void system_net_chart_draw(FrameContext & /*ctx*/, ViewState &view_state) {
  ZoneScoped;
  SystemNetChartState &my_state = view_state.system_net_chart_state;

  if (ImGui::Begin("System Network", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y = try_initial_y_fit(
        my_state.y_axis_fitted, my_state.recv_mb_per_sec.size());
    if (ImPlot::BeginPlot("##SystemNet", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted++;
      }
      setup_chart(my_state.times, format_io_rate_mb);

      push_fill_alpha();
      ImPlot::PlotShaded(TITLE_RECV, my_state.times.data(),
                         my_state.recv_mb_per_sec.data(),
                         my_state.recv_mb_per_sec.size());
      ImPlot::PlotShaded(TITLE_SEND, my_state.times.data(),
                         my_state.send_mb_per_sec.data(),
                         my_state.send_mb_per_sec.size());
      pop_fill_alpha();

      ImPlot::PlotLine(TITLE_RECV, my_state.times.data(),
                       my_state.recv_mb_per_sec.data(),
                       my_state.recv_mb_per_sec.size());
      ImPlot::PlotLine(TITLE_SEND, my_state.times.data(),
                       my_state.send_mb_per_sec.data(),
                       my_state.send_mb_per_sec.size());

      chart_add_tooltip(TITLE_RECV, "receive bytes from /proc/net/dev");
      chart_add_tooltip(TITLE_SEND, "transmit bytes from /proc/net/dev");

      show_top_process_tooltip(my_state.times, my_state.top_processes, "Net",
                               my_state.total_mb_per_sec, format_io_rate_mb);

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
