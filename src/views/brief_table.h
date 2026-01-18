#pragma once

#include "base.h"
#include "process_stat.h"

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
  eBriefTableColumnId_Count,
};

struct BriefTableLine {
  int pid;
  size_t state_index;
};

struct BriefTreeNode {
  int pid;
  size_t state_index;
  BriefTreeNode *first_child;
  BriefTreeNode *next_sibling;
};

struct BriefTableState {
  Array<BriefTableLine> lines;
  BriefTableColumnId sorted_by;
  ImGuiSortDirection sorted_order;
  int selected_pid;  // -1 means no selection
  char kill_error[128];
  bool tree_mode;  // Toggle: false = flat, true = tree
};

void brief_table_update(BriefTableState &my_state, State &state);

void brief_table_draw(FrameContext &ctx, ViewState &view_state, const State &state);

// Pure logic functions (exposed for testing)
size_t binary_search_pid(const Array<ProcessStat> &stats, int pid);

void sort_brief_table_lines(BriefTableState &my_state, const StateSnapshot &state);

BriefTreeNode *build_process_tree(BumpArena &arena,
                                  const Array<BriefTableLine> &lines,
                                  const StateSnapshot &snapshot,
                                  BriefTableColumnId sorted_by,
                                  ImGuiSortDirection sorted_order);
