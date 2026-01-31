#include "imgui.h"
#include "state.h"
#include "views/brief_table.h"

#include <algorithm>
#include <cstring>

// How long to keep dead processes visible (in nanoseconds)
static constexpr int64_t DEAD_PROCESS_DISPLAY_NS = 2'000'000'000; // 2 seconds

size_t binary_search_pid(const Array<ProcessStat> &stats, const int pid) {
  return bin_search_exact(
      stats.size, [&stats](const size_t mid) { return stats.data[mid].pid; },
      pid);
}

static bool table_line_is_less(const BriefTableColumnId sorted_by,
                               const BriefTableLine &left,
                               const BriefTableLine &right) {
  switch (sorted_by) {
  case eBriefTableColumnId_Pid:
    return left.pid < right.pid;
  case eBriefTableColumnId_Name:
    return strcmp(left.comm, right.comm) < 0;
  case eBriefTableColumnId_State:
    return left.state < right.state;
  case eBriefTableColumnId_Threads:
    return left.num_threads < right.num_threads;
  case eBriefTableColumnId_CpuTotalPerc: {
    const double left_val =
        left.derived_stat.cpu_user_perc + left.derived_stat.cpu_kernel_perc;
    const double right_val =
        right.derived_stat.cpu_user_perc + right.derived_stat.cpu_kernel_perc;
    return left_val < right_val;
  }
  case eBriefTableColumnId_CpuUserPerc:
    return left.derived_stat.cpu_user_perc < right.derived_stat.cpu_user_perc;
  case eBriefTableColumnId_CpuKernelPerc:
    return left.derived_stat.cpu_kernel_perc <
           right.derived_stat.cpu_kernel_perc;
  case eBriefTableColumnId_MemRssBytes:
    return left.derived_stat.mem_resident_bytes <
           right.derived_stat.mem_resident_bytes;
  case eBriefTableColumnId_MemVirtBytes:
    return left.derived_stat.mem_virtual_bytes <
           right.derived_stat.mem_virtual_bytes;
  case eBriefTableColumnId_IoReadKbPerSec:
    return left.derived_stat.io_read_kb_per_sec <
           right.derived_stat.io_read_kb_per_sec;
  case eBriefTableColumnId_IoWriteKbPerSec:
    return left.derived_stat.io_write_kb_per_sec <
           right.derived_stat.io_write_kb_per_sec;
  case eBriefTableColumnId_NetRecvKbPerSec:
    return left.derived_stat.net_recv_kb_per_sec <
           right.derived_stat.net_recv_kb_per_sec;
  case eBriefTableColumnId_NetSendKbPerSec:
    return left.derived_stat.net_send_kb_per_sec <
           right.derived_stat.net_send_kb_per_sec;
  case eBriefTableColumnId_Count:
    return false;
  }
  return false;
}

static void sort_flat(BriefTableState &my_state) {
  const auto sort_ascending =
      [sorted_by = my_state.sorted_by](const BriefTableLine &left,
                                       const BriefTableLine &right) {
        return table_line_is_less(sorted_by, left, right);
      };

  if (my_state.sorted_order != ImGuiSortDirection_Descending) {
    std::stable_sort(my_state.lines.data,
                     my_state.lines.data + my_state.lines.size, sort_ascending);
  } else {
    std::stable_sort(my_state.lines.data,
                     my_state.lines.data + my_state.lines.size,
                     [&](const auto &left, const auto &right) {
                       return sort_ascending(right, left);
                     });
  }

  // Reset tree depth for flat mode
  for (size_t i = 0; i < my_state.lines.size; ++i) {
    my_state.lines.data[i].tree_depth = 0;
  }
}

// Recursively add a process and its children to the output array (DFS)
// Assumes lines are already sorted by PID
static void add_tree_node(const Array<BriefTableLine> &src,
                          BriefTableLine *dst, size_t &dst_idx, size_t src_idx,
                          int depth) {
  dst[dst_idx] = src.data[src_idx];
  dst[dst_idx].tree_depth = depth;
  ++dst_idx;

  const int parent_pid = src.data[src_idx].pid;

  // Find and add all children (already sorted by PID since src is sorted)
  for (size_t i = 0; i < src.size; ++i) {
    if (src.data[i].ppid == parent_pid && src.data[i].pid != parent_pid) {
      add_tree_node(src, dst, dst_idx, i, depth + 1);
    }
  }
}

void sort_brief_table_tree(BriefTableState &my_state, BumpArena &arena) {
  Array<BriefTableLine> &lines = my_state.lines;
  if (lines.size == 0) return;

  // Sort by PID first for consistent tree ordering
  std::sort(lines.data, lines.data + lines.size,
            [](const BriefTableLine &a, const BriefTableLine &b) {
              return a.pid < b.pid;
            });

  // Allocate output array
  BriefTableLine *sorted = arena.alloc_array_of<BriefTableLine>(lines.size);
  size_t sorted_idx = 0;

  // Find root processes (ppid not in our PID list) and add with DFS
  for (size_t i = 0; i < lines.size; ++i) {
    const int ppid = lines.data[i].ppid;
    // Root if ppid is 0, or if ppid is not found in our process list
    bool is_root = (ppid == 0);
    if (!is_root) {
      is_root = true;
      for (size_t j = 0; j < lines.size; ++j) {
        if (lines.data[j].pid == ppid) {
          is_root = false;
          break;
        }
      }
    }
    if (is_root) {
      add_tree_node(lines, sorted, sorted_idx, i, 0);
    }
  }

  // Copy back to lines array
  for (size_t i = 0; i < sorted_idx; ++i) {
    my_state.lines.data[i] = sorted[i];
  }
  my_state.lines.size = sorted_idx;
}

void sort_brief_table_lines(BriefTableState &my_state) {
  sort_flat(my_state);
}

static void brief_table_line_init(BriefTableLine &new_line,
                                  const ProcessStat &stat,
                                  const ProcessDerivedStat &derived_stat) {
  new_line.pid = stat.pid;
  new_line.ppid = stat.ppid;
  new_line.comm = stat.comm;
  new_line.state = stat.state;
  new_line.num_threads = stat.num_threads;

  new_line.derived_stat = derived_stat;
  new_line.filter_state = 0;
}

// Rebuilds lines in previous display order (with new processes appended)
// for stable sorting.
void brief_table_update(BriefTableState &my_state, State &state) {

  const StateSnapshot &new_snapshot = state.snapshot;
  const Array<BriefTableLine> &old_lines = my_state.lines;
  const int64_t now_ns = new_snapshot.at.time_since_epoch().count();

  const Array<bool> added =
      Array<bool>::create(state.snapshot_arena, new_snapshot.stats.size);

  // Allocate enough space for old lines + new processes
  const size_t max_lines = old_lines.size + new_snapshot.stats.size;
  Array<BriefTableLine> new_lines =
      Array<BriefTableLine>::create(state.snapshot_arena, max_lines);
  size_t new_lines_count = 0;

  // Process old lines: keep alive ones, mark dead ones
  for (size_t i = 0; i < old_lines.size; ++i) {
    const BriefTableLine &old_line = old_lines.data[i];

    // Skip processes that have been dead too long
    if (old_line.death_time_ns > 0 &&
        now_ns - old_line.death_time_ns > DEAD_PROCESS_DISPLAY_NS) {
      continue;
    }

    const size_t state_index =
        binary_search_pid(new_snapshot.stats, old_line.pid);

    if (state_index != SIZE_MAX) {
      // Process still alive
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      brief_table_line_init(new_line, new_snapshot.stats.data[state_index],
                            new_snapshot.derived_stats.data[state_index]);

      new_line.first_seen_ns = old_line.first_seen_ns;
      new_line.death_time_ns = 0;

      added.data[state_index] = true;
    } else {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      new_line = old_line;
      new_line.comm = state.snapshot_arena.alloc_string_copy(old_line.comm);
      if (old_line.death_time_ns == 0) {
        // Process just died
        new_line.death_time_ns = now_ns;
      }
    }
  }

  // Add new processes
  // On first update (old_lines empty), use 0 to avoid marking all as "new"
  const int64_t new_process_first_seen = old_lines.size > 0 ? now_ns : 0;
  for (size_t i = 0; i < new_snapshot.stats.size; ++i) {
    if (!added.data[i]) {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      brief_table_line_init(new_line, new_snapshot.stats.data[i],
                            new_snapshot.derived_stats.data[i]);
      new_line.first_seen_ns = new_process_first_seen;
    }
  }

  new_lines.size = new_lines_count;
  my_state.lines = new_lines;

  if (my_state.tree_mode) {
    sort_brief_table_tree(my_state, state.snapshot_arena);
  } else {
    sort_brief_table_lines(my_state);
  }
}
