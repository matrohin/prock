#pragma once

#include "base/base.h"
#include "imgui.h"
#include "implot.h"
#include "views/common_charts.h"

// Show tooltip with top process on chart hover
// format_value :: (double, char *, int, void *)
// TODO: refactor the format_value away and just make it simpler
template <class F>
void show_top_process_tooltip(const ChartTrack &track,
                              const ChartData<double> &times,
                              const ChartData<double> &system_values,
                              const ChartData<TopProcess> &top_processes,
                              const char *system_label, F format_value) {
  if (!ImPlot::IsPlotHovered() || track.size == 0) {
    return;
  }
  ImPlotPoint mouse = ImPlot::GetPlotMousePos();
  const uint32_t idx = lower_bound(
      track.size,
      [&track, times](const uint32_t i) { return times[track.to_data_idx(i)]; },
      mouse.x);
  const uint32_t data_idx = track.to_data_idx(idx);

  bool is_system = true;
  bool is_process = false;
  char system_buf[32];
  format_value(system_values[data_idx], system_buf, sizeof(system_buf),
               &is_system);
  ImGui::BeginTooltip();
  ImGui::Text("%s: %s", system_label, system_buf);
  const TopProcess &top = top_processes[data_idx];
  if (top.pid > 0) {
    char top_buf[32];
    format_value(top.value, top_buf, sizeof(top_buf), &is_process);
    ImGui::Text("Top: %s (PID %d) %s", top.name.data ? top.name.data : "?",
                top.pid, top_buf);
  }
  ImGui::EndTooltip();
}

inline void chart_add_tooltip(const char *title, const char *tooltip) {
  if (ImPlot::IsLegendEntryHovered(title)) {
    ImGui::SetTooltip("%s", tooltip);
  }
}

inline void push_fit_with_padding() {
  ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.5f));
}

inline void pop_fit_with_padding() { ImPlot::PopStyleVar(); }

constexpr float FILL_ALPHA_LOW = 0.25f;
constexpr float FILL_ALPHA_HIGH = 0.5f;
constexpr float FILL_ALPHA_FULL = 1.00f;

constexpr ImPlotAxisFlags COMMON_X_FLAGS_FOLLOW =
    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
constexpr ImPlotAxisFlags COMMON_X_FLAGS_STATIC = ImPlotAxisFlags_RangeFit;
constexpr ImPlotAxisFlags COMMON_Y_FLAGS_AUTO =
    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
constexpr ImPlotAxisFlags COMMON_Y_FLAGS_STATIC = ImPlotAxisFlags_RangeFit;

// Handles delayed Y axis fit: first frame lets X establish its range,
// second frame fits Y using RangeFit against the established X range.
// Returns true if caller should increment y_axis_fitted after BeginPlot.
inline bool try_initial_y_fit(const int y_axis_fitted,
                              const uint32_t data_size) {
  if (y_axis_fitted >= 2 || data_size < 3) {
    return false;
  }
  if (y_axis_fitted == 1) {
    ImPlot::SetNextAxisToFit(ImAxis_Y1);
  }
  return true;
}

inline void setup_chart(const double last_time,
                        const ImPlotFormatter y_formatter,
                        const bool auto_follow = true,
                        const bool y_auto_fit = true) {
  const ImPlotAxisFlags x_flags =
      auto_follow ? COMMON_X_FLAGS_FOLLOW : COMMON_X_FLAGS_STATIC;
  const ImPlotAxisFlags y_flags =
      y_auto_fit ? COMMON_Y_FLAGS_AUTO : COMMON_Y_FLAGS_STATIC;
  ImPlot::SetupAxes(nullptr, nullptr, x_flags, y_flags);

  ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

  const double range_start = last_time - 60;
  ImPlot::SetupAxisFitConstraints(ImAxis_X1, range_start, last_time);

  ImPlot::SetupAxisFormat(ImAxis_Y1, y_formatter);
  ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);

  ImPlot::SetupMouseText(ImPlotLocation_NorthEast);
}
