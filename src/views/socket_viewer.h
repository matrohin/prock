#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "process_window_flags.h"
#include "sync.h"

#include "imgui.h"
#include "on_demand_common.h"

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
  OnDemandWindow od;

  int netlink_error_code;

  // Data (owned by SocketViewerState::cur_arena)
  Array<SocketEntry> sockets;
};

struct SocketViewerState {
  GrowingArray<SocketViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;

void socket_viewer_request(SocketViewerState &state, Sync &sync, Pid pid,
                           const char *comm, ImGuiID dock_id = 0,
                           ProcessWindowFlags extra_flags = 0);
void socket_viewer_update(SocketViewerState &state, Sync &sync);
void socket_viewer_draw(FrameContext &ctx, ViewState &view_state);
