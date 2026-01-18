#include "imgui.h"
#include "views/brief_table.h"
#include "state.h"

#include <algorithm>
#include <cstring>

size_t binary_search_pid(const Array<ProcessStat> &stats, int pid) {
  size_t left = 0;
  size_t right = stats.size;
  while (left < right) {
    size_t mid = (left + right) / 2;
    if (stats.data[mid].pid < pid) {
      left = mid + 1;
    } else if (stats.data[mid].pid > pid) {
      right = mid;
    } else {
      return mid;
    }
  }
  return SIZE_MAX;
}

namespace {

// Sort siblings linked list using provided comparison
BriefTreeNode *sort_siblings(BumpArena &arena, BriefTreeNode *head,
                             const StateSnapshot &snapshot,
                             BriefTableColumnId sorted_by,
                             ImGuiSortDirection sorted_order) {
  if (!head || !head->next_sibling) return head;

  // Count siblings
  size_t count = 0;
  for (BriefTreeNode *n = head; n; n = n->next_sibling) count++;

  // Copy to array for sorting
  BriefTreeNode **arr = (BriefTreeNode **)arena.alloc_raw(count * sizeof(BriefTreeNode *), alignof(BriefTreeNode *));
  size_t i = 0;
  for (BriefTreeNode *n = head; n; n = n->next_sibling) arr[i++] = n;

  // Sort using same logic as flat mode
  auto compare = [&](BriefTreeNode *left, BriefTreeNode *right) {
    const ProcessStat &ls = snapshot.stats.data[left->state_index];
    const ProcessStat &rs = snapshot.stats.data[right->state_index];
    const ProcessDerivedStat &ld = snapshot.derived_stats.data[left->state_index];
    const ProcessDerivedStat &rd = snapshot.derived_stats.data[right->state_index];

    bool ascending;
    switch (sorted_by) {
      case eBriefTableColumnId_Pid: ascending = left->pid < right->pid; break;
      case eBriefTableColumnId_Name: ascending = strcmp(ls.comm, rs.comm) < 0; break;
      case eBriefTableColumnId_State: ascending = ls.state < rs.state; break;
      case eBriefTableColumnId_Threads: ascending = ls.num_threads < rs.num_threads; break;
      case eBriefTableColumnId_CpuTotalPerc: ascending = (ld.cpu_user_perc + ld.cpu_kernel_perc) < (rd.cpu_user_perc + rd.cpu_kernel_perc); break;
      case eBriefTableColumnId_CpuUserPerc: ascending = ld.cpu_user_perc < rd.cpu_user_perc; break;
      case eBriefTableColumnId_CpuKernelPerc: ascending = ld.cpu_kernel_perc < rd.cpu_kernel_perc; break;
      case eBriefTableColumnId_MemRssBytes: ascending = ld.mem_resident_bytes < rd.mem_resident_bytes; break;
      case eBriefTableColumnId_MemVirtBytes: ascending = ls.vsize < rs.vsize; break;
      case eBriefTableColumnId_IoReadKbPerSec: ascending = ld.io_read_kb_per_sec < rd.io_read_kb_per_sec; break;
      case eBriefTableColumnId_IoWriteKbPerSec: ascending = ld.io_write_kb_per_sec < rd.io_write_kb_per_sec; break;
      default: ascending = false; break;
    }
    return (sorted_order == ImGuiSortDirection_Descending) ? !ascending : ascending;
  };

  std::stable_sort(arr, arr + count, compare);

  // Rebuild linked list
  for (size_t j = 0; j + 1 < count; ++j) {
    arr[j]->next_sibling = arr[j + 1];
  }
  arr[count - 1]->next_sibling = nullptr;

  return arr[0];
}

} // unnamed namespace

BriefTreeNode *build_process_tree(BumpArena &arena,
                                  const Array<BriefTableLine> &lines,
                                  const StateSnapshot &snapshot,
                                  BriefTableColumnId sorted_by,
                                  ImGuiSortDirection sorted_order) {
  if (lines.size == 0) return nullptr;

  // Create nodes for all processes
  BriefTreeNode *nodes = (BriefTreeNode *)arena.alloc_raw(
      lines.size * sizeof(BriefTreeNode), alignof(BriefTreeNode));

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
