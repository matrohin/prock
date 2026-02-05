#pragma once

#include "imgui.h"
#include "imgui_internal.h"

inline int format_memory_bytes(const double bytes, char *buff, const int size) {
  if (bytes >= 1024.0 * 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f G", bytes / (1024.0 * 1024.0 * 1024.0));
  }
  if (bytes >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f M", bytes / (1024.0 * 1024.0));
  }
  if (bytes >= 1024.0) {
    return snprintf(buff, size, "%.0f K", bytes / 1024.0);
  }
  return snprintf(buff, size, "%.0f B", bytes);
}

inline const char *get_state_tooltip(const char state) {
  switch (state) {
  case 'R':
    return "Running";
  case 'S':
    return "Sleeping (interruptible)";
  case 'D':
    return "Disk sleep (uninterruptible)";
  case 'Z':
    return "Zombie";
  case 'T':
    return "Stopped (signal)";
  case 't':
    return "Tracing stop";
  case 'X':
  case 'x':
    return "Dead";
  case 'I':
    return "Idle";
  default:
    return nullptr;
  }
}

inline void table_item_draw_state(const char state) {
  ImGui::Text("%c", state);
  if (ImGui::IsItemHovered()) {
    const char *desc = get_state_tooltip(state);
    if (desc) ImGui::SetTooltip("%s", desc);
  }
}

inline void table_item_draw_float(const double value) {
  ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", value);
}

inline void table_item_draw_long(const long value) {
  ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%ld", value);
}

inline void table_item_draw_text(const char *text) { ImGui::Text("%s", text); }

inline void table_item_draw_memory(const double bytes) {
  char mem_buf[32];
  format_memory_bytes(bytes, mem_buf, sizeof(mem_buf));
  ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", mem_buf);
}
