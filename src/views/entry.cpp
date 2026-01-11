#include "views/entry.h"

#include "views/brief_table.h"
#include "views/core_chart.h"
#include "views/cpu_chart.h"
#include "views/full_table.h"
#include "views/mem_chart.h"
#include "views/view_state.h"


void views_update(ViewState &view_state, State &state, const StateSnapshot &old_snapshot) {
  brief_table_update(view_state.brief_table_state, state, old_snapshot);
  cpu_chart_update(view_state.cpu_chart_state, state, old_snapshot);
  mem_chart_update(view_state.mem_chart_state, state, old_snapshot);
  core_chart_update(view_state.core_chart_state, state, old_snapshot);
}


void views_draw(FrameContext &ctx, ViewState &view_state, const State &state) {
  full_table_draw(state.snapshot);
  brief_table_draw(view_state, state);
  cpu_chart_draw(view_state, state);
  mem_chart_draw(view_state, state);
  core_chart_draw(ctx, view_state, state);
}

