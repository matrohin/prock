#pragma once

#include "base/containers.h"
#include "cpu_chart.h"
#include "icons.h"
#include "notifications.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <unistd.h>

constexpr ImGuiWindowFlags COMMON_VIEW_FLAGS = ImGuiWindowFlags_NoCollapse;
constexpr ImGuiTableFlags COMMON_TABLE_FLAGS =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
    ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_HighlightHoveredColumn;

// ImPlot axis formatter for memory values in KB
inline int common_format_memory_kb(const double value, char *buff,
                                   const int size, void * /*user_data*/) {
  if (value >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f GB", value / (1024.0 * 1024.0));
  }
  if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f MB", value / 1024.0);
  }
  return snprintf(buff, size, "%.0f KB", value);
}

// ImPlot axis formatter for percentage values
inline int common_format_percent(const double value, char *buff, const int size,
                                 void * /*user_data*/) {
  return snprintf(buff, size, "%.0f%%", value);
}

// ImPlot axis formatter for I/O rate in KB/s with dynamic units
inline int common_format_io_rate_kb(const double value, char *buff,
                                    const int size, void * /*user_data*/) {
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
inline int common_format_io_rate_mb(const double value, char *buff,
                                    const int size, void * /*user_data*/) {
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
      views.begin(), views.end(),
      [](const auto &left, const auto &right) { return left.pid < right.pid; });
}

// Handle table sort specs, calling sort_fn if sorting changed
template <typename ColumnId, typename SortFn>
bool handle_table_sort_specs(ColumnId &sorted_by,
                             ImGuiSortDirection &sorted_order, SortFn sort_fn) {
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

// Restart the application with elevated privileges via pkexec
inline void restart_with_pkexec() {
  char exe_path[PATH_MAX];
  const ssize_t len =
      readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len > 0) {
    exe_path[len] = '\0';
    // Preserve display environment variables that pkexec strips
    const char *env_vars[] = {"DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                              "XAUTHORITY"};
    char env_args[5][512];
    const char *args[9] = {"pkexec", "env"};
    int arg_idx = 2;
    for (int i = 0; i < 4; ++i) {
      const char *val = getenv(env_vars[i]);
      if (val) {
        snprintf(env_args[i], sizeof(env_args[i]), "%s=%s", env_vars[i], val);
        args[arg_idx++] = env_args[i];
      }
    }
    // Pass config dir so elevated process uses same settings
    const char *home = getenv("HOME");
    if (home) {
      snprintf(env_args[4], sizeof(env_args[4]),
               "PROCK_CONFIG_DIR=%s/.config/prock", home);
      args[arg_idx++] = env_args[4];
    }
    args[arg_idx++] = exe_path;
    args[arg_idx] = nullptr;
    execvp("pkexec", const_cast<char *const *>(args));
  }
}

// True for errors a pkexec / privilege escalation could resolve.
inline bool is_permission_error(const int error_code) {
  return error_code == EACCES || error_code == EPERM;
}

// Draw error message with optional pkexec restart button for permission errors
inline void draw_error_with_pkexec(const int error_code) {
  char error_message[128];
  snprintf(error_message, sizeof(error_message), "Error: %s",
           strerror(error_code));
  ImGui::Text("%s", error_message);
  if (is_permission_error(error_code)) {
    if (ImGui::Button(ICON_MD_SHIELD " Restart with pkexec")) {
      restart_with_pkexec();
    }
  }
}

// Draw an error for a failed netlink sock_diag socket query. ENOENT means the
// kernel has no inet_diag handler (module missing, unloadable, or not built).
inline void draw_socket_query_error(const int error_code) {
  ImGui::Text("Error: socket query failed: %s", strerror(error_code));
  if (error_code == ENOENT) {
    ImGui::TextDisabled("The kernel's inet_diag module is unavailable.");
  }
}

// Push an error notification; on permission errors it offers a pkexec restart.
inline void notify_error(Notifications &notifications, const int error_code,
                         const char *fmt, ...) {
  const bool can_escalate = is_permission_error(error_code);
  va_list args;
  va_start(args, fmt);
  notifications_vpush_action(
      notifications, eNotificationSeverity_Error,
      can_escalate ? "Restart with pkexec" : nullptr,
      can_escalate ? +[](const void *) { restart_with_pkexec(); } : nullptr,
      nullptr, fmt, args);
  va_end(args);
}

inline void notify_info(Notifications &notifications, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  notifications_vpush_action(notifications, eNotificationSeverity_Info, nullptr,
                             nullptr, nullptr, fmt, args);
  va_end(args);
}

// Info notification with a clickable action button. action_fn(action_data) runs
// on click; action_data must outlive the toast (e.g. allocated in the
// notifications arena, which lives as long as any notification is shown).
inline void notify_info_action(Notifications &notifications,
                               const char *action_label,
                               void (*action_fn)(const void *user_data),
                               const void *action_data, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  notifications_vpush_action(notifications, eNotificationSeverity_Info,
                             action_label, action_fn, action_data, fmt, args);
  va_end(args);
}

// Clipboard helpers that confirm the copy with a toast. Cell copies preview
// the value; rows are too long to preview, so they just say what happened.
inline void clipboard_copy_cell(Notifications &notifications,
                                const String &text) {
  ImGui::SetClipboardText(text.data);
  constexpr uint32_t MAX_PREVIEW = 64;
  if (text.len <= MAX_PREVIEW) {
    notify_info(notifications, "Copied: %s", text.data);
  } else {
    notify_info(notifications, "Copied: %.*s...", MAX_PREVIEW, text.data);
  }
}

inline void clipboard_copy_row(Notifications &notifications, const char *text) {
  ImGui::SetClipboardText(text);
  notify_info(notifications, "Copied row");
}

// Ctrl+C "copy selected row" gate for the viewer tables. Confirms the stored
// selection is still in range before the caller indexes data[selected_index]:
// a Refresh can replace the list with fewer rows and leave selected_index
// pointing past the end.
inline bool copy_row_shortcut(const int selected_index, const uint32_t size) {
  return selected_index >= 0 && selected_index < static_cast<int>(size) &&
         ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C);
}

// Remembers which column was under the mouse on the right-click that opened a
// row context menu, so "Copy <cell>" knows what to copy while the popup is up.
// Call right after the row's spanning Selectable/TreeNode (it must be the last
// item). Only one context menu can be open at a time, so a single slot serves
// every table. The trigger must mirror OpenPopupOnItemClick's (mouse release +
// AllowWhenBlockedByPopup): a press-based plain-hover check misses the
// recapture when right-clicking while the previous popup is still open.
inline int table_context_column(const int column_count) {
  static int captured = 0;
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
    const int hovered = ImGui::TableGetHoveredColumn();
    captured = hovered >= 0 && hovered < column_count ? hovered : 0;
  }
  return captured;
}

// Menu label like: Copy "esbuild". Long values are elided; the ### suffix
// keeps the item ID stable regardless of the value.
inline String copy_cell_menu_label(BumpArena &arena, const String &cell) {
  constexpr uint32_t MAX_LABEL = 24;
  if (cell.len <= MAX_LABEL) {
    return String::sprintf(arena, "Copy \"%s\"###CopyCell", cell.data);
  }
  return String::sprintf(arena, "Copy \"%.*s...\"###CopyCell", MAX_LABEL,
                         cell.data);
}

inline bool popup_close_on_escape() {
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
    return true;
  }
  return false;
}

// Returns CPU percentage scaled for display:
// - per_core=true: raw value (can exceed 100% on multi-core)
// - per_core=false: normalized to 0-100% range
inline double scale_cpu_perc(const double value, const int num_cores,
                             const bool per_core) {
  if (per_core || num_cores <= 0) return value;
  return value / num_cores;
}

template <typename T, typename Compare>
void sort_bidirectional(T *data, uint32_t size, const ImGuiSortDirection dir,
                        Compare cmp) {
  if (size == 0) return;
  if (dir == ImGuiSortDirection_Ascending) {
    std::stable_sort(data, data + size, cmp);
  } else {
    std::stable_sort(data, data + size,
                     [&](const T &a, const T &b) { return cmp(b, a); });
  }
}

template <typename T, typename FormatFn>
void copy_all_to_clipboard(Notifications &notifications, BumpArena &arena,
                           const T *data, const uint32_t count,
                           const size_t per_item_estimate, const char *header,
                           FormatFn fmt) {
  const size_t buf_size = 128 + count * per_item_estimate;
  char *buf = arena.alloc_string(buf_size);
  size_t used = 0;

  // snprintf returns the length it WOULD have written; clamp every advance to
  // the bytes actually stored so one oversized row (e.g. a near-PATH_MAX path
  // against a small per_item_estimate) can't push `used` past the buffer and
  // underflow the remaining-size passed to the next call.
  const auto advance = [&](const int written) {
    if (written <= 0) return;
    const size_t remaining = buf_size - used; // includes null terminator slot
    used += static_cast<size_t>(written) < remaining
                ? static_cast<size_t>(written)
                : remaining - 1;
  };

  advance(snprintf(buf, buf_size, "%s", header));
  for (uint32_t i = 0; i < count && used + 1 < buf_size; ++i) {
    advance(fmt(buf + used, buf_size - used, data[i]));
  }
  ImGui::SetClipboardText(buf);
  notify_info(notifications, "Copied %u rows", count);
}