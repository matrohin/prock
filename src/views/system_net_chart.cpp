#include "system_net_chart.h"

#include "common_implot.h"
#include "views/common.h"
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
  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.recv_mb_per_sec.realloc(new_arena);
    my_state.send_mb_per_sec.realloc(new_arena);

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
      setup_chart(my_state.times, format_io_rate_mb,
                  view_state.preferences_state.auto_follow);

      const ImPlotSpec fill = fill_alpha_spec();
      ImPlot::PlotShaded(TITLE_RECV, my_state.times.data(),
                         my_state.recv_mb_per_sec.data(),
                         my_state.recv_mb_per_sec.size(), 0, fill);
      ImPlot::PlotShaded(TITLE_SEND, my_state.times.data(),
                         my_state.send_mb_per_sec.data(),
                         my_state.send_mb_per_sec.size(), 0, fill);

      ImPlot::PlotLine(TITLE_RECV, my_state.times.data(),
                       my_state.recv_mb_per_sec.data(),
                       my_state.recv_mb_per_sec.size());
      ImPlot::PlotLine(TITLE_SEND, my_state.times.data(),
                       my_state.send_mb_per_sec.data(),
                       my_state.send_mb_per_sec.size());

      chart_add_tooltip(TITLE_RECV, "receive bytes from /proc/net/dev");
      chart_add_tooltip(TITLE_SEND, "transmit bytes from /proc/net/dev");

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
