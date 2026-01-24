#pragma once

#include "base.h"

#include "imgui.h"

struct ProcessHostWindow {
  int pid;
  ImGuiID dockspace_id;
  char title[64];
  bool open;
};

struct ProcessHostState {
  GrowingArray<ProcessHostWindow> windows;
  BumpArena cur_arena;
  size_t wasted_bytes;
};

struct ViewState;

ImGuiID process_host_open(ProcessHostState &state, int pid, const char *comm);
void process_host_draw(ViewState &view_state);
