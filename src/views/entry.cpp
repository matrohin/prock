#include "views/entry.h"

#include "views/brief_table.h"
#include "views/cpu_chart.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/system_cpu_chart.h"
#include "views/system_io_chart.h"
#include "views/system_mem_chart.h"
#include "views/view_state.h"

void views_update(ViewState &view_state, State &state) {
  brief_table_update(view_state.brief_table_state, state);
  cpu_chart_update(view_state.cpu_chart_state, state);
  mem_chart_update(view_state.mem_chart_state, state);
  io_chart_update(view_state.io_chart_state, state);
  system_cpu_chart_update(view_state.system_cpu_chart_state, state);
  system_mem_chart_update(view_state.system_mem_chart_state, state);
  system_io_chart_update(view_state.system_io_chart_state, state);
  library_viewer_update(view_state.library_viewer_state, *view_state.sync);
}

void views_draw(FrameContext &ctx, ViewState &view_state, const State &state) {
  brief_table_draw(ctx, view_state, state);
  cpu_chart_draw(view_state);
  mem_chart_draw(view_state);
  io_chart_draw(view_state);
  system_io_chart_draw(ctx, view_state);
  system_mem_chart_draw(ctx, view_state);
  system_cpu_chart_draw(ctx, view_state);
  library_viewer_draw(ctx, view_state);
}
