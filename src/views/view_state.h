#pragma once

#include "views/brief_table.h"
#include "views/cpu_chart.h"
#include "views/mem_chart.h"

struct ViewState {
  BriefTableState brief_table_state;
  CpuChartState cpu_chart_state;
  MemChartState mem_chart_state;
};
