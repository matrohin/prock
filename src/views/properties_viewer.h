#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "process_window_flags.h"
#include "readers/properties_reader.h"
#include "sync.h"
#include "views/ui.h"

#include "imgui.h"
#include "on_demand_common.h"

struct PropertiesViewerWindow {
  OnDemandWindow od;

  // Strings owned by PropertiesViewerState::cur_arena.
  ProcessProperties props;

  // What was highlighted in the value box when the context menu was opened
  UiTextSelection menu_selection;
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
