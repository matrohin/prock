#include "views/entry.h"

#include "views/brief_table.h"
#include "views/cpu_chart.h"
#include "views/environ_viewer.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/open_files_viewer.h"
#include "views/ports_viewer.h"
#include "views/process_host.h"
#include "views/properties_viewer.h"
#include "views/replay_controls.h"
#include "views/smaps_viewer.h"
#include "views/socket_viewer.h"
#include "views/system_cpu_chart.h"
#include "views/system_io_chart.h"
#include "views/system_mem_chart.h"
#include "views/system_net_chart.h"
#include "views/threads_viewer.h"
#include "views/view_state.h"

#include "tracy/Tracy.hpp"

void entry_views_update(FrameContext &ctx, ViewState &view_state,
                        State &state) {
  ZoneScoped;
  const bool paused = !view_state.preferences_state.auto_follow;
  if (!paused)
    brief_table_update(ctx, view_state.brief_table_state,
                       view_state.string_interner, state);
  cpu_chart_update(view_state.cpu_chart_state, state);
  mem_chart_update(view_state.mem_chart_state, state);
  io_chart_update(view_state.io_chart_state, state);
  system_cpu_chart_update(view_state.system_cpu_chart_state,
                          *view_state.persistent_arena,
                          view_state.string_interner, state);
  system_mem_chart_update(view_state.system_mem_chart_state,
                          view_state.string_interner, state);
  system_io_chart_update(view_state.system_io_chart_state, state);
  system_net_chart_update(view_state.system_net_chart_state, state);
  if (!paused)
    threads_viewer_update(ctx, view_state.threads_viewer_state,
                          *view_state.sync, state);
}

void entry_views_reset_history(ViewState &view_state) {
  common_charts_clear(view_state.cpu_chart_state.charts);
  common_charts_clear(view_state.mem_chart_state.charts);
  common_charts_clear(view_state.io_chart_state.charts);
  view_state.system_cpu_chart_state.track = {};
  view_state.system_mem_chart_state.track = {};
  view_state.system_io_chart_state.track = {};
  view_state.system_net_chart_state.track = {};
  // Drop the process rows so the restarted pass recomputes new/old highlighting
  // from scratch instead of measuring ages against the previous pass's
  // timestamps (which would flag every row as new and never expire dead ones).
  view_state.brief_table_state.lines = {};
}

void entry_views_on_demand_update(ViewState &view_state) {
  ZoneScoped;
  menu_bar_update(view_state);
  library_viewer_update(view_state.library_viewer_state, *view_state.sync);
  environ_viewer_update(view_state.environ_viewer_state, *view_state.sync);
  socket_viewer_update(view_state.socket_viewer_state, *view_state.sync);
  open_files_viewer_update(view_state.open_files_viewer_state,
                           *view_state.sync);
  smaps_viewer_update(view_state.smaps_viewer_state, *view_state.sync);
  ports_viewer_update(view_state.ports_viewer_state, *view_state.sync);
  properties_viewer_update(view_state.properties_viewer_state,
                           *view_state.sync);
  brief_table_dump_update(view_state.notifications, *view_state.sync);
  notifications_update(view_state.notifications);
}

void entry_views_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state) {
  ZoneScoped;
  menu_bar_draw(view_state);
  process_host_draw(view_state);
  cpu_chart_draw(view_state);
  mem_chart_draw(view_state);
  io_chart_draw(view_state);
  system_io_chart_draw(ctx, view_state);
  system_net_chart_draw(ctx, view_state);
  system_mem_chart_draw(ctx, view_state);
  system_cpu_chart_draw(ctx, view_state);
  // Drawn right after the per-process charts so it docks as the first tab after
  // them in the process host.
  properties_viewer_draw(ctx, view_state, state);
  library_viewer_draw(ctx, view_state);
  environ_viewer_draw(ctx, view_state);
  threads_viewer_draw(ctx, view_state, state);
  socket_viewer_draw(ctx, view_state);
  open_files_viewer_draw(ctx, view_state);
  smaps_viewer_draw(ctx, view_state);
  ports_viewer_draw(ctx, view_state);
  brief_table_draw(ctx, view_state, state);
  replay_overlay_draw(view_state);
  notifications_draw(ctx, view_state.notifications);

  command_dispatch(view_state);
  command_palette_draw(view_state);
  replay_open_dialog_draw(view_state);
}
