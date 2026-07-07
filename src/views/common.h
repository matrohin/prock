#pragma once

#include "constants.h"
#include "cpu_chart.h"
#include "icons.h"
#include "notifications.h"
#include "style_control.h"

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

// Stretch weight for the trailing spacer column in viewer header toolbars.
// Small relative to the filter column (default weight 1.0) so the spacer is a
// thin gap at the right edge that keeps the controls left-aligned.
constexpr float HEADER_SPACER_WEIGHT = 0.25f;

// Standard table flags used by most viewer tables
constexpr ImGuiTableFlags COMMON_TABLE_FLAGS =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
    ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_HighlightHoveredColumn;

// Data tables render in the monospaced font: push right after a successful
// BeginTable, pop right before the matching EndTable. Sized off FontSizeBase
// (zoom composes on top) with the optical correction factor applied.
inline void push_mono_font() {
  ImGui::PushFont(style_control_mono_font(),
                  ImGui::GetStyle().FontSizeBase * MONO_FONT_SIZE_FACTOR);
}
inline void pop_mono_font() { ImGui::PopFont(); }

// Fixed pixel sizes are authored at the base font size; scale them by the
// live zoom/DPI so they track the text instead of staying frozen when the UI
// is zoomed or on a HiDPI monitor. Call with the default UI font active.
inline float ui_scale() { return ImGui::GetFontSize() / BASE_FONT_SIZE; }

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

inline void draw_filter_input(ImGuiTextFilter &filter, const char *id,
                              char *filter_text, const size_t filter_text_size,
                              const char *hint = "Filter") {
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

  // Suppress the keyboard-nav focus ring: SetKeyboardFocusHere() leaves its
  // visibility to leftover nav state, so the border would appear erratically
  ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
  ImGui::InputTextWithHint(id, hint, filter_text, filter_text_size);
  ImGui::PopStyleColor();

  if (filter_text[0] != '\0') {
    strncpy(filter.InputBuf, filter_text, sizeof(filter.InputBuf));
    filter.InputBuf[sizeof(filter.InputBuf) - 1] = '\0';
    filter.Build();
  }
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
