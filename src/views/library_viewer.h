#pragma once

#include "base.h"
#include "sync.h"

enum LibraryViewerStatus {
  eLibraryViewerStatus_Loading,
  eLibraryViewerStatus_Ready,
  eLibraryViewerStatus_Error,
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
};

struct LibraryViewerState {
  GrowingArray<LibraryViewerWindow> windows;
  BumpArena cur_arena;
  size_t wasted_bytes;
};

struct ViewState;
struct State;

void library_viewer_request(LibraryViewerState &state, Sync &sync, int pid, const char *comm);
void library_viewer_update(LibraryViewerState &state, Sync &sync);
void library_viewer_draw(ViewState &view_state, const State &state);
