#pragma once

#include "../sources/sync.h"
#include "process_window_flags.h"

#include "imgui.h"

struct ThreadLine {
  int tid;
  char comm[64];
  char state;
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

  // Display data (owned by ThreadsViewerState::cur_arena, rebuilt on each snapshot)
  Array<ThreadLine> lines;

  // Previous snapshot for delta computation (owned by cur_arena)
  // Stored in TID-sorted order (as returned by read_process_threads)
  // for linear-scan delta matching
  Array<ProcessStat> prev_threads;
  int64_t prev_at_ns; // nanoseconds since steady_clock epoch

  // UI state
  int selected_tid;
  char filter_text[256];
  ThreadsViewerColumnId sorted_by;
  ImGuiSortDirection sorted_order;
};

struct ThreadsViewerState {
  GrowingArray<ThreadsViewerWindow> windows;
  BumpArena cur_arena;
  uint32_t wasted_bytes;
};

struct FrameContext;
struct ViewState;
struct State;

void threads_viewer_open(ThreadsViewerState &state, Sync &sync, int pid,
                         const char *comm, ImGuiID dock_id = 0,
                         ProcessWindowFlags extra_flags = 0);
void threads_viewer_update(ThreadsViewerState &state, const State &state_data);
void threads_viewer_draw(FrameContext &ctx, ViewState &view_state,
                         const State &state);
