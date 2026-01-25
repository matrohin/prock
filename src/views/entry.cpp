#include "views/entry.h"

#include "views/brief_table.h"
#include "views/cpu_chart.h"
#include "views/environ_viewer.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/net_chart.h"
#include "views/process_host.h"
#include "views/socket_viewer.h"
#include "views/system_cpu_chart.h"
#include "views/system_io_chart.h"
#include "views/system_mem_chart.h"
#include "views/system_net_chart.h"
#include "views/threads_viewer.h"
#include "views/view_state.h"

#include "tracy/Tracy.hpp"

void views_update(ViewState &view_state, State &state) {
  ZoneScoped;
  brief_table_update(view_state.brief_table_state, state);
  cpu_chart_update(view_state.cpu_chart_state, state);
  mem_chart_update(view_state.mem_chart_state, state);
  io_chart_update(view_state.io_chart_state, state);
  net_chart_update(view_state.net_chart_state, state);
  system_cpu_chart_update(view_state.system_cpu_chart_state, state);
  system_mem_chart_update(view_state.system_mem_chart_state, state);
  system_io_chart_update(view_state.system_io_chart_state, state);
  system_net_chart_update(view_state.system_net_chart_state, state);
  library_viewer_update(view_state.library_viewer_state, *view_state.sync);
  environ_viewer_update(view_state.environ_viewer_state, *view_state.sync);
  threads_viewer_update(view_state.threads_viewer_state, state, *view_state.sync);
  socket_viewer_update(view_state.socket_viewer_state, *view_state.sync);
}

void views_draw(FrameContext &ctx, ViewState &view_state, const State &state) {
  ZoneScoped;
  menu_bar_draw(view_state);
  brief_table_draw(ctx, view_state, state);
  process_host_draw(view_state);
  cpu_chart_draw(view_state);
  mem_chart_draw(view_state);
  io_chart_draw(view_state);
  net_chart_draw(view_state);
  system_io_chart_draw(ctx, view_state);
  system_net_chart_draw(ctx, view_state);
  system_mem_chart_draw(ctx, view_state);
  system_cpu_chart_draw(ctx, view_state);
  library_viewer_draw(ctx, view_state);
  environ_viewer_draw(ctx, view_state);
  threads_viewer_draw(ctx, view_state, state);
  socket_viewer_draw(ctx, view_state);
}

void views_process_thread_snapshots(ViewState &view_state, const State &state,
                                    const UpdateSnapshot &snapshot) {
  threads_viewer_process_snapshot(view_state.threads_viewer_state, state,
                                  snapshot.thread_snapshots);
}
