#include "process_host.h"

#include "views/common.h"
#include "views/cpu_chart.h"
#include "views/environ_viewer.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/net_chart.h"
#include "views/threads_viewer.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

static void close_docked_children(const ImGuiID dock_id, ViewState &view_state,
                                  const int pid) {
  process_window_close(dock_id, view_state.cpu_chart_state.charts, pid);
  process_window_close(dock_id, view_state.mem_chart_state.charts, pid);
  process_window_close(dock_id, view_state.io_chart_state.charts, pid);
  process_window_close(dock_id, view_state.net_chart_state.charts, pid);
  process_window_close(dock_id, view_state.library_viewer_state.windows, pid);
  process_window_close(dock_id, view_state.environ_viewer_state.windows, pid);
  process_window_close(dock_id, view_state.threads_viewer_state.windows, pid);
}

void process_host_restore_layout(ViewState &view_state, const int pid) {
  process_window_redock(view_state.cpu_chart_state.charts, pid);
  process_window_redock(view_state.mem_chart_state.charts, pid);
  process_window_redock(view_state.io_chart_state.charts, pid);
  process_window_redock(view_state.net_chart_state.charts, pid);
  process_window_redock(view_state.library_viewer_state.windows, pid);
  process_window_redock(view_state.environ_viewer_state.windows, pid);
  process_window_redock(view_state.threads_viewer_state.windows, pid);
}

ImGuiID process_host_open(ProcessHostState &state, const int pid,
                          const char *comm) {
  for (size_t i = 0; i < state.windows.size(); ++i) {
    ProcessHostWindow &win = state.windows.data()[i];
    if (win.pid == pid) return 0;
  }

  ProcessHostWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->pid = pid;
  snprintf(win->title, sizeof(win->title), "Process: %s (%d)###ProcHost%d",
           comm, pid, pid);
  win->dockspace_id = ImGui::GetID(win->title);

  return win->dockspace_id;
}

void process_host_draw(ViewState &view_state) {
  ZoneScoped;
  ProcessHostState &my_state = view_state.process_host_state;
  size_t last = 0;

  bool should_be_opened = true;
  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    const ProcessHostWindow &win = my_state.windows.data()[last];
    view_state.cascade.next_if_new(win.title);

    if (ImGui::Begin(win.title, &should_be_opened, COMMON_VIEW_FLAGS)) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        my_state.focused_pid = win.pid;
      }
      ImGui::DockSpace(win.dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    }
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      close_docked_children(win.dockspace_id, view_state, win.pid);
      my_state.wasted_bytes += sizeof(ProcessHostWindow);
    }
  }
  my_state.windows.shrink_to(last);
}
