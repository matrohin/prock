#pragma once

#include "base/base.h"
#include "base/const_string.h"
#include "sources/process_stat.h"

#include "imgui.h"

struct FrameContext;
struct State;
struct StateSnapshot;
struct ViewState;

enum BriefTableColumnId {
  eBriefTableColumnId_Pid,
  eBriefTableColumnId_Name,
  eBriefTableColumnId_State,
  eBriefTableColumnId_Threads,
  eBriefTableColumnId_CpuTotalPerc,
  eBriefTableColumnId_CpuUserPerc,
  eBriefTableColumnId_CpuKernelPerc,
  eBriefTableColumnId_MemRssBytes,
  eBriefTableColumnId_MemVirtBytes,
  eBriefTableColumnId_IoReadKbPerSec,
  eBriefTableColumnId_IoWriteKbPerSec,
  eBriefTableColumnId_CmdLine,
  eBriefTableColumnId_Count,
};

struct BriefTableLine {
  ConstString name;
  long num_threads;
  const char *cmdline;
  ProcessDerivedStat derived_stat;
  int64_t first_seen_ns;
  int64_t death_time_ns;
  Pid pid;
  Pid ppid;
  int tree_depth; // 0 for root, incremented for children (used in tree mode)
  char state;
  uint8_t filter_state; // 0=hidden, 1=match, 2=ancestor (grayed), 3=subtree
};

struct BriefTableState {
  Array<BriefTableLine> lines;
  BriefTableColumnId sorted_by;
  ImGuiSortDirection sorted_order;
  Pid selected_pid = -1;
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

void brief_table_update(BriefTableState &my_state, InternTable &string_interner,
                        State &state);

void brief_table_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state);

// Pure logic functions (exposed for testing)
uint32_t binary_search_pid(const Array<ProcessStat> &stats, int pid);

void sort_brief_table_lines(BriefTableState &my_state);
void sort_brief_table_tree(BriefTableState &my_state, BumpArena &arena);
