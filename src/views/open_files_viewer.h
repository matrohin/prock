#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "process_window_flags.h"
#include "sync.h"

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
  OnDemandWindow od;

  // Data (owned by OpenFilesViewerState::cur_arena)
  Array<OpenFileEntry> files;
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
