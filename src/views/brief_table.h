#pragma once

#include "base.h"

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
  eBriefTableColumnId_Count,
};

struct BriefTableLine {
  int pid;
  size_t state_index;
};

struct BriefTableState {
  Array<BriefTableLine> lines;
  BriefTableColumnId sorted_by;
  ImGuiSortDirection sorted_order;
  int selected_pid;  // -1 means no selection
};

void brief_table_update(
  BriefTableState &my_state, State &state, const StateSnapshot &old);

void brief_table_draw(ViewState &view_state, const State &state);
