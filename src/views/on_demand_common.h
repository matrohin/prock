#pragma once

#include "base/containers.h"
#include "base/string.h"
#include "sync.h"
#include "views/process_window_flags.h"

#include "imgui.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <mutex>

enum OnDemandViewerStatus {
  eOnDemandViewerStatus_Loading,
  eOnDemandViewerStatus_Ready,
  eOnDemandViewerStatus_Error,
};

// Fields shared by every on-demand viewer window
struct OnDemandWindow {
  OnDemandViewerStatus status;
  Pid pid;
  ImGuiID dock_id;
  ProcessWindowFlags flags;
  int error_code;
  int selected_index; // -1 means no selection
  int context_menu_column;
  int sorted_by; // per-viewer column enum value
  ImGuiSortDirection sorted_order;
  double last_updated; // ImGui::GetTime() when data last arrived, 0 = never
  char process_name[64];
  char filter_text[256];
};

struct ViewState;

String on_demand_viewer_title(BumpArena &frame_arena,
                              OnDemandViewerStatus status,
                              const char *viewer_name, uint32_t results_size,
                              const char *process_name, Pid pid);

// Fill the common fields of a freshly emplaced (zero-initialized) window.
void on_demand_window_init(OnDemandWindow &od, Pid pid, const char *comm,
                           ImGuiID dock_id, ProcessWindowFlags extra_flags);

// Fail a viewer window whose initial request was dropped
void on_demand_mark_request_dropped(OnDemandWindow &od);

// Apply a response's status: on success mark Ready, stamp last_updated
// On error store the code and return false
bool on_demand_apply_response(OnDemandWindow &od, int error_code);

// Status after a refresh click
void on_demand_refresh_status(OnDemandWindow &od, bool request_sent);

// Shell around a viewer window
bool on_demand_window_begin(ViewState &view_state, OnDemandWindow &od,
                            const char *title, bool &keep_open);
bool on_demand_window_begin(ViewState &view_state, OnDemandWindow &od,
                            const char *viewer_name, uint32_t results_size,
                            BumpArena &frame_arena, bool &keep_open);
void on_demand_window_end(OnDemandWindow &od);

// Standard table toolbar row, optional extra cells in the middle
bool on_demand_toolbar_begin(OnDemandWindow &od, ImGuiTextFilter &filter,
                             const char *filter_id, int extra_cells = 0);
bool on_demand_toolbar_end(OnDemandWindow &od, bool refresh_pending = false);

// Binary search for pid over windows spaced `stride` bytes apart
OnDemandWindow *on_demand_find_window(void *windows, uint32_t count,
                                      size_t stride, Pid pid);

template <class Win>
Win *on_demand_find(GrowingArray<Win> &windows, const Pid pid) {
  static_assert(offsetof(Win, od) == 0,
                "OnDemandWindow must be the first member, named od");
  return reinterpret_cast<Win *>(
      on_demand_find_window(windows.data(), windows.size(), sizeof(Win), pid));
}

// Returns true if the window exists and was marked for focus.
template <class Win>
bool on_demand_focus(GrowingArray<Win> &windows, const Pid pid) {
  Win *win = on_demand_find(windows, pid);
  if (win) win->od.flags |= eProcessWindowFlags_FocusRequested;
  return win != nullptr;
}

template <class Win>
void on_demand_close(const ImGuiID dock_id, GrowingArray<Win> &windows,
                     const Pid pid) {
  Win *win = on_demand_find(windows, pid);
  if (win && win->od.dock_id == dock_id) {
    win->od.flags |= eProcessWindowFlags_CloseRequested;
  }
}

template <class Win>
void on_demand_redock(GrowingArray<Win> &windows, const Pid pid) {
  if (Win *win = on_demand_find(windows, pid)) {
    win->od.flags |= eProcessWindowFlags_RedockRequested;
  }
}

// Restore the sorted-by-pid invariant
template <class Win> void on_demand_sort_added(GrowingArray<Win> &windows) {
  std::sort(windows.begin(), windows.end(),
            [](const Win &l, const Win &r) { return l.od.pid < r.od.pid; });
}

// Push a request to an on-demand reader queue - under the same mutex its CV
// wait predicate is checked with - and wake the reader. Returns false when the
// queue is full and the request was dropped.
template <class Req, uint32_t N>
bool on_demand_send_request(Sync &sync, Channel<Req, N> &queue,
                            const Req &req) {
  bool pushed = false;
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    pushed = queue.push(req);
  }
  if (pushed) sync.on_demand_reader.request_read_cv.notify_one();
  return pushed;
}
