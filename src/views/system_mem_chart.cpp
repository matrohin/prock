#include "system_mem_chart.h"

#include "common_implot.h"
#include "views/common.h"
#include "views/common_charts.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui.h"
#include "implot.h"
#include "tracy/Tracy.hpp"

void system_mem_chart_update(SystemMemChartState &my_state,
                             InternTable &interner, const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  const MemInfo &mem = snapshot.mem_info;
  if (mem.mem_total == 0) {
    return;
  }

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  const ulong used_kb = mem.mem_total - mem.mem_available;

  const uint32_t new_idx = my_state.track.emplace_back();
  my_state.times[new_idx] = update_at;
  my_state.used[new_idx] = used_kb;
  my_state.available[new_idx] = mem.mem_available;

  // Find top memory process (store in KB to match system values)
  my_state.top_processes[new_idx] =
      find_top_process(snapshot, interner, [](const ProcessDerivedStat &d) {
        return d.mem_resident_bytes / 1024.0;
      });
}

void system_mem_chart_draw(FrameContext & /*ctx*/, ViewState &view_state) {
  ZoneScoped;
  SystemMemChartState &my_state = view_state.system_mem_chart_state;

  if (ImGui::Begin("System Memory Usage", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y =
        try_initial_y_fit(my_state.y_axis_fitted, my_state.track.size);
    if (ImPlot::BeginPlot("##SystemMem", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted++;
      }
      setup_chart(my_state.times[my_state.track.last_idx()],
                  common_format_memory_kb,
                  view_state.preferences_state.auto_follow,
                  view_state.preferences_state.y_auto_fit);

      ImPlotSpec spec = {};
      spec.FillAlpha = FILL_ALPHA_LOW;
      spec.Offset = my_state.track.head;
      ImPlot::PlotShaded(TITLE_USED, my_state.times, my_state.used,
                         my_state.track.size, 0, spec);
      ImPlot::PlotShaded(TITLE_AVAILABLE, my_state.times, my_state.available,
                         my_state.track.size, 0, spec);

      spec.FillAlpha = FILL_ALPHA_FULL;
      ImPlot::PlotLine(TITLE_USED, my_state.times, my_state.used,
                       my_state.track.size, spec);
      ImPlot::PlotLine(TITLE_AVAILABLE, my_state.times, my_state.available,
                       my_state.track.size, spec);

      chart_add_tooltip(TITLE_USED,
                        "MemTotal - MemAvailable from /proc/meminfo");
      chart_add_tooltip(TITLE_AVAILABLE, "MemAvailable from /proc/meminfo");

      show_top_process_tooltip(my_state.track, my_state.times, my_state.used,
                               my_state.top_processes, "Used",
                               common_format_memory_kb);

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
