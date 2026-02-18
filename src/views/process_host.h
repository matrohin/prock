#pragma once

#include "base/base.h"
#include "views/process_window_flags.h"

#include "imgui.h"

struct ProcessHostWindow {
  Pid pid;
  ImGuiID dockspace_id;
  char title[64];
  ProcessWindowFlags flags;
  bool open;
};

struct ProcessHostState {
  GrowingArray<ProcessHostWindow> windows;
  BumpArena cur_arena;
  uint32_t wasted_bytes;
  Pid focused_pid = -1;
};

struct ViewState;

ImGuiID process_host_open(ProcessHostState &state, Pid pid, const char *comm);
void process_host_draw(ViewState &view_state);
void process_host_restore_layout(ViewState &view_state, Pid pid);
