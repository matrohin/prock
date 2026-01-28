#pragma once

#include "../sources/sync.h"
#include "base.h"

#include "imgui.h"

enum SocketViewerStatus {
  eSocketViewerStatus_Loading,
  eSocketViewerStatus_Ready,
  eSocketViewerStatus_Error,
};

enum SocketViewerColumnId {
  eSocketViewerColumnId_Protocol,
  eSocketViewerColumnId_LocalAddress,
  eSocketViewerColumnId_RemoteAddress,
  eSocketViewerColumnId_State,
  eSocketViewerColumnId_RecvQ,
  eSocketViewerColumnId_SendQ,
  eSocketViewerColumnId_Count,
};

struct SocketViewerWindow {
  SocketViewerStatus status;
  int pid;
  ImGuiID dock_id;
  char process_name[64];
  char error_message[128];
  int error_code;
  int selected_index;
  char filter_text[256];

  ProcessWindowFlags flags;

  // Data (owned by SocketViewerState::cur_arena)
  Array<SocketEntry> sockets;

  // Sorting
  SocketViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
};

struct SocketViewerState {
  GrowingArray<SocketViewerWindow> windows;
  BumpArena cur_arena;
  size_t wasted_bytes;
};

struct FrameContext;
struct ViewState;

void socket_viewer_request(SocketViewerState &state, Sync &sync, int pid,
                           const char *comm, ImGuiID dock_id = 0,
                           ProcessWindowFlags extra_flags = 0);
void socket_viewer_update(SocketViewerState &state, Sync &sync);
void socket_viewer_draw(FrameContext &ctx, ViewState &view_state);
