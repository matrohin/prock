#pragma once

#include "base.h"
#include "implot.h"

inline void push_fit_with_padding(bool &auto_fit) {
  ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.5f));
  if (auto_fit) {
    ImPlot::SetNextAxesToFit();
    auto_fit = false;
  }
}

inline void pop_fit_with_padding() {
  ImPlot::PopStyleVar();
}

inline void setup_time_scale(const GrowingArray<double> &times) {
  ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

  const double last = times.last_or(0);
  const double range_start = last - 60;
  ImPlot::SetupAxisFitConstraints(ImAxis_X1, range_start, last);
}
