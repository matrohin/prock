#pragma once

#include "base/base.h"
#include "process_window_flags.h"
#include "sources/sync.h"

#include "imgui.h"
#include "on_demand_common.h"

enum OpenFilesViewerColumnId {
  eOpenFilesViewerColumnId_Fd,
  eOpenFilesViewerColumnId_Type,
  eOpenFilesViewerColumnId_Access,
  eOpenFilesViewerColumnId_Size,
  eOpenFilesViewerColumnId_Path,
  eOpenFilesViewerColumnId_Count,
};

struct OpenFilesViewerWindow {
  OnDemandViewerStatus status;
  Pid pid;
  ImGuiID dock_id;
  char process_name[64];
  int error_code;
  int selected_index; // -1 means no selection
  char filter_text[256];

  ProcessWindowFlags flags;

  // Data (owned by OpenFilesViewerState::cur_arena)
  Array<OpenFileEntry> files;

  // Sorting
  OpenFilesViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;

  double last_updated;
};

struct OpenFilesViewerState {
  GrowingArray<OpenFilesViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;

void open_files_viewer_request(OpenFilesViewerState &state, Sync &sync, Pid pid,
                               const char *comm, ImGuiID dock_id = 0,
                               ProcessWindowFlags extra_flags = 0);
void open_files_viewer_update(OpenFilesViewerState &state, Sync &sync);
void open_files_viewer_draw(FrameContext &ctx, ViewState &view_state);
