#pragma once

#include "base.h"
#include "sync.h"

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
  bool open;
  LibraryViewerStatus status;
  int pid;
  char process_name[64];
  int error_code;
  char error_message[128];

  // Data (owned by LibraryViewerState::cur_arena)
  Array<LibraryEntry> libraries;

  // Sorting and selection
  LibraryViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
  int selected_index; // -1 means no selection
};

struct LibraryViewerState {
  GrowingArray<LibraryViewerWindow> windows;
  BumpArena cur_arena;
  size_t wasted_bytes;
  int focused_window_pid = -1; // Track which window is focused for menu actions
};

struct FrameContext;
struct ViewState;
struct State;

void library_viewer_request(LibraryViewerState &state, Sync &sync, int pid,
                            const char *comm);
void library_viewer_update(LibraryViewerState &state, Sync &sync);
void library_viewer_draw(FrameContext &ctx, ViewState &view_state);
