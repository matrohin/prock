#include "process_window_flags.h"

#include "views/view_state.h"

void process_window_check_close(ProcessWindowFlags &flags,
                                bool &should_be_opened) {
  if (flags & eProcessWindowFlags_CloseRequested) {
    flags &= ~eProcessWindowFlags_CloseRequested;
    should_be_opened = false;
  }
}

void process_window_handle_focus(ProcessWindowFlags &flags) {
  if (flags & eProcessWindowFlags_FocusRequested) {
    ImGui::SetWindowFocus();
    flags &= ~eProcessWindowFlags_FocusRequested;
  }
}

void process_window_handle_docking_and_pos(ViewState &view_state,
                                           const ImGuiID dock_id,
                                           ProcessWindowFlags &flags,
                                           const char *label) {
  if (dock_id != 0) {
    const ImGuiCond cond = (flags & eProcessWindowFlags_RedockRequested)
                               ? ImGuiCond_Always
                               : ImGuiCond_Once;
    ImGui::SetNextWindowDockID(dock_id, cond);
    flags &= ~eProcessWindowFlags_RedockRequested;
  } else {
    view_state.cascade.next_if_new(label);
  }
}
