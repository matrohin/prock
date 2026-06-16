#include "system_io_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"
#include "tracy/Tracy.hpp"

void system_io_chart_update(SystemIoChartState &my_state, const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  const DiskIoRate &rate = snapshot.disk_io_rate;

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  const uint32_t idx = my_state.track.emplace_back();
  my_state.times[idx] = update_at;
  my_state.read_mb_per_sec[idx] = rate.read_mb_per_sec;
  my_state.write_mb_per_sec[idx] = rate.write_mb_per_sec;
}

void system_io_chart_draw(FrameContext & /*ctx*/, ViewState &view_state) {
  ZoneScoped;
  SystemIoChartState &my_state = view_state.system_io_chart_state;

  if (ImGui::Begin("System I/O", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y =
        try_initial_y_fit(my_state.y_axis_fitted, my_state.track.size);
    if (ImPlot::BeginPlot("##SystemIO", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted++;
      }
      setup_chart(my_state.times[my_state.track.last_idx()], format_io_rate_mb,
                  view_state.preferences_state.auto_follow,
                  view_state.preferences_state.y_auto_fit);

      ImPlotSpec spec = {};
      spec.FillAlpha = FILL_ALPHA_LOW;
      spec.Offset = my_state.track.head;
      ImPlot::PlotShaded(TITLE_READ, my_state.times, my_state.read_mb_per_sec,
                         my_state.track.size, 0, spec);
      ImPlot::PlotShaded(TITLE_WRITE, my_state.times, my_state.write_mb_per_sec,
                         my_state.track.size, 0, spec);

      spec.FillAlpha = FILL_ALPHA_FULL;
      ImPlot::PlotLine(TITLE_READ, my_state.times, my_state.read_mb_per_sec,
                       my_state.track.size, spec);
      ImPlot::PlotLine(TITLE_WRITE, my_state.times, my_state.write_mb_per_sec,
                       my_state.track.size, spec);

      chart_add_tooltip(TITLE_READ, "sectors read from /proc/diskstats");
      chart_add_tooltip(TITLE_WRITE, "sectors written from /proc/diskstats");

      if (ImPlot::IsPlotHovered() && my_state.track.size > 0) {
        ImPlotPoint mouse = ImPlot::GetPlotMousePos();
        const uint32_t i = lower_bound(
            my_state.track.size,
            [&](uint32_t k) {
              return my_state.times[my_state.track.to_data_idx(k)];
            },
            mouse.x);
        const uint32_t data_idx = my_state.track.to_data_idx(i);
        char read_buf[32];
        char write_buf[32];
        format_io_rate_mb(my_state.read_mb_per_sec[data_idx], read_buf,
                          sizeof(read_buf), nullptr);
        format_io_rate_mb(my_state.write_mb_per_sec[data_idx], write_buf,
                          sizeof(write_buf), nullptr);
        ImGui::BeginTooltip();
        ImGui::Text("Read: %s", read_buf);
        ImGui::Text("Write: %s", write_buf);
        ImGui::EndTooltip();
      }

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
