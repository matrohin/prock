#pragma once

#include "views/brief_table.h"
#include "views/cpu_chart.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/system_cpu_chart.h"
#include "views/system_io_chart.h"
#include "views/system_mem_chart.h"

#include "imgui_internal.h"

struct CascadeLayout {
  ImVec2 start = {30, 30};
  ImVec2 pos = {30, 30};
  ImVec2 offset = {30, 30};
  ImVec2 size = {500, 400};

  void next() {
    ImVec2 viewport_size = ImGui::GetMainViewport()->Size;

    // Reset Y and shift start to the right when hitting bottom
    if (pos.y + size.y > viewport_size.y) {
      start.x += offset.x;
      start.y = 30;
      pos = start;
    }

    // Full reset when hitting right edge
    if (pos.x + size.x > viewport_size.x) {
      start = {30, 30};
      pos = start;
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(size, ImGuiCond_Once);
    pos.x += offset.x;
    pos.y += offset.y;
  }

  void next_if_new(const char *window_name) {
    if (!ImGui::FindWindowByName(window_name)) {
      next();
    }
  }
};

struct FrameContext {
  BumpArena frame_arena;
};

struct ViewState {
  Sync *sync;
  CascadeLayout cascade;

  BriefTableState brief_table_state;
  CpuChartState cpu_chart_state;
  MemChartState mem_chart_state;
  IoChartState io_chart_state;
  SystemCpuChartState system_cpu_chart_state;
  SystemMemChartState system_mem_chart_state;
  SystemIoChartState system_io_chart_state;
  LibraryViewerState library_viewer_state;
};
