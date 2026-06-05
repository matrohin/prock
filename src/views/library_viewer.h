#pragma once

#include "base/base.h"
#include "sources/sync.h"

#include "imgui.h"

enum LibraryViewerColumnId {
  eLibraryViewerColumnId_Path,
  eLibraryViewerColumnId_MappedSize,
  eLibraryViewerColumnId_FileSize,
  eLibraryViewerColumnId_Count,
};

struct LibraryViewerWindow {
  OnDemandViewerStatus status;
  Pid pid;
  ImGuiID dock_id;
  char process_name[64];
  int error_code;
  int selected_index; // -1 means no selection
  char filter_text[256];

  ProcessWindowFlags flags;

  // Data (owned by LibraryViewerState::cur_arena)
  Array<LibraryEntry> libraries;

  // Sorting and selection
  LibraryViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
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
