#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdint>
#include <ctime>

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

inline void table_item_draw_percent(const double value) {
  ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f%%", value);
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

// Compact cell text: time-of-day if the process started today, else date+time.
inline int format_start_time(const int64_t epoch_sec, char *buff,
                             const int size) {
  if (epoch_sec <= 0) return snprintf(buff, size, "-");
  const time_t t = static_cast<time_t>(epoch_sec);
  struct tm tm_start;
  struct tm tm_now;
  const time_t now = time(nullptr);
  localtime_r(&t, &tm_start);
  localtime_r(&now, &tm_now);
  if (tm_start.tm_year == tm_now.tm_year &&
      tm_start.tm_yday == tm_now.tm_yday) {
    return (int)strftime(buff, size, "%H:%M:%S", &tm_start);
  }
  return (int)strftime(buff, size, "%b %d %H:%M", &tm_start);
}

// Full absolute timestamp, e.g. "2026-06-26 14:23:05". Used for copy/paste.
inline int format_start_time_absolute(const int64_t epoch_sec, char *buff,
                                      const int size) {
  if (epoch_sec <= 0) return snprintf(buff, size, "Unknown");
  const time_t t = static_cast<time_t>(epoch_sec);
  struct tm tm_start;
  localtime_r(&t, &tm_start);
  return (int)strftime(buff, size, "%Y-%m-%d %H:%M:%S", &tm_start);
}

// Absolute timestamp plus relative elapsed time, for the on-hover tooltip:
// "2026-06-26 14:23:05 (3h 12m ago)".
inline int format_start_time_full(const int64_t epoch_sec, char *buff,
                                  const int size) {
  if (epoch_sec <= 0) return snprintf(buff, size, "Unknown");
  char ts[32];
  format_start_time_absolute(epoch_sec, ts, sizeof(ts));

  int64_t elapsed = (int64_t)time(nullptr) - epoch_sec;
  if (elapsed < 0) elapsed = 0;
  const int64_t days = elapsed / 86400;
  const int64_t hours = (elapsed % 86400) / 3600;
  const int64_t mins = (elapsed % 3600) / 60;
  const int64_t secs = elapsed % 60;
  char ago[32];
  if (days > 0) {
    snprintf(ago, sizeof(ago), "%lldd %lldh", (long long)days, (long long)hours);
  } else if (hours > 0) {
    snprintf(ago, sizeof(ago), "%lldh %lldm", (long long)hours, (long long)mins);
  } else if (mins > 0) {
    snprintf(ago, sizeof(ago), "%lldm %llds", (long long)mins, (long long)secs);
  } else {
    snprintf(ago, sizeof(ago), "%llds", (long long)secs);
  }
  return snprintf(buff, size, "%s (%s ago)", ts, ago);
}

inline void table_item_draw_start_time(const int64_t epoch_sec) {
  char buf[32];
  format_start_time(epoch_sec, buf, sizeof(buf));
  ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", buf);
  if (epoch_sec > 0 && ImGui::IsItemHovered()) {
    char full[64];
    format_start_time_full(epoch_sec, full, sizeof(full));
    ImGui::SetTooltip("%s", full);
  }
}
