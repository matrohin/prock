#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "process_window_flags.h"
#include "sync.h"

#include "imgui.h"
#include "on_demand_common.h"

enum EnvironViewerColumnId {
  eEnvironViewerColumnId_Name,
  eEnvironViewerColumnId_Value,
  eEnvironViewerColumnId_Count,
};

struct EnvironViewerWindow {
  OnDemandWindow od;

  int selected_child_index; // -1 means parent selected, >= 0 means child
                            // segment

  // Data (owned by EnvironViewerState::cur_arena)
  Array<EnvironEntry> entries;
};

struct EnvironViewerState {
  GrowingArray<EnvironViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;
struct State;

void environ_viewer_request(EnvironViewerState &state, Sync &sync, Pid pid,
                            const char *comm, ImGuiID dock_id = 0,
                            ProcessWindowFlags extra_flags = 0);
void environ_viewer_update(EnvironViewerState &state, Sync &sync);
void environ_viewer_draw(FrameContext &ctx, ViewState &view_state);
