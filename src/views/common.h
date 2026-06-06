#pragma once

#include "cpu_chart.h"
#include "icons.h"
#include "notifications.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <unistd.h>

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
      views.begin(), views.end(),
      [](const auto &left, const auto &right) { return left.pid < right.pid; });
}

// Standard table flags used by most viewer tables
constexpr ImGuiTableFlags COMMON_TABLE_FLAGS =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
    ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_HighlightHoveredColumn;

// Draw a filter input with Ctrl+F keyboard shortcut
// Refresh toolbar button (icon + label). When `pending`, shows a disabled
// "Refreshing..." state and returns false (smaps' in-flight reload state).
inline bool draw_refresh_button(const bool pending = false) {
  if (pending) {
    ImGui::BeginDisabled();
    ImGui::Button(ICON_MD_REFRESH " Refreshing...");
    ImGui::EndDisabled();
    return false;
  }
  return ImGui::Button(ICON_MD_REFRESH " Refresh");
}

// Draw a muted "Updated Xs ago" on the current toolbar line. `last_updated` is
// an ImGui::GetTime() timestamp captured when data last arrived; 0 means never.
inline void draw_last_updated(const double last_updated) {
  if (last_updated <= 0.0) return;
  const double secs = ImGui::GetTime() - last_updated;
  ImGui::SameLine();
  if (secs < 1.0) {
    ImGui::TextDisabled("Updated just now");
  } else if (secs < 60.0) {
    ImGui::TextDisabled("Updated %.0fs ago", secs);
  } else if (secs < 3600.0) {
    ImGui::TextDisabled("Updated %.0fm ago", secs / 60.0);
  } else {
    ImGui::TextDisabled("Updated %.0fh ago", secs / 3600.0);
  }
}

inline ImGuiTextFilter draw_filter_input(const char *id, char *filter_text,
                                         const size_t filter_text_size) {
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
    ImGui::SetKeyboardFocusHere();
  }

  // Get widget ID to check if it's active and to reload its buffer after
  // modification
  const ImGuiID input_id = ImGui::GetID(id);
  const bool is_active = ImGui::GetActiveID() == input_id;
  bool buffer_modified = false;

  // Handle shortcuts when filter input is active (before drawing)
  if (is_active) {
    // Ctrl+W: delete last filter entry (word)
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W,
                        ImGuiInputFlags_RouteAlways)) {
      uint32_t len = static_cast<uint32_t>(strlen(filter_text));
      if (len > 0) {
        while (len > 0 && filter_text[len - 1] == ',') {
          len--;
        }
        while (len > 0 && filter_text[len - 1] != ',') {
          len--;
        }
        filter_text[len] = '\0';
        buffer_modified = true;
      }
    }

    // Tell ImGui to reload from user buffer if we modified it
    if (buffer_modified) {
      if (ImGuiInputTextState *state = ImGui::GetInputTextState(input_id)) {
        state->ReloadUserBufAndMoveToEnd();
      }
    }
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

// Push an error notification; on permission errors it offers a pkexec restart.
inline void notify_error(Notifications &notifications, const int error_code,
                         const char *fmt, ...) {
  const bool can_escalate = is_permission_error(error_code);
  va_list args;
  va_start(args, fmt);
  notifications_vpush_action(notifications, eNotificationSeverity_Error,
                             can_escalate ? "Restart with pkexec" : nullptr,
                             can_escalate ? restart_with_pkexec : nullptr, fmt,
                             args);
  va_end(args);
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
inline double scale_cpu_perc(double value, int num_cores, bool per_core) {
  if (per_core || num_cores <= 0) return value;
  return value / num_cores;
}

template <typename T, typename Compare>
void sort_bidirectional(T *data, uint32_t size, ImGuiSortDirection dir,
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
void copy_all_to_clipboard(BumpArena &arena, const T *data, uint32_t count,
                           size_t per_item_estimate, const char *header,
                           FormatFn fmt) {
  const size_t buf_size = 128 + count * per_item_estimate;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", header);
  for (uint32_t i = 0; i < count; ++i) {
    ptr += fmt(ptr, buf_size - static_cast<size_t>(ptr - buf), data[i]);
  }
  ImGui::SetClipboardText(buf);
}
