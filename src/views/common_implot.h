#pragma once

#include "base/base.h"
#include "imgui.h"
#include "implot.h"
#include "views/common_charts.h"

// Show tooltip with top process on chart hover
// format_value :: (double, char *, int, void *)
template <class F>
void show_top_process_tooltip(const GrowingArray<double> &times,
                              const GrowingArray<TopProcess> &top_processes,
                              const char *system_label,
                              const GrowingArray<double> &system_values,
                              F format_value) {
  if (!ImPlot::IsPlotHovered() || times.size() == 0) {
    return;
  }
  ImPlotPoint mouse = ImPlot::GetPlotMousePos();
  size_t idx = lower_bound(
      times.size(), [&times](size_t i) { return times.data()[i]; }, mouse.x);
  if (idx >= times.size()) {
    return;
  }
  constexpr bool is_system = true;
  constexpr bool is_process = false;
  char system_buf[32];
  format_value(system_values.data()[idx], system_buf, sizeof(system_buf),
               const_cast<bool *>(&is_system));
  ImGui::BeginTooltip();
  ImGui::Text("%s: %s", system_label, system_buf);
  const TopProcess &top = top_processes.data()[idx];
  if (top.pid > 0) {
    char top_buf[32];
    format_value(top.value, top_buf, sizeof(top_buf),
                 const_cast<bool *>(&is_process));
    ImGui::Text("Top: %s (PID %d) %s", top.comm, top.pid, top_buf);
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

inline void push_fill_alpha(const float val = 0.25f) {
  ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, val);
}
inline void pop_fill_alpha() { ImPlot::PopStyleVar(); }

constexpr ImPlotAxisFlags COMMON_X_FLAGS =
    ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
constexpr ImPlotAxisFlags COMMON_Y_FLAGS = ImPlotAxisFlags_RangeFit;

// Returns true if Y axis fit was requested; caller should set y_axis_fitted =
// true after BeginPlot succeeds.
inline bool try_initial_y_fit(bool y_axis_fitted, size_t data_size) {
  if (y_axis_fitted || data_size < 3) {
    return false;
  }
  ImPlot::SetNextAxisToFit(ImAxis_Y1);
  return true;
}

inline void setup_chart(const GrowingArray<double> &times,
                        const ImPlotFormatter y_formatter) {
  ImPlot::SetupAxes("Time", nullptr, COMMON_X_FLAGS, COMMON_Y_FLAGS);

  ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

  const double last = times.last_or(0);
  const double range_start = last - 60;
  ImPlot::SetupAxisFitConstraints(ImAxis_X1, range_start, last);

  ImPlot::SetupAxisFormat(ImAxis_Y1, y_formatter);
  ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);

  ImPlot::SetupMouseText(ImPlotLocation_NorthEast);
}
