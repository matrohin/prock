#pragma once

#include "base/base.h"
#include "sources/sync.h"

#include "imgui.h"

enum EnvironViewerStatus {
  eEnvironViewerStatus_Loading,
  eEnvironViewerStatus_Ready,
  eEnvironViewerStatus_Error,
};

enum EnvironViewerColumnId {
  eEnvironViewerColumnId_Name,
  eEnvironViewerColumnId_Value,
  eEnvironViewerColumnId_Count,
};

struct EnvironViewerWindow {
  EnvironViewerStatus status;
  Pid pid;
  ImGuiID dock_id;
  char process_name[64];
  int error_code;
  int selected_index;       // -1 means no selection
  int selected_child_index; // -1 means parent selected, >= 0 means child segment
  char filter_text[256];

  ProcessWindowFlags flags;

  // Data (owned by EnvironViewerState::cur_arena)
  Array<EnvironEntry> entries;

  // Sorting and selection
  EnvironViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
};

struct EnvironViewerState {
  GrowingArray<EnvironViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;
struct State;

void environ_viewer_request(EnvironViewerState &state, Sync &sync, Pid pid,
                            const char *comm, ImGuiID dock_id = 0,
                            ProcessWindowFlags extra_flags = 0);
void environ_viewer_update(EnvironViewerState &state, Sync &sync);
void environ_viewer_draw(FrameContext &ctx, ViewState &view_state);
void environ_viewer_close_if_docked_in(EnvironViewerState &state, Pid pid,
                                       ImGuiID dockspace_id);
void environ_viewer_restore_layout_by_pid(EnvironViewerState &state, Pid pid);
