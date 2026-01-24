#include "imgui.h"
#include "state.h"
#include "views/brief_table.h"

#include <algorithm>
#include <cstring>

size_t binary_search_pid(const Array<ProcessStat> &stats, const int pid) {
  return bin_search_exact(
      stats.size, [&stats](const size_t mid) { return stats.data[mid].pid; },
      pid);
}

// Sort siblings linked list using provided comparison
static BriefTreeNode *sort_siblings(BumpArena &arena, BriefTreeNode *head,
                                    const StateSnapshot &snapshot,
                                    const BriefTableColumnId sorted_by,
                                    const ImGuiSortDirection sorted_order) {
  if (!head || !head->next_sibling) return head;

  // Count siblings
  size_t count = 0;
  for (BriefTreeNode *n = head; n; n = n->next_sibling)
    count++;

  // Copy to array for sorting
  BriefTreeNode **arr = arena.alloc_array_of<BriefTreeNode *>(count);
  size_t i = 0;
  for (BriefTreeNode *n = head; n; n = n->next_sibling)
    arr[i++] = n;

  // Sort using same logic as flat mode
  auto compare = [&](const BriefTreeNode *left, const BriefTreeNode *right) {
    const ProcessStat &ls = snapshot.stats.data[left->state_index];
    const ProcessStat &rs = snapshot.stats.data[right->state_index];
    const ProcessDerivedStat &ld =
        snapshot.derived_stats.data[left->state_index];
    const ProcessDerivedStat &rd =
        snapshot.derived_stats.data[right->state_index];

    bool ascending;
    switch (sorted_by) {
    case eBriefTableColumnId_Pid:
      ascending = left->pid < right->pid;
      break;
    case eBriefTableColumnId_Name:
      ascending = strcmp(ls.comm, rs.comm) < 0;
      break;
    case eBriefTableColumnId_State:
      ascending = ls.state < rs.state;
      break;
    case eBriefTableColumnId_Threads:
      ascending = ls.num_threads < rs.num_threads;
      break;
    case eBriefTableColumnId_CpuTotalPerc:
      ascending = (ld.cpu_user_perc + ld.cpu_kernel_perc) <
                  (rd.cpu_user_perc + rd.cpu_kernel_perc);
      break;
    case eBriefTableColumnId_CpuUserPerc:
      ascending = ld.cpu_user_perc < rd.cpu_user_perc;
      break;
    case eBriefTableColumnId_CpuKernelPerc:
      ascending = ld.cpu_kernel_perc < rd.cpu_kernel_perc;
      break;
    case eBriefTableColumnId_MemRssBytes:
      ascending = ld.mem_resident_bytes < rd.mem_resident_bytes;
      break;
    case eBriefTableColumnId_MemVirtBytes:
      ascending = ls.vsize < rs.vsize;
      break;
    case eBriefTableColumnId_IoReadKbPerSec:
      ascending = ld.io_read_kb_per_sec < rd.io_read_kb_per_sec;
      break;
    case eBriefTableColumnId_IoWriteKbPerSec:
      ascending = ld.io_write_kb_per_sec < rd.io_write_kb_per_sec;
      break;
    default:
      ascending = false;
      break;
    }
    return (sorted_order == ImGuiSortDirection_Descending) ? !ascending
                                                           : ascending;
  };

  std::stable_sort(arr, arr + count, compare);

  // Rebuild linked list
  for (size_t j = 0; j + 1 < count; ++j) {
    arr[j]->next_sibling = arr[j + 1];
  }
  arr[count - 1]->next_sibling = nullptr;

  return arr[0];
}

void sort_brief_table_lines(BriefTableState &my_state,
                            const StateSnapshot &state) {
  const auto sort_ascending = [&state, sorted_by = my_state.sorted_by](
                                  const BriefTableLine &left,
                                  const BriefTableLine &right) {
    switch (sorted_by) {
    case eBriefTableColumnId_Pid:
      return left.pid < right.pid;
    case eBriefTableColumnId_Name:
      return strcmp(state.stats.data[left.state_index].comm,
                    state.stats.data[right.state_index].comm) < 0;
    case eBriefTableColumnId_State:
      return state.stats.data[left.state_index].state <
             state.stats.data[right.state_index].state;
    case eBriefTableColumnId_Threads:
      return state.stats.data[left.state_index].num_threads <
             state.stats.data[right.state_index].num_threads;
    case eBriefTableColumnId_CpuTotalPerc: {
      const double left_val =
          state.derived_stats.data[left.state_index].cpu_user_perc +
          state.derived_stats.data[left.state_index].cpu_kernel_perc;
      const double right_val =
          state.derived_stats.data[right.state_index].cpu_user_perc +
          state.derived_stats.data[right.state_index].cpu_kernel_perc;
      return left_val < right_val;
    }
    case eBriefTableColumnId_CpuUserPerc:
      return state.derived_stats.data[left.state_index].cpu_user_perc <
             state.derived_stats.data[right.state_index].cpu_user_perc;
    case eBriefTableColumnId_CpuKernelPerc:
      return state.derived_stats.data[left.state_index].cpu_kernel_perc <
             state.derived_stats.data[right.state_index].cpu_kernel_perc;
    case eBriefTableColumnId_MemRssBytes:
      return state.derived_stats.data[left.state_index].mem_resident_bytes <
             state.derived_stats.data[right.state_index].mem_resident_bytes;
    case eBriefTableColumnId_MemVirtBytes:
      return state.stats.data[left.state_index].vsize <
             state.stats.data[right.state_index].vsize;
    case eBriefTableColumnId_IoReadKbPerSec:
      return state.derived_stats.data[left.state_index].io_read_kb_per_sec <
             state.derived_stats.data[right.state_index].io_read_kb_per_sec;
    case eBriefTableColumnId_IoWriteKbPerSec:
      return state.derived_stats.data[left.state_index].io_write_kb_per_sec <
             state.derived_stats.data[right.state_index].io_write_kb_per_sec;
    case eBriefTableColumnId_NetRecvKbPerSec:
      return state.derived_stats.data[left.state_index].net_recv_kb_per_sec <
             state.derived_stats.data[right.state_index].net_recv_kb_per_sec;
    case eBriefTableColumnId_NetSendKbPerSec:
      return state.derived_stats.data[left.state_index].net_send_kb_per_sec <
             state.derived_stats.data[right.state_index].net_send_kb_per_sec;
    case eBriefTableColumnId_Count:
      return false;
    }
    return false;
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

// Rebuilds lines in previous display order (with new processes appended) for
// stable sorting.
void brief_table_update(BriefTableState &my_state, State &state) {

  const StateSnapshot &new_snapshot = state.snapshot;
  const Array<BriefTableLine> &old_lines = my_state.lines;

  const Array<bool> added =
      Array<bool>::create(state.snapshot_arena, new_snapshot.stats.size);
  for (size_t i = 0; i < added.size; ++i) {
    added.data[i] = false;
  }

  Array<BriefTableLine> new_lines = Array<BriefTableLine>::create(
      state.snapshot_arena, new_snapshot.stats.size);
  size_t new_lines_count = 0;

  for (size_t i = 0; i < old_lines.size; ++i) {
    const BriefTableLine &old_line = old_lines.data[i];
    const size_t state_index = binary_search_pid(new_snapshot.stats, old_line.pid);

    if (state_index != SIZE_MAX) {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      new_line.pid = old_line.pid;
      new_line.state_index = state_index;
      added.data[state_index] = true;
    }
  }

  for (size_t i = 0; i < new_snapshot.stats.size; ++i) {
    if (!added.data[i]) {
      BriefTableLine &new_line = new_lines.data[new_lines_count++];
      new_line.pid = new_snapshot.stats.data[i].pid;
      new_line.state_index = i;
    }
  }

  new_lines.size = new_lines_count;
  my_state.lines = new_lines;
  sort_brief_table_lines(my_state, new_snapshot);
}

BriefTreeNode *build_process_tree(BumpArena &arena,
                                  const Array<BriefTableLine> &lines,
                                  const StateSnapshot &snapshot,
                                  const BriefTableColumnId sorted_by,
                                  const ImGuiSortDirection sorted_order) {
  if (lines.size == 0) return nullptr;

  // Create nodes for all processes
  BriefTreeNode *nodes = arena.alloc_array_of<BriefTreeNode>(lines.size);

  // Map from pid to node index (use snapshot stats for binary search)
  for (size_t i = 0; i < lines.size; ++i) {
    nodes[i].pid = lines.data[i].pid;
    nodes[i].state_index = lines.data[i].state_index;
    nodes[i].first_child = nullptr;
    nodes[i].next_sibling = nullptr;
  }

  BriefTreeNode *roots = nullptr;

  // Build tree by linking children to parents
  for (size_t i = 0; i < lines.size; ++i) {
    // NOLINTNEXTLINE
    const ProcessStat &stat = snapshot.stats.data[lines.data[i].state_index];
    int ppid = stat.ppid;

    // Find parent node
    BriefTreeNode *parent = nullptr;
    if (ppid > 0 && ppid != nodes[i].pid) {
      // Search for parent in our nodes
      for (size_t j = 0; j < lines.size; ++j) {
        if (nodes[j].pid == ppid) {
          parent = &nodes[j];
          break;
        }
      }
    }

    if (parent) {
      // Add as child of parent (prepend to list)
      nodes[i].next_sibling = parent->first_child;
      parent->first_child = &nodes[i];
    } else {
      // No parent found, add to roots (prepend to list)
      nodes[i].next_sibling = roots;
      roots = &nodes[i];
    }
  }

  // Sort roots
  roots = sort_siblings(arena, roots, snapshot, sorted_by, sorted_order);

  // Sort children at each level
  for (size_t i = 0; i < lines.size; ++i) {
    if (nodes[i].first_child) {
      nodes[i].first_child = sort_siblings(arena, nodes[i].first_child,
                                           snapshot, sorted_by, sorted_order);
    }
  }

  return roots;
}
