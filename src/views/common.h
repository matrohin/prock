#pragma once

#include <cstdio>

constexpr ImGuiWindowFlags COMMON_VIEW_FLAGS = ImGuiWindowFlags_NoCollapse;

// ImPlot axis formatter for memory values in KB
inline int format_memory_kb(double value, char *buff, int size,
                            void * /*user_data*/) {
  if (value >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f GB", value / (1024.0 * 1024.0));
  } else if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f MB", value / 1024.0);
  } else {
    return snprintf(buff, size, "%.0f KB", value);
  }
}

// Format memory value in bytes to human-readable string
inline int format_memory_bytes(double bytes, char *buff, int size) {
  if (bytes >= 1024.0 * 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f G", bytes / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f M", bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024.0) {
    return snprintf(buff, size, "%.0f K", bytes / 1024.0);
  } else {
    return snprintf(buff, size, "%.0f B", bytes);
  }
}

// ImPlot axis formatter for percentage values
inline int format_percent(double value, char *buff, int size,
                          void * /*user_data*/) {
  return snprintf(buff, size, "%.0f%%", value);
}

// ImPlot axis formatter for I/O rate in KB/s with dynamic units
inline int format_io_rate_kb(double value, char *buff, int size,
                             void * /*user_data*/) {
  if (value >= 1024.0 * 1024.0) {
    return snprintf(buff, size, "%.1f GB/s", value / (1024.0 * 1024.0));
  } else if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f MB/s", value / 1024.0);
  } else if (value >= 1.0) {
    return snprintf(buff, size, "%.1f KB/s", value);
  } else {
    return snprintf(buff, size, "%.0f B/s", value * 1024.0);
  }
}

// ImPlot axis formatter for I/O rate in MB/s with dynamic units
inline int format_io_rate_mb(double value, char *buff, int size,
                             void * /*user_data*/) {
  if (value >= 1024.0) {
    return snprintf(buff, size, "%.1f GB/s", value / 1024.0);
  } else if (value >= 1.0) {
    return snprintf(buff, size, "%.1f MB/s", value);
  } else if (value >= 1.0 / 1024.0) {
    return snprintf(buff, size, "%.1f KB/s", value * 1024.0);
  } else {
    return snprintf(buff, size, "%.0f B/s", value * 1024.0 * 1024.0);
  }
}
