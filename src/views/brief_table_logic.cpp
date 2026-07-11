#include "base/algorithms.h"
#include "imgui.h"
#include "state.h"
#include "views/brief_table.h"
#include "views/common_charts.h"

#include <algorithm>
#include <cstring>

// How long to keep dead processes visible (in nanoseconds)
static constexpr int64_t DEAD_PROCESS_DISPLAY_NS = 2'000'000'000; // 2 seconds

uint32_t binary_search_pid(const Array<ProcessStat> &stats, const int pid) {
  return bin_search_exact(
      stats.size, [&stats](const uint32_t mid) { return stats.data[mid].pid; },
      pid);
}

static bool table_line_is_less(const BriefTableColumnId sorted_by,
                               const BriefTableLine &left,
                               const BriefTableLine &right) {
  switch (sorted_by) {
  case eBriefTableColumnId_Pid:
    return left.pid < right.pid;
  case eBriefTableColumnId_Name:
    return strcmp(left.name.data, right.name.data) < 0;
  case eBriefTableColumnId_Username:
    return strcmp(left.username.data, right.username.data) < 0;
  case eBriefTableColumnId_State:
    return left.state < right.state;
  case eBriefTableColumnId_Threads:
    return left.num_threads < right.num_threads;
  case eBriefTableColumnId_StartTime:
    return left.start_time_epoch_sec < right.start_time_epoch_sec;
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
  case eBriefTableColumnId_Nice:
    return left.nice < right.nice;
  case eBriefTableColumnId_CmdLine:
    return strcmp(left.cmdline, right.cmdline) < 0;
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
}

struct TreeNode {
  uint32_t first_child;
  uint32_t next_sibling;
};

static void tree_dfs(const Array<BriefTableLine> &lines, const TreeNode *nodes,
                     BriefTableLine *dst, uint32_t &dst_idx,
                     const uint32_t node_idx, const int depth) {
  dst[dst_idx] = lines.data[node_idx - 1];
  dst[dst_idx].tree_depth = depth;
  ++dst_idx;

  for (uint32_t child = nodes[node_idx].first_child; child != 0;
       child = nodes[child].next_sibling) {
    tree_dfs(lines, nodes, dst, dst_idx, child, depth + 1);
  }
}

void sort_brief_table_tree(BriefTableState &my_state, BumpArena &arena) {
  Array<BriefTableLine> &lines = my_state.lines;
  if (lines.size == 0) return;

  // Sort by PID first for consistent tree ordering and binary search
  std::sort(lines.data, lines.data + lines.size,
            [](const BriefTableLine &a, const BriefTableLine &b) {
              return a.pid < b.pid;
            });

  const uint32_t n = lines.size;

  // Index 0 is the sentinel (virtual root). Real processes at indices 1..N.
  TreeNode *nodes = arena.alloc_array_of<TreeNode>(n + 1);

  // Build left-child/right-sibling tree using binary search for parent lookup.
  // Iterate in reverse so that prepending produces ascending PID order.
  for (uint32_t i = n; i-- > 0;) {
    const Pid ppid = lines.data[i].ppid;
    uint32_t parent_node = 0; // sentinel = root
    if (ppid != 0 && lines.data[i].pid != ppid) {
      const uint32_t parent_idx = bin_search_exact(
          n, [&lines](const uint32_t mid) { return lines.data[mid].pid; },
          ppid);
      if (parent_idx != UINT32_MAX) {
        parent_node = parent_idx + 1;
      }
    }
    // Prepend this node as a child of parent_node
    const uint32_t node_idx = i + 1;
    nodes[node_idx].next_sibling = nodes[parent_node].first_child;
    nodes[parent_node].first_child = node_idx;
  }

  // DFS from sentinel's children to produce sorted output
  BriefTableLine *sorted = arena.alloc_array_of<BriefTableLine>(n);
  uint32_t sorted_idx = 0;

  for (uint32_t root = nodes[0].first_child; root != 0;
       root = nodes[root].next_sibling) {
    tree_dfs(lines, nodes, sorted, sorted_idx, root, 0);
  }

  my_state.lines.inplace_copy_from(sorted, sorted_idx);
}

void sort_brief_table_lines(BriefTableState &my_state) { sort_flat(my_state); }

static void brief_table_line_init(BriefTableLine &new_line,
                                  const ProcessStat &stat,
                                  const ProcessDerivedStat &derived_stat,
                                  InternTable &interner,
                                  const SystemInfo &system) {
  new_line.pid = stat.pid;
  new_line.ppid = stat.ppid;
  new_line.cmdline = stat.cmdline ? stat.cmdline : "";
  new_line.name = process_display_name(new_line.cmdline, stat.comm, interner);
  new_line.username = stat.username;
  new_line.state = stat.state;
  new_line.num_threads = stat.num_threads;
  new_line.nice = stat.nice;

  // starttime is in clock ticks since boot; convert to an absolute epoch.
  new_line.start_time_epoch_sec =
      system.boot_time_epoch_sec != 0 && system.ticks_in_second != 0
          ? static_cast<int64_t>(system.boot_time_epoch_sec +
                                 stat.starttime / system.ticks_in_second)
          : 0;

  new_line.derived_stat = derived_stat;
  new_line.filter_state = 0;
}

// Rebuilds lines in previous display order (with new processes appended)
// for stable sorting.
void brief_table_update(BriefTableState &my_state, InternTable &string_interner,
                        State &state) {
  const StateSnapshot &new_snapshot = state.snapshot;
  const Array<BriefTableLine> &old_lines = my_state.lines;
  const int64_t now_ns = new_snapshot.at.time_since_epoch().count();

  const Array<bool> added =
      Array<bool>::create(state.snapshot_arena, new_snapshot.stats.size);

  // Allocate enough space for old lines + new processes
  const uint32_t max_lines = old_lines.size + new_snapshot.stats.size;
  Array<BriefTableLine> new_lines =
      Array<BriefTableLine>::create(state.snapshot_arena, max_lines);
  uint32_t new_lines_count = 0;

  // Process old lines: keep alive ones, mark dead ones
  for (const BriefTableLine &old_line : old_lines) {
    // Skip processes that have been dead too long
    if (old_line.death_time_ns > 0 &&
        now_ns - old_line.death_time_ns > DEAD_PROCESS_DISPLAY_NS) {
      continue;
    }

    const uint32_t state_index =
        binary_search_pid(new_snapshot.stats, old_line.pid);

    if (state_index != UINT32_MAX) {
      // Process still alive
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      brief_table_line_init(new_line, new_snapshot.stats.data[state_index],
                            new_snapshot.derived_stats.data[state_index],
                            string_interner, state.system);

      new_line.first_seen_ns = old_line.first_seen_ns;
      new_line.death_time_ns = 0;

      added.data[state_index] = true;
    } else {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      new_line = old_line;
      new_line.cmdline =
          old_line.cmdline
              ? state.snapshot_arena.alloc_string_copy(old_line.cmdline)
              : "";
      if (old_line.death_time_ns == 0) {
        // Process just died
        new_line.death_time_ns = now_ns;
      }
    }
  }

  // Add new processes
  // On first update (old_lines empty), use 0 to avoid marking all as "new"
  const int64_t new_process_first_seen = old_lines.size > 0 ? now_ns : 0;
  for (uint32_t i = 0; i < new_snapshot.stats.size; ++i) {
    if (!added.data[i]) {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      brief_table_line_init(new_line, new_snapshot.stats.data[i],
                            new_snapshot.derived_stats.data[i], string_interner,
                            state.system);
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
