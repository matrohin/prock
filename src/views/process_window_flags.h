#pragma once

#include "base.h"

#include "imgui.h"

using ProcessWindowFlags = int;

enum ProcessWindowFlags_ {
  eProcessWindowFlags_None = 0,
  eProcessWindowFlags_CloseRequested = 1 << 0,
  eProcessWindowFlags_RedockRequested = 1 << 1,
};

template <class T>
void process_window_close(ImGuiID dock_id, GrowingArray<T> &windows, int pid) {
  T *data = windows.data();
  const size_t size = windows.size();
  const size_t i = bin_search_exact(
      size, [data](const size_t mid) { return data[mid].pid; }, pid);
  if (i < size && data[i].dock_id == dock_id) {
    data[i].flags |= eProcessWindowFlags_CloseRequested;
  }
}

template <class T>
void process_window_redock(GrowingArray<T> &windows, const int pid) {
  T *data = windows.data();
  const size_t size = windows.size();
  const size_t i = bin_search_exact(
      size, [data](const size_t mid) { return data[mid].pid; }, pid);
  if (i < size) {
    data[i].flags |= eProcessWindowFlags_RedockRequested;
  }
}

void process_window_check_close(ProcessWindowFlags &flags,
                                bool &should_be_opened);

void process_window_handle_docking_and_pos(ViewState &view_state,
                                            ImGuiID dock_id,
                                            ProcessWindowFlags &flags,
                                            const char *label);