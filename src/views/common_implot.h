#pragma once

#include "base.h"
#include "imgui.h"
#include "implot.h"

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

inline void setup_chart(const GrowingArray<double> &times,
                        const ImPlotFormatter y_formatter) {
  ImPlot::SetupAxes("Time", nullptr, ImPlotAxisFlags_AutoFit);

  ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

  const double last = times.last_or(0);
  const double range_start = last - 60;
  ImPlot::SetupAxisFitConstraints(ImAxis_X1, range_start, last);

  ImPlot::SetupAxisFormat(ImAxis_Y1, y_formatter);
  ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, HUGE_VAL);

  ImPlot::SetupMouseText(ImPlotLocation_NorthEast);
}
