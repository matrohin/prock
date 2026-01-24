#include "system_net_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"

#include <cmath>

void system_net_chart_update(SystemNetChartState &my_state, const State &state) {
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
  SystemNetChartState &my_state = view_state.system_net_chart_state;

  ImGui::Begin("System Network", nullptr, COMMON_VIEW_FLAGS);
  if (ImGui::IsWindowFocused()) {
    view_state.focused_view = eFocusedView_SystemNetChart;
  }

  push_fit_with_padding();
  const bool should_fit_y = !my_state.y_axis_fitted && my_state.recv_mb_per_sec.size() >= 2;
  if (should_fit_y) {
    ImPlot::SetNextAxisToFit(ImAxis_Y1);
  }
  if (ImPlot::BeginPlot("##SystemNet", ImVec2(-1, -1), ImPlotFlags_Crosshairs)) {
    if (should_fit_y) {
      my_state.y_axis_fitted = true;
    }
    setup_chart(my_state.times, format_io_rate_mb);

    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
    ImPlot::PlotShaded("Recv", my_state.times.data(),
                       my_state.recv_mb_per_sec.data(),
                       my_state.recv_mb_per_sec.size(), 0, CHART_FLAGS);
    ImPlot::PlotShaded("Send", my_state.times.data(),
                       my_state.send_mb_per_sec.data(),
                       my_state.send_mb_per_sec.size(), 0, CHART_FLAGS);
    ImPlot::PopStyleVar();
    ImPlot::PlotLine("Recv", my_state.times.data(),
                     my_state.recv_mb_per_sec.data(),
                     my_state.recv_mb_per_sec.size());
    ImPlot::PlotLine("Send", my_state.times.data(),
                     my_state.send_mb_per_sec.data(),
                     my_state.send_mb_per_sec.size());

    ImPlot::EndPlot();
  }

  pop_fit_with_padding();
  ImGui::End();
}
