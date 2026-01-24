#include "process_host.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"

ImGuiID process_host_open(ProcessHostState &state, const int pid,
                          const char *comm) {
  // Check if window exists for pid - reopen if found
  for (size_t i = 0; i < state.windows.size(); ++i) {
    if (state.windows.data()[i].pid == pid) {
      state.windows.data()[i].open = true;
      return state.windows.data()[i].dockspace_id;
    }
  }

  // Create new window
  ProcessHostWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->pid = pid;
  win->open = true;
  snprintf(win->title, sizeof(win->title), "Process: %s (%d)###ProcHost%d",
           comm, pid, pid);
  win->dockspace_id = ImGui::GetID(win->title);

  return win->dockspace_id;
}

void process_host_draw(ViewState &view_state) {
  ProcessHostState &my_state = view_state.process_host_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    ProcessHostWindow &win = my_state.windows.data()[last];
    if (!win.open) {
      my_state.wasted_bytes += sizeof(ProcessHostWindow);
      continue;
    }

    view_state.cascade.next_if_new(win.title);

    if (ImGui::Begin(win.title, &win.open, COMMON_VIEW_FLAGS)) {
      ImGui::DockSpace(win.dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    }
    ImGui::End();
    ++last;
  }
  my_state.windows.shrink_to(last);
}
