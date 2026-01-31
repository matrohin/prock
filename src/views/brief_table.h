#pragma once

#include "../sources/process_stat.h"
#include "base.h"

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
  eBriefTableColumnId_NetRecvKbPerSec,
  eBriefTableColumnId_NetSendKbPerSec,
  eBriefTableColumnId_Count,
};

struct BriefTableLine {
  int pid;
  int ppid;
  const char *comm;
  char state;
  long num_threads;

  ProcessDerivedStat derived_stat;

  int64_t first_seen_ns;
  int64_t death_time_ns;
  int tree_depth; // 0 for root, incremented for children (used in tree mode)
  uint8_t filter_state; // 0=hidden, 1=matches filter, 2=ancestor of match (grayed)
};

struct BriefTableState {
  Array<BriefTableLine> lines;
  BriefTableColumnId sorted_by;
  ImGuiSortDirection sorted_order;
  int selected_pid; // -1 means no selection
  char kill_error[128];
  bool tree_mode; // Toggle: false = flat, true = tree
  char filter_text[256];
};

void brief_table_update(BriefTableState &my_state, State &state);

void brief_table_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state);

// Pure logic functions (exposed for testing)
size_t binary_search_pid(const Array<ProcessStat> &stats, int pid);

void sort_brief_table_lines(BriefTableState &my_state);
void sort_brief_table_tree(BriefTableState &my_state, BumpArena &arena);
