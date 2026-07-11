#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "process_window_flags.h"
#include "readers/properties_reader.h"
#include "sync.h"

#include "imgui.h"
#include "on_demand_common.h"

struct PropertiesViewerWindow {
  OnDemandViewerStatus status;
  Pid pid;
  ImGuiID dock_id;
  char process_name[64];
  int error_code;
  int selected_index; // -1 means no selection

  ProcessWindowFlags flags;

  // Strings owned by PropertiesViewerState::cur_arena.
  ProcessProperties props;
};

struct PropertiesViewerState {
  GrowingArray<PropertiesViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;
struct State;

void properties_viewer_request(PropertiesViewerState &state, Sync &sync,
                               Pid pid, const char *comm, ImGuiID dock_id = 0,
                               ProcessWindowFlags extra_flags = 0);
void properties_viewer_update(PropertiesViewerState &state, Sync &sync);
void properties_viewer_draw(FrameContext &ctx, ViewState &view_state,
                            const State &state);
