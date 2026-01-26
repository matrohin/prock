#pragma once

#include "cpu_chart.h"
#include "imgui_internal.h"

#include <cstdio>

constexpr ImGuiWindowFlags COMMON_VIEW_FLAGS = ImGuiWindowFlags_NoCollapse;

// ImPlot axis formatter for memory values in KB
inline int format_memory_kb(const double value, char *buff, const int size,
                            void * /*user_data*/) {
  if (value >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f GB", value / (1024.0 * 1024.0));
  }
  if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f MB", value / 1024.0);
  }
  return snprintf(buff, size, "%.0f KB", value);
}

// Format memory value in bytes to human-readable string
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

// ImPlot axis formatter for percentage values
inline int format_percent(const double value, char *buff, const int size,
                          void * /*user_data*/) {
  return snprintf(buff, size, "%.0f%%", value);
}

// ImPlot axis formatter for I/O rate in KB/s with dynamic units
inline int format_io_rate_kb(const double value, char *buff, const int size,
                             void * /*user_data*/) {
  if (value >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f GB/s", value / (1024.0 * 1024.0));
  }
  if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f MB/s", value / 1024.0);
  }
  if (value >= 1.0) {
    return snprintf(buff, size, "%.1f KB/s", value);
  }
  return snprintf(buff, size, "%.0f B/s", value * 1024.0);
}

// ImPlot axis formatter for I/O rate in MB/s with dynamic units
inline int format_io_rate_mb(const double value, char *buff, const int size,
                             void * /*user_data*/) {
  if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f GB/s", value / 1024.0);
  }
  if (value >= 1.0) {
    return snprintf(buff, size, "%.1f MB/s", value);
  }
  if (value >= 1.0 / 1024.0) {
    return snprintf(buff, size, "%.1f KB/s", value * 1024.0);
  }
  return snprintf(buff, size, "%.0f B/s", value * 1024.0 * 1024.0);
}

template <class T> void common_views_sort_added(GrowingArray<T> &views) {
  // FIXME: performance (no need to resort sorted part)
  std::sort(
      views.data(), views.data() + views.size(),
      [](const auto &left, const auto &right) { return left.pid < right.pid; });
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

// Standard table flags used by most viewer tables
constexpr ImGuiTableFlags COMMON_TABLE_FLAGS =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
    ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
    ImGuiTableFlags_ScrollY;

// Draw a filter input with Ctrl+F keyboard shortcut
inline ImGuiTextFilter draw_filter_input(const char *id, char *filter_text,
                                         size_t filter_text_size) {
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::InputTextWithHint(id, "Filter", filter_text, filter_text_size);
  ImGuiTextFilter filter;
  if (filter_text[0] != '\0') {
    strncpy(filter.InputBuf, filter_text, sizeof(filter.InputBuf));
    filter.InputBuf[sizeof(filter.InputBuf) - 1] = '\0';
    filter.Build();
  }
  return filter;
}

// Handle table sort specs, calling sort_fn if sorting changed
template <typename ColumnId, typename SortFn>
bool handle_table_sort_specs(ColumnId &sorted_by, ImGuiSortDirection &sorted_order,
                             SortFn sort_fn) {
  if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
    if (sort_specs->SpecsDirty) {
      sorted_by = static_cast<ColumnId>(sort_specs->Specs->ColumnUserID);
      sorted_order = sort_specs->Specs->SortDirection;
      sort_fn();
      sort_specs->SpecsDirty = false;
      return true;
    }
  }
  return false;
}
