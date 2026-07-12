#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "process_window_flags.h"
#include "sync.h"

#include "imgui.h"
#include "on_demand_common.h"

enum LibraryViewerColumnId {
  eLibraryViewerColumnId_Path,
  eLibraryViewerColumnId_MappedSize,
  eLibraryViewerColumnId_FileSize,
  eLibraryViewerColumnId_Count,
};

struct LibraryViewerWindow {
  OnDemandWindow od;

  // Data (owned by LibraryViewerState::cur_arena)
  Array<LibraryEntry> libraries;
};

struct LibraryViewerState {
  GrowingArray<LibraryViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;
struct State;

void library_viewer_request(LibraryViewerState &state, Sync &sync, Pid pid,
                            const char *comm, ImGuiID dock_id = 0,
                            ProcessWindowFlags extra_flags = 0);
void library_viewer_update(LibraryViewerState &state, Sync &sync);
void library_viewer_draw(FrameContext &ctx, ViewState &view_state);
