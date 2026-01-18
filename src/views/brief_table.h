#pragma once

#include "base.h"

struct FrameContext;
struct State;
struct StateSnapshot;

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

void brief_table_update(
  BriefTableState &my_state, State &state, const StateSnapshot &old);

void brief_table_draw(FrameContext &ctx, ViewState &view_state, const State &state);
