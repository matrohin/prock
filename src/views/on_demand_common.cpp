#include "on_demand_common.h"

#include "base/algorithms.h"
#include "constants.h"
#include "views/labels.h"
#include "views/ui.h"

#include <cfloat>
#include <cstdio>

String on_demand_viewer_title(BumpArena &frame_arena,
                              const OnDemandViewerStatus status,
                              const char *viewer_name,
                              const uint32_t results_size,
                              const char *process_name, const Pid pid) {
  switch (status) {
  case eOnDemandViewerStatus_Error:
    return String::sprintf(frame_arena, "%s (Error) - %s (%d)###%s%d",
                           viewer_name, process_name, pid, viewer_name, pid);
  case eOnDemandViewerStatus_Loading:
    return String::sprintf(frame_arena, "%s (Loading...) - %s (%d)###%s%d",
                           viewer_name, process_name, pid, viewer_name, pid);
  case eOnDemandViewerStatus_Ready:
    return String::sprintf(frame_arena, "%s (%u) - %s (%d)###%s%d", viewer_name,
                           results_size, process_name, pid, viewer_name, pid);
  }
  return INTERNAL_ERROR;
}

void on_demand_window_init(OnDemandWindow &od, const Pid pid, const char *comm,
                           const ImGuiID dock_id,
                           const ProcessWindowFlags extra_flags) {
  od.status = eOnDemandViewerStatus_Loading;
  od.pid = pid;
  od.dock_id = dock_id;
  od.flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  od.error_code = 0;
  od.selected_index = -1;
  od.context_menu_column = 0;
  od.last_updated = 0.0;
  snprintf(od.process_name, sizeof(od.process_name), "%s", comm);
}

void on_demand_mark_request_dropped(OnDemandWindow &od) {
  od.status = eOnDemandViewerStatus_Error;
  od.error_code = EAGAIN;
}

bool on_demand_apply_response(OnDemandWindow &od, const int error_code) {
  if (error_code != 0) {
    od.status = eOnDemandViewerStatus_Error;
    od.error_code = error_code;
    return false;
  }
  od.status = eOnDemandViewerStatus_Ready;
  od.last_updated = ImGui::GetTime();
  return true;
}

void on_demand_refresh_status(OnDemandWindow &od, const bool request_sent) {
  od.status = request_sent ? eOnDemandViewerStatus_Loading
                           : eOnDemandViewerStatus_Ready;
}

bool on_demand_window_begin(ViewState &view_state, OnDemandWindow &od,
                            const char *title, bool &keep_open) {
  process_window_handle_docking_and_pos(view_state, od.dock_id, od.flags,
                                        title);
  keep_open = true;
  const ImGuiWindowFlags win_flags = process_window_flags(od.flags);
  if (!ImGui::Begin(title, &keep_open, win_flags)) {
    return false;
  }
  process_window_check_close(od.flags, keep_open);
  return true;
}

bool on_demand_window_begin(ViewState &view_state, OnDemandWindow &od,
                            const char *viewer_name,
                            const uint32_t results_size, BumpArena &frame_arena,
                            bool &keep_open) {
  const String title =
      on_demand_viewer_title(frame_arena, od.status, viewer_name, results_size,
                             od.process_name, od.pid);
  return on_demand_window_begin(view_state, od, title.data, keep_open);
}

void on_demand_window_end(OnDemandWindow &od) {
  process_window_handle_focus(od.flags);
  ImGui::End();
}

bool on_demand_toolbar_begin(OnDemandWindow &od, ImGuiTextFilter &filter,
                             const char *filter_id, const int extra_cells) {
  if (!ImGui::BeginTable("Header", 4 + extra_cells,
                         ImGuiTableFlags_SizingStretchSame)) {
    return false;
  }
  ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
  for (int i = 0; i < extra_cells + 2; ++i) {
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
  }
  ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch,
                          HEADER_SPACER_WEIGHT);
  ImGui::TableNextRow();

  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ui_filter_input(filter, filter_id, od.filter_text, sizeof(od.filter_text));
  return true;
}

bool on_demand_toolbar_end(OnDemandWindow &od, const bool refresh_pending) {
  ImGui::TableNextColumn();
  const bool refresh_clicked = ui_refresh_button(refresh_pending);

  ImGui::TableNextColumn();
  ui_last_updated(od.last_updated);

  ImGui::TableNextColumn(); // spacer

  ImGui::EndTable();
  return refresh_clicked;
}

OnDemandWindow *on_demand_find_window(void *windows, const uint32_t count,
                                      const size_t stride, const Pid pid) {
  uint8_t *base = static_cast<uint8_t *>(windows);
  const uint32_t idx = bin_search_exact(
      count,
      [base, stride](const uint32_t mid) {
        return reinterpret_cast<const OnDemandWindow *>(base + mid * stride)
            ->pid;
      },
      pid);
  if (idx == UINT32_MAX) {
    return nullptr;
  }
  return reinterpret_cast<OnDemandWindow *>(base + idx * stride);
}
