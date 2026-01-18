#include "brief_table.h"

#include "views/common.h"
#include "views/cpu_chart.h"
#include "views/mem_chart.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/view_state.h"

#include "imgui_internal.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <signal.h>

namespace {

const char *PROCESS_COPY_HEADER = "PID\tName\tState\tThreads\tCPU Total\tCPU User\tCPU Kernel\tRSS (KB)\tVirt (KB)\tI/O Read (KB/s)\tI/O Write (KB/s)\n";

void copy_process_row(const ProcessStat &stat, const ProcessDerivedStat &derived) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f",
           PROCESS_COPY_HEADER,
           stat.pid, stat.comm, stat.state, stat.num_threads,
           derived.cpu_user_perc + derived.cpu_kernel_perc,
           derived.cpu_user_perc, derived.cpu_kernel_perc,
           derived.mem_resident_bytes / 1024.0, stat.vsize / 1024.0,
           derived.io_read_kb_per_sec, derived.io_write_kb_per_sec);
  ImGui::SetClipboardText(buf);
}

void copy_all_processes(BumpArena &arena, const BriefTableState &my_state, const StateSnapshot &snapshot) {
  // Header + all rows
  size_t buf_size = 256 + my_state.lines.size * 256;
  char *buf = (char *)arena.alloc_raw(buf_size, 1);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", PROCESS_COPY_HEADER);

  for (size_t i = 0; i < my_state.lines.size; ++i) {
    const BriefTableLine &line = my_state.lines.data[i];
    const ProcessStat &stat = snapshot.stats.data[line.state_index];
    const ProcessDerivedStat &derived = snapshot.derived_stats.data[line.state_index];
    ptr += snprintf(ptr, buf_size - (ptr - buf), "%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f\n",
                    stat.pid, stat.comm, stat.state, stat.num_threads,
                    derived.cpu_user_perc + derived.cpu_kernel_perc,
                    derived.cpu_user_perc, derived.cpu_kernel_perc,
                    derived.mem_resident_bytes / 1024.0, stat.vsize / 1024.0,
                    derived.io_read_kb_per_sec, derived.io_write_kb_per_sec);
  }
  ImGui::SetClipboardText(buf);
}

void sort_as_requested(BriefTableState &my_state, const StateSnapshot &state) {
  const auto sort_ascending = [&state, sorted_by = my_state.sorted_by](const BriefTableLine &left, const BriefTableLine &right) {
    switch (sorted_by) {
      case eBriefTableColumnId_Pid: return left.pid < right.pid;
      case eBriefTableColumnId_Name: return strcmp(state.stats.data[left.state_index].comm, state.stats.data[right.state_index].comm) < 0;
      case eBriefTableColumnId_State: return state.stats.data[left.state_index].state < state.stats.data[right.state_index].state;
      case eBriefTableColumnId_Threads: return state.stats.data[left.state_index].num_threads < state.stats.data[right.state_index].num_threads;
      case eBriefTableColumnId_CpuTotalPerc: {
        const double left_val = state.derived_stats.data[left.state_index].cpu_user_perc + state.derived_stats.data[left.state_index].cpu_kernel_perc;
        const double right_val = state.derived_stats.data[right.state_index].cpu_user_perc + state.derived_stats.data[right.state_index].cpu_kernel_perc;
        return left_val < right_val;
      }
      case eBriefTableColumnId_CpuUserPerc: return state.derived_stats.data[left.state_index].cpu_user_perc < state.derived_stats.data[right.state_index].cpu_user_perc;
      case eBriefTableColumnId_CpuKernelPerc: return state.derived_stats.data[left.state_index].cpu_kernel_perc < state.derived_stats.data[right.state_index].cpu_kernel_perc;
      case eBriefTableColumnId_MemRssBytes: return state.derived_stats.data[left.state_index].mem_resident_bytes < state.derived_stats.data[right.state_index].mem_resident_bytes;
      case eBriefTableColumnId_MemVirtBytes: return state.stats.data[left.state_index].vsize < state.stats.data[right.state_index].vsize;
      case eBriefTableColumnId_IoReadKbPerSec: return state.derived_stats.data[left.state_index].io_read_kb_per_sec < state.derived_stats.data[right.state_index].io_read_kb_per_sec;
      case eBriefTableColumnId_IoWriteKbPerSec: return state.derived_stats.data[left.state_index].io_write_kb_per_sec < state.derived_stats.data[right.state_index].io_write_kb_per_sec;
      case eBriefTableColumnId_Count: return false;
    }
    return false;
  };

  if (my_state.sorted_order != ImGuiSortDirection_Descending) {
    std::stable_sort(my_state.lines.data, my_state.lines.data + my_state.lines.size, sort_ascending);
  } else {
    std::stable_sort(my_state.lines.data, my_state.lines.data + my_state.lines.size, [&](const auto& left, const auto& right) { return sort_ascending(right, left); });
  }
}

} // unnamed namespace


// Rebuilds lines in previous display order (with new processes appended) for stable sorting.
void brief_table_update(
  BriefTableState &my_state, State &state, const StateSnapshot &old) {

  const StateSnapshot &new_snapshot = state.snapshot;
  const Array<BriefTableLine> &old_lines = my_state.lines;

  Array<bool> added = Array<bool>::create(state.snapshot_arena, new_snapshot.stats.size);
  for (size_t i = 0; i < added.size; ++i) {
    added.data[i] = false;
  }

  Array<BriefTableLine> new_lines = Array<BriefTableLine>::create(state.snapshot_arena, new_snapshot.stats.size);
  size_t new_lines_count = 0;

  for (size_t i = 0; i < old_lines.size; ++i) {
    const BriefTableLine &old_line = old_lines.data[i];
    size_t state_index = binary_search_pid(new_snapshot.stats, old_line.pid);

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
  sort_as_requested(my_state, new_snapshot);
}

static void draw_tree_nodes(FrameContext &ctx, ViewState &view_state,
                            const State &state, BriefTreeNode *node,
                            BriefTableState &my_state) {
  for (BriefTreeNode *n = node; n; n = n->next_sibling) {
    const ProcessStat &stat = state.snapshot.stats.data[n->state_index];
    const ProcessDerivedStat &derived_stat = state.snapshot.derived_stats.data[n->state_index];
    bool has_children = (n->first_child != nullptr);
    bool is_selected = (my_state.selected_pid == n->pid);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
                               ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf |
                                ImGuiTreeNodeFlags_NoTreePushOnOpen;

    char label[32];
    snprintf(label, sizeof(label), "%d", n->pid);
    bool open = ImGui::TreeNodeEx(label, flags);

    // Handle selection on click
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
      my_state.selected_pid = n->pid;
    }

    // Context menu (same as flat mode)
    if (ImGui::BeginPopupContextItem(label)) {
      my_state.selected_pid = n->pid;
      if (ImGui::MenuItem("Copy", "Ctrl+C")) {
        copy_process_row(stat, derived_stat);
      }
      if (ImGui::MenuItem("Copy All")) {
        copy_all_processes(ctx.frame_arena, my_state, state.snapshot);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("CPU Chart")) {
        cpu_chart_add(view_state.cpu_chart_state, n->pid, stat.comm);
      }
      if (ImGui::MenuItem("Memory Chart")) {
        mem_chart_add(view_state.mem_chart_state, n->pid, stat.comm);
      }
      if (ImGui::MenuItem("I/O Chart")) {
        io_chart_add(view_state.io_chart_state, n->pid, stat.comm);
      }
      if (ImGui::MenuItem("Show Loaded Libraries")) {
        library_viewer_request(view_state.library_viewer_state,
                               *view_state.sync, n->pid, stat.comm);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Kill Process", "Del")) {
        if (kill(n->pid, SIGTERM) != 0) {
          snprintf(my_state.kill_error, sizeof(my_state.kill_error),
                   "Failed to kill %d: %s", n->pid, strerror(errno));
        }
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::MenuItem("Force Kill")) {
        if (kill(n->pid, SIGKILL) != 0) {
          snprintf(my_state.kill_error, sizeof(my_state.kill_error),
                   "Failed to kill %d: %s", n->pid, strerror(errno));
        }
      }
      ImGui::EndPopup();
    }

    // Render other columns
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Name)) ImGui::Text("%s", stat.comm);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_State)) {
      ImGui::Text("%c", stat.state);
      if (ImGui::IsItemHovered()) {
        const char *desc = nullptr;
        switch (stat.state) {
          case 'R': desc = "Running"; break;
          case 'S': desc = "Sleeping (interruptible)"; break;
          case 'D': desc = "Disk sleep (uninterruptible)"; break;
          case 'Z': desc = "Zombie"; break;
          case 'T': desc = "Stopped (signal)"; break;
          case 't': desc = "Tracing stop"; break;
          case 'X': case 'x': desc = "Dead"; break;
          case 'I': desc = "Idle"; break;
        }
        if (desc) ImGui::SetTooltip("%s", desc);
      }
    }
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Threads)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%ld", stat.num_threads);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuTotalPerc)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.cpu_user_perc + derived_stat.cpu_kernel_perc);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuUserPerc)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.cpu_user_perc);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuKernelPerc)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.cpu_kernel_perc);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemRssBytes)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.0f K", derived_stat.mem_resident_bytes / 1024);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemVirtBytes)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.0f K", stat.vsize / 1024.0);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoReadKbPerSec)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.io_read_kb_per_sec);
    if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoWriteKbPerSec)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.io_write_kb_per_sec);

    // Recurse for children
    if (open && has_children) {
      draw_tree_nodes(ctx, view_state, state, n->first_child, my_state);
      ImGui::TreePop();
    }
  }
}

void brief_table_draw(FrameContext &ctx, ViewState &view_state, const State &state) {
  BriefTableState &my_state = view_state.brief_table_state;

  ImGui::Begin("Process Table", nullptr, COMMON_VIEW_FLAGS);
  ImGui::Checkbox("Tree View", &my_state.tree_mode);
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu processes)", my_state.lines.size);

  if (ImGui::BeginTable("Processes", eBriefTableColumnId_Count,
                        ImGuiTableFlags_Resizable |
                        ImGuiTableFlags_Reorderable |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Hideable |
                        ImGuiTableFlags_Sortable |
                        ImGuiTableFlags_NoSavedSettings |
                        ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("Process ID", ImGuiTableColumnFlags_NoHide, 0.0f, eBriefTableColumnId_Pid);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f, eBriefTableColumnId_Name);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None, 0.0f, eBriefTableColumnId_State);
    ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 0.0f, eBriefTableColumnId_Threads);
    ImGui::TableSetupColumn("CPU Total (%)", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, eBriefTableColumnId_CpuTotalPerc);
    ImGui::TableSetupColumn("CPU User (%)", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 0.0f, eBriefTableColumnId_CpuUserPerc);
    ImGui::TableSetupColumn("CPU Kernel (%)", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, eBriefTableColumnId_CpuKernelPerc);
    ImGui::TableSetupColumn("RSS (Bytes)", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, eBriefTableColumnId_MemRssBytes);
    ImGui::TableSetupColumn("Virtual Size (Bytes)", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 0.0f, eBriefTableColumnId_MemVirtBytes);
    ImGui::TableSetupColumn("I/O Read (KB/s)", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 0.0f, eBriefTableColumnId_IoReadKbPerSec);
    ImGui::TableSetupColumn("I/O Write (KB/s)", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, 0.0f, eBriefTableColumnId_IoWriteKbPerSec);
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
      if (sort_specs->SpecsDirty) {
        my_state.sorted_by = static_cast<BriefTableColumnId>(sort_specs->Specs->ColumnUserID);
        my_state.sorted_order = sort_specs->Specs->SortDirection;
        sort_as_requested(my_state, state.snapshot);
        sort_specs->SpecsDirty = false;
      }
    }

    // Build tree on demand when in tree mode
    BriefTreeNode *tree_roots = nullptr;
    if (my_state.tree_mode) {
      tree_roots = build_process_tree(
          ctx.frame_arena, my_state.lines, state.snapshot,
          my_state.sorted_by, my_state.sorted_order);
    }

    if (my_state.tree_mode && tree_roots) {
      // Tree mode rendering
      draw_tree_nodes(ctx, view_state, state, tree_roots, my_state);
    } else {
      // Flat mode rendering
      for (size_t i = 0; i < my_state.lines.size; ++i) {
        ImGui::TableNextRow();

        BriefTableLine &line = my_state.lines.data[i];
        const ProcessStat &stat = state.snapshot.stats.data[line.state_index];
        const ProcessDerivedStat &derived_stat = state.snapshot.derived_stats.data[line.state_index];
        const bool is_selected = (my_state.selected_pid == line.pid);

        ImGui::TableSetColumnIndex(eBriefTableColumnId_Pid);
        {
          char label[32];
          snprintf(label, sizeof(label), "%d", line.pid);
          if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
            my_state.selected_pid = line.pid;
          }
          if (ImGui::BeginPopupContextItem(label)) {
            my_state.selected_pid = line.pid;
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
              copy_process_row(stat, derived_stat);
            }
            if (ImGui::MenuItem("Copy All")) {
              copy_all_processes(ctx.frame_arena, my_state, state.snapshot);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("CPU Chart")) {
              cpu_chart_add(view_state.cpu_chart_state, line.pid, stat.comm);
            }
            if (ImGui::MenuItem("Memory Chart")) {
              mem_chart_add(view_state.mem_chart_state, line.pid, stat.comm);
            }
            if (ImGui::MenuItem("I/O Chart")) {
              io_chart_add(view_state.io_chart_state, line.pid, stat.comm);
            }
            if (ImGui::MenuItem("Show Loaded Libraries")) {
              library_viewer_request(view_state.library_viewer_state,
                                     *view_state.sync, line.pid, stat.comm);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Kill Process", "Del") || ImGui::IsKeyPressed(ImGuiKey_Delete)) {
              if (kill(line.pid, SIGTERM) != 0) {
                snprintf(my_state.kill_error, sizeof(my_state.kill_error), "Failed to kill %d: %s", line.pid, strerror(errno));
              }
              ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Force Kill")) {
              if (kill(line.pid, SIGKILL) != 0) {
                snprintf(my_state.kill_error, sizeof(my_state.kill_error), "Failed to kill %d: %s", line.pid, strerror(errno));
              }
            }
            ImGui::EndPopup();
          }
        }
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Name)) ImGui::Text("%s", stat.comm);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_State)) {
          ImGui::Text("%c", stat.state);
          if (ImGui::IsItemHovered()) {
            const char *desc = nullptr;
            switch (stat.state) {
              case 'R': desc = "Running"; break;
              case 'S': desc = "Sleeping (interruptible)"; break;
              case 'D': desc = "Disk sleep (uninterruptible)"; break;
              case 'Z': desc = "Zombie"; break;
              case 'T': desc = "Stopped (signal)"; break;
              case 't': desc = "Tracing stop"; break;
              case 'X': case 'x': desc = "Dead"; break;
              case 'I': desc = "Idle"; break;
            }
            if (desc) ImGui::SetTooltip("%s", desc);
          }
        }
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Threads)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%ld", stat.num_threads);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuTotalPerc)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.cpu_user_perc + derived_stat.cpu_kernel_perc);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuUserPerc)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.cpu_user_perc);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuKernelPerc)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.cpu_kernel_perc);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemRssBytes)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.0f K", derived_stat.mem_resident_bytes / 1024);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemVirtBytes)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.0f K", stat.vsize / 1024.0);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoReadKbPerSec)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.io_read_kb_per_sec);
        if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoWriteKbPerSec)) ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f", derived_stat.io_write_kb_per_sec);
      }
    }

    ImGui::EndTable();
  }

  // Ctrl+C to copy selected row
  if (my_state.selected_pid > 0 && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
    for (size_t i = 0; i < my_state.lines.size; ++i) {
      if (my_state.lines.data[i].pid == my_state.selected_pid) {
        const ProcessStat &stat = state.snapshot.stats.data[my_state.lines.data[i].state_index];
        const ProcessDerivedStat &derived = state.snapshot.derived_stats.data[my_state.lines.data[i].state_index];
        copy_process_row(stat, derived);
        break;
      }
    }
  }

  // Del key to kill selected process
  if (my_state.selected_pid > 0 && ImGui::Shortcut(ImGuiKey_Delete)) {
    if (kill(my_state.selected_pid, SIGTERM) != 0) {
      snprintf(my_state.kill_error, sizeof(my_state.kill_error), "Failed to kill %d: %s", my_state.selected_pid, strerror(errno));
    }
  }

  // Show error popup if there's an error
  if (my_state.kill_error[0] != '\0') {
    ImGui::OpenPopup("Kill Error");
  }
  if (ImGui::BeginPopupModal("Kill Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s", my_state.kill_error);
    if (ImGui::Button("OK")) {
      my_state.kill_error[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
}
