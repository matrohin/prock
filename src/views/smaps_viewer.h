#pragma once

#include "base/base.h"
#include "sources/sync.h"

#include "imgui.h"

enum SmapsViewerColumnId {
  eSmapsViewerColumnId_Address,
  eSmapsViewerColumnId_Perms,
  eSmapsViewerColumnId_Size,
  eSmapsViewerColumnId_Rss,
  eSmapsViewerColumnId_Pss,
  eSmapsViewerColumnId_Private,
  eSmapsViewerColumnId_Swap,
  eSmapsViewerColumnId_Mapping,
  eSmapsViewerColumnId_SegmentCount, // grouped mode only
  eSmapsViewerColumnId_Count,
};

struct SmapsViewerWindow {
  OnDemandViewerStatus status;
  Pid pid;
  ImGuiID dock_id;
  char process_name[64];
  int error_code;
  int selected_index;
  char filter_text[256];

  ProcessWindowFlags flags;

  // Data (owned by SmapsViewerState::cur_arena)
  Array<SmapsSegment> segments;

  bool grouped;
  bool refresh_pending;

  // Sorting
  SmapsViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;

  double last_updated;
};

struct SmapsViewerState {
  GrowingArray<SmapsViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t updates_since_last_cleanup;
};

struct FrameContext;
struct ViewState;

void smaps_viewer_request(SmapsViewerState &state, Sync &sync, Pid pid,
                          const char *comm, ImGuiID dock_id = 0,
                          ProcessWindowFlags extra_flags = 0);
void smaps_viewer_update(SmapsViewerState &state, Sync &sync);
void smaps_viewer_draw(FrameContext &ctx, ViewState &view_state);
