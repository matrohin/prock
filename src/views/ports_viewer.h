#pragma once

#include "base/base.h"
#include "readers/port_scan_reader.h"
#include "sync.h"

#include "imgui.h"

enum PortsViewerStatus {
  ePortsViewerStatus_Loading,
  ePortsViewerStatus_Ready,
};

enum PortsViewerColumnId {
  ePortsViewerColumnId_Protocol,
  ePortsViewerColumnId_LocalAddress,
  ePortsViewerColumnId_RemoteAddress,
  ePortsViewerColumnId_State,
  ePortsViewerColumnId_Pid,
  ePortsViewerColumnId_Process,
  ePortsViewerColumnId_Count,
};

struct PortsViewerState {
  bool was_visible;
  bool layout_inited;
  bool focus_filter;
  bool permission_limited;
  PortsViewerStatus status;

  BumpArena cur_arena;
  Array<PortEntry> entries;

  char filter_text[256];
  int selected_index = -1;
  int context_menu_column = 0;

  int scan_error_code;
  int netlink_error_code;

  double last_updated = 0.0;

  PortsViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
};

struct FrameContext;
struct ViewState;

void ports_viewer_update(PortsViewerState &state, Sync &sync);
void ports_viewer_draw(FrameContext &ctx, ViewState &view_state);
