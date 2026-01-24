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
                             const State &state) {
  const StateSnapshot &snapshot = state.snapshot;
  const MemInfo &mem = snapshot.mem_info;
  if (mem.mem_total == 0) {
    return;
  }

  const double update_at = std::chrono::duration_cast<Seconds>(
                               state.update_system_time.time_since_epoch())
                               .count();

  const ulong used_kb = mem.mem_total - mem.mem_available;

  *my_state.times.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
      update_at;
  *my_state.used.emplace_back(my_state.cur_arena, my_state.wasted_bytes) =
      used_kb;

  if (my_state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = my_state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    my_state.times.realloc(new_arena);
    my_state.used.realloc(new_arena);

    my_state.cur_arena = new_arena;
    my_state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void system_mem_chart_draw(FrameContext & /*ctx*/, ViewState &view_state) {
  ZoneScoped;
  SystemMemChartState &my_state = view_state.system_mem_chart_state;

  if (ImGui::Begin("System Memory Usage", nullptr, COMMON_VIEW_FLAGS)) {
    push_fit_with_padding();
    const bool should_fit_y =
        !my_state.y_axis_fitted && my_state.used.size() >= 2;
    if (should_fit_y) {
      ImPlot::SetNextAxisToFit(ImAxis_Y1);
    }
    if (ImPlot::BeginPlot("##SystemMem", ImVec2(-1, -1),
                          ImPlotFlags_Crosshairs)) {
      if (should_fit_y) {
        my_state.y_axis_fitted = true;
      }
      setup_chart(my_state.times, format_memory_kb);

      push_fill_alpha();
      ImPlot::PlotShaded(TITLE_USED, my_state.times.data(),
                         my_state.used.data(), my_state.used.size());
      pop_fill_alpha();

      ImPlot::PlotLine(TITLE_USED, my_state.times.data(), my_state.used.data(),
                       my_state.used.size());

      if (ImPlot::IsLegendEntryHovered(TITLE_USED)) {
        ImGui::SetTooltip("Used = MemTotal - MemAvailable");
      }

      ImPlot::EndPlot();
    }

    pop_fit_with_padding();
  }
  ImGui::End();
}
