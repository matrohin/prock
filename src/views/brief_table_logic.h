#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "readers/process_stat.h"
#include "state/state.h"

#include "imgui.h"

#include <cstdint>

struct FrameContext;
struct Notifications;
struct State;
struct StateSnapshot;
struct Sync;
struct ViewState;

enum BriefTableColumnId {
  eBriefTableColumnId_Pid,
  eBriefTableColumnId_Name,
  eBriefTableColumnId_Username,
  eBriefTableColumnId_State,
  eBriefTableColumnId_Threads,
  eBriefTableColumnId_StartTime,
  eBriefTableColumnId_CpuTotalPerc,
  eBriefTableColumnId_CpuUserPerc,
  eBriefTableColumnId_CpuKernelPerc,
  eBriefTableColumnId_MemRssBytes,
  eBriefTableColumnId_MemVirtBytes,
  eBriefTableColumnId_IoReadKbPerSec,
  eBriefTableColumnId_IoWriteKbPerSec,
  eBriefTableColumnId_Nice,
  eBriefTableColumnId_CmdLine,
  eBriefTableColumnId_Count,
};

struct BriefTableLine {
  ConstString name;
  long num_threads;
  long nice;
  const char *cmdline;
  PersistentString username;
  ProcessDerivedStat derived_stat;
  int64_t first_seen_ns;
  int64_t death_time_ns;
  int64_t start_time_epoch_sec; // 0 if unknown
  Pid pid;
  Pid ppid;
  int tree_depth; // 0 for root, incremented for children (used in tree mode)
  char state;
  uint8_t filter_state; // 0=hidden, 1=match, 2=ancestor (grayed), 3=subtree
};

struct BriefTableState {
  Array<BriefTableLine> lines;
  int64_t lines_at_ns;
  BriefTableColumnId sorted_by;
  ImGuiSortDirection sorted_order;
  Pid selected_pid = -1;
  int context_menu_column = 0;
  char filter_text[256];

  Pid control_edit_pid;        // PID being edited, 0 if none
  uint64_t affinity_edit_mask; // Affinity mask being edited
  int64_t type_search_time_ns; // Last keystroke timestamp for timeout
  int priority_edit_nice;      // Nice value being edited
  bool show_affinity_popup;
  bool show_priority_popup;
  bool tree_mode = true;
  bool focus_filter_requested = false; // set by the "Filter processes" command
  char type_search[32];                // Current search string
};

uint32_t binary_search_pid(const Array<ProcessStat> &stats, int pid);

void sort_brief_table_lines(BriefTableState &my_state);
void sort_brief_table_tree(BriefTableState &my_state, BumpArena &temp_arena);
