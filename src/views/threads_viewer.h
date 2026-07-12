#pragma once

#include "base/containers.h"
#include "process_window_flags.h"
#include "sync.h"

#include "imgui.h"
#include "on_demand_common.h"

struct ThreadLine {
  double cpu_user_perc;
  double cpu_kernel_perc;
  int tid;
  int last_cpu; // Last CPU the thread ran on, -1 if unknown
  char comm[64];
  char wchan[64]; // Kernel function the thread is blocked in, "" if running
  char state;
};

// Per-thread CPU counters kept for cross-update deltas. Storing only these
// (not full ProcessStat) avoids dangling snapshot-owned comm/cmdline pointers.
struct ThreadCpuSample {
  Pid pid;
  ulong utime;
  ulong stime;
  SteadyTimePoint read_time;
};

enum ThreadsViewerColumnId {
  eThreadsViewerColumnId_Tid,
  eThreadsViewerColumnId_Name,
  eThreadsViewerColumnId_State,
  eThreadsViewerColumnId_Wchan,
  eThreadsViewerColumnId_CpuTotal,
  eThreadsViewerColumnId_CpuKernel,
  eThreadsViewerColumnId_LastCpu,
  eThreadsViewerColumnId_Count,
};

struct ThreadsViewerWindow {
  Pid pid;
  char process_name[64];
  ImGuiID dock_id;
  ProcessWindowFlags flags;

  // Status tracking
  OnDemandViewerStatus status;
  char error_message[128];
  int error_code;

  // Display data (owned by ThreadsViewerState::cur_arena, rebuilt on each
  // snapshot)
  Array<ThreadLine> lines;

  // Previous snapshot for delta computation (owned by cur_arena)
  // Stored in TID-sorted order (as returned by read_process_threads)
  // for linear-scan delta matching
  Array<ThreadCpuSample> prev_threads;

  // UI state
  int selected_tid;
  int context_menu_column;
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

void threads_viewer_open(ThreadsViewerState &state, Sync &sync, Pid pid,
                         const char *comm, ImGuiID dock_id = 0,
                         ProcessWindowFlags extra_flags = 0);
void threads_viewer_update(FrameContext &ctx, ThreadsViewerState &state,
                           Sync &sync, const State &state_data);
void threads_viewer_draw(FrameContext &ctx, ViewState &view_state,
                         const State &state);
