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

  const uint32_t idx = my_state.track.emplace_back();
  my_state.times[idx] = update_at;
  my_state.recv_mb_per_sec[idx] = rate.recv_mb_per_sec;
  my_state.send_mb_per_sec[idx] = rate.send_mb_per_sec;
}

void system_net_chart_draw(FrameContext & /*ctx*/, ViewState &view_state) {
  ZoneScoped;
  SystemNetChartState &my_state = view_state.system_net_chart_state;

  if (ImGui::Begin("System Network", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y =
        try_initial_y_fit(my_state.y_axis_fitted, my_state.track.size);
    if (ImPlot::BeginPlot("##SystemNet", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted++;
      }
      setup_chart(my_state.times[my_state.track.last_idx()],
                  common_format_io_rate_mb,
                  view_state.preferences_state.auto_follow,
                  view_state.preferences_state.y_auto_fit);

      ImPlotSpec spec = {};
      spec.FillAlpha = FILL_ALPHA_LOW;
      spec.Offset = my_state.track.head;
      ImPlot::PlotShaded(TITLE_RECV, my_state.times, my_state.recv_mb_per_sec,
                         my_state.track.size, 0, spec);
      ImPlot::PlotShaded(TITLE_SEND, my_state.times, my_state.send_mb_per_sec,
                         my_state.track.size, 0, spec);

      spec.FillAlpha = FILL_ALPHA_FULL;
      ImPlot::PlotLine(TITLE_RECV, my_state.times, my_state.recv_mb_per_sec,
                       my_state.track.size, spec);
      ImPlot::PlotLine(TITLE_SEND, my_state.times, my_state.send_mb_per_sec,
                       my_state.track.size, spec);

      chart_add_tooltip(TITLE_RECV, "receive bytes from /proc/net/dev");
      chart_add_tooltip(TITLE_SEND, "transmit bytes from /proc/net/dev");

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
