#pragma once

#include "../sources/sync.h"
#include "base.h"

#include "imgui.h"

enum LibraryViewerStatus {
  eLibraryViewerStatus_Loading,
  eLibraryViewerStatus_Ready,
  eLibraryViewerStatus_Error,
};

enum LibraryViewerColumnId {
  eLibraryViewerColumnId_Path,
  eLibraryViewerColumnId_MappedSize,
  eLibraryViewerColumnId_FileSize,
  eLibraryViewerColumnId_Count,
};

struct LibraryViewerWindow {
  LibraryViewerStatus status;
  int pid;
  ImGuiID dock_id;
  char process_name[64];
  char error_message[128];
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
  size_t wasted_bytes;
};

struct FrameContext;
struct ViewState;
struct State;

void library_viewer_request(LibraryViewerState &state, Sync &sync, int pid,
                            const char *comm, ImGuiID dock_id = 0,
                            ProcessWindowFlags extra_flags = 0);
void library_viewer_update(LibraryViewerState &state, Sync &sync);
void library_viewer_draw(FrameContext &ctx, ViewState &view_state);
