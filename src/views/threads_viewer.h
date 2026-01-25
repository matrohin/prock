#pragma once

#include "../sources/sync.h"
#include "process_window_flags.h"

#include "imgui.h"

struct ThreadDerivedStat {
  double cpu_user_perc;
  double cpu_kernel_perc;
  long mem_resident_bytes;
};

enum ThreadsViewerColumnId {
  eThreadsViewerColumnId_Tid,
  eThreadsViewerColumnId_Name,
  eThreadsViewerColumnId_State,
  eThreadsViewerColumnId_CpuTotal,
  eThreadsViewerColumnId_CpuKernel,
  eThreadsViewerColumnId_Memory,
  eThreadsViewerColumnId_Count,
};

enum ThreadsViewerStatus {
  eThreadsViewerStatus_Loading,
  eThreadsViewerStatus_Ready,
  eThreadsViewerStatus_Error,
};

struct ThreadsViewerWindow {
  int pid;
  char process_name[64];
  ImGuiID dock_id;
  ProcessWindowFlags flags;

  // Status tracking
  ThreadsViewerStatus status;
  char error_message[128];
  int error_code;

  // Current data (owned by ThreadsViewerState::cur_arena)
  Array<ProcessStat> threads;
  Array<ThreadDerivedStat> derived;

  // Previous snapshot for delta computation
  Array<ProcessStat> prev_threads;
  int64_t prev_at_ns;  // nanoseconds since steady_clock epoch

  // UI state
  int selected_tid;
  char filter_text[256];
  ThreadsViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
};

struct ThreadsViewerState {
  GrowingArray<ThreadsViewerWindow> windows;
  BumpArena cur_arena;
  size_t wasted_bytes;
};

struct FrameContext;
struct ViewState;
struct State;

void threads_viewer_open(ThreadsViewerState &state, Sync &sync, int pid,
                         const char *comm, ImGuiID dock_id = 0);
void threads_viewer_update(ThreadsViewerState &state, const State &state_data,
                           Sync &sync);
void threads_viewer_draw(FrameContext &ctx, ViewState &view_state,
                         const State &state);
void threads_viewer_process_snapshot(ThreadsViewerState &state,
                                     const State &state_data,
                                     const Array<ThreadSnapshot> &snapshots);
