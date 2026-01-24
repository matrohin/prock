#pragma once

#include "../sources/sync.h"
#include "base.h"

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
  int pid;
  ImGuiID dock_id;
  char process_name[64];
  char error_message[128];
  int error_code;
  int selected_index; // -1 means no selection
  char filter_text[256];

  bool open;

  // Data (owned by EnvironViewerState::cur_arena)
  Array<EnvironEntry> entries;

  // Sorting and selection
  EnvironViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
};

struct EnvironViewerState {
  GrowingArray<EnvironViewerWindow> windows;
  BumpArena cur_arena;
  size_t wasted_bytes;
  int focused_window_pid = -1;
};

struct FrameContext;
struct ViewState;
struct State;

void environ_viewer_request(EnvironViewerState &state, Sync &sync, int pid,
                            const char *comm, ImGuiID dock_id = 0);
void environ_viewer_update(EnvironViewerState &state, Sync &sync);
void environ_viewer_draw(FrameContext &ctx, ViewState &view_state);
