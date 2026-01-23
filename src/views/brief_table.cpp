#include "brief_table.h"

#include "views/common.h"
#include "views/cpu_chart.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui_internal.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <signal.h>

const char *PROCESS_COPY_HEADER =
    "PID\tName\tState\tThreads\tCPU Total\tCPU User\tCPU Kernel\tRSS "
    "(KB)\tVirt (KB)\tI/O Read (KB/s)\tI/O Write (KB/s)\n";

static void copy_process_row(const ProcessStat &stat,
                             const ProcessDerivedStat &derived) {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "%s%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f",
           PROCESS_COPY_HEADER, stat.pid, stat.comm, stat.state,
           stat.num_threads, derived.cpu_user_perc + derived.cpu_kernel_perc,
           derived.cpu_user_perc, derived.cpu_kernel_perc,
           derived.mem_resident_bytes / 1024.0, stat.vsize / 1024.0,
           derived.io_read_kb_per_sec, derived.io_write_kb_per_sec);
  ImGui::SetClipboardText(buf);
}

static void copy_all_processes(BumpArena &arena,
                               const BriefTableState &my_state,
                               const StateSnapshot &snapshot) {
  // Header + all rows
  size_t buf_size = 256 + my_state.lines.size * 256;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", PROCESS_COPY_HEADER);

  for (size_t i = 0; i < my_state.lines.size; ++i) {
    const BriefTableLine &line = my_state.lines.data[i];
    const ProcessStat &stat = snapshot.stats.data[line.state_index];
    const ProcessDerivedStat &derived =
        snapshot.derived_stats.data[line.state_index];
    ptr +=
        snprintf(ptr, buf_size - (ptr - buf),
                 "%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f\n",
                 stat.pid, stat.comm, stat.state, stat.num_threads,
                 derived.cpu_user_perc + derived.cpu_kernel_perc,
                 derived.cpu_user_perc, derived.cpu_kernel_perc,
                 derived.mem_resident_bytes / 1024.0, stat.vsize / 1024.0,
                 derived.io_read_kb_per_sec, derived.io_write_kb_per_sec);
  }
  ImGui::SetClipboardText(buf);
}

// Returns true if this node, any sibling, or any descendant matches the filter
static bool tree_node_has_match(const BriefTreeNode *node,
                                const ImGuiTextFilter &filter,
                                const StateSnapshot &snapshot) {
  for (const BriefTreeNode *n = node; n; n = n->next_sibling) {
    const ProcessStat &stat = snapshot.stats.data[n->state_index];
    if (filter.PassFilter(stat.comm)) return true;

    // Check children
    if (n->first_child &&
        tree_node_has_match(n->first_child, filter, snapshot)) {
      return true;
    }
  }
  return false;
}

static const char *get_state_tooltip(const ProcessStat &stat) {
  switch (stat.state) {
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

static void table_context_menu_draw(FrameContext &ctx, ViewState &view_state,
                                    const State &state,
                                    BriefTableState &my_state, const int pid,
                                    const ProcessStat &stat,
                                    const ProcessDerivedStat &derived_stat,
                                    const char *label) {
  if (ImGui::BeginPopupContextItem(label)) {
    my_state.selected_pid = pid;
    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
      copy_process_row(stat, derived_stat);
    }
    if (ImGui::MenuItem("Copy All")) {
      copy_all_processes(ctx.frame_arena, my_state, state.snapshot);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("CPU Chart")) {
      cpu_chart_add(view_state.cpu_chart_state, pid, stat.comm);
    }
    if (ImGui::MenuItem("Memory Chart")) {
      mem_chart_add(view_state.mem_chart_state, pid, stat.comm);
    }
    if (ImGui::MenuItem("I/O Chart")) {
      io_chart_add(view_state.io_chart_state, pid, stat.comm);
    }
    if (ImGui::MenuItem("Show Loaded Libraries")) {
      library_viewer_request(view_state.library_viewer_state, *view_state.sync,
                             pid, stat.comm);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Kill Process", "Del") ||
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
      if (kill(pid, SIGTERM) != 0) {
        snprintf(my_state.kill_error, sizeof(my_state.kill_error),
                 "Failed to kill %d: %s", pid, strerror(errno));
      }
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::MenuItem("Force Kill")) {
      if (kill(pid, SIGKILL) != 0) {
        snprintf(my_state.kill_error, sizeof(my_state.kill_error),
                 "Failed to kill %d: %s", pid, strerror(errno));
      }
    }
    ImGui::EndPopup();
  }
}

static void data_columns_draw(const ProcessStat &stat,
                              const ProcessDerivedStat &derived_stat) {
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Name))
    ImGui::Text("%s", stat.comm);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_State)) {
    ImGui::Text("%c", stat.state);
    if (ImGui::IsItemHovered()) {
      const char *desc = get_state_tooltip(stat);
      if (desc) ImGui::SetTooltip("%s", desc);
    }
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Threads))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%ld", stat.num_threads);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuTotalPerc))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.cpu_user_perc +
                           derived_stat.cpu_kernel_perc);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuUserPerc))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.cpu_user_perc);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuKernelPerc))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.cpu_kernel_perc);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemRssBytes)) {
    char buf[32];
    format_memory_bytes(derived_stat.mem_resident_bytes, buf, sizeof(buf));
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", buf);
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemVirtBytes)) {
    char buf[32];
    format_memory_bytes(stat.vsize, buf, sizeof(buf));
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", buf);
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoReadKbPerSec))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.io_read_kb_per_sec);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoWriteKbPerSec))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.io_write_kb_per_sec);
}

static void draw_tree_nodes(FrameContext &ctx, ViewState &view_state,
                            const State &state, BriefTreeNode *node,
                            BriefTableState &my_state,
                            const ImGuiTextFilter &filter) {
  const bool filter_active = filter.IsActive();

  for (BriefTreeNode *n = node; n; n = n->next_sibling) {
    const ProcessStat &stat = state.snapshot.stats.data[n->state_index];
    const ProcessDerivedStat &derived_stat =
        state.snapshot.derived_stats.data[n->state_index];
    const bool has_children = n->first_child != nullptr;
    const bool is_selected = my_state.selected_pid == n->pid;

    // Filter logic: skip if no match and no matching descendants
    bool node_matches = filter.PassFilter(stat.comm);
    bool has_matching_descendant = false;
    if (filter_active && !node_matches && has_children) {
      has_matching_descendant =
          tree_node_has_match(n->first_child, filter, state.snapshot);
    }
    if (filter_active && !node_matches && !has_matching_descendant) {
      continue;
    }

    // Gray out non-matching parents that have matching descendants
    bool should_gray =
        filter_active && !node_matches && has_matching_descendant;
    if (should_gray) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
                               ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!has_children)
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    char label[32];
    snprintf(label, sizeof(label), "%d", n->pid);
    const bool open = ImGui::TreeNodeEx(label, flags);

    // Handle selection on click or keyboard navigation
    if ((ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ||
        ImGui::IsItemFocused()) {
      my_state.selected_pid = n->pid;
    }

    table_context_menu_draw(ctx, view_state, state, my_state, n->pid, stat,
                            derived_stat, label);

    data_columns_draw(stat, derived_stat);

    // Pop gray style before recursing so children aren't affected
    if (should_gray) {
      ImGui::PopStyleColor();
    }

    // Recurse for children
    if (open && has_children) {
      draw_tree_nodes(ctx, view_state, state, n->first_child, my_state, filter);
      ImGui::TreePop();
    }
  }
}

void brief_table_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state) {
  BriefTableState &my_state = view_state.brief_table_state;

  char title[64];
  snprintf(title, sizeof(title), "Process Table (%zu processes)###ProcessTable",
           my_state.lines.size);
  ImGui::Begin(title, nullptr, COMMON_VIEW_FLAGS);
  if (ImGui::IsWindowFocused()) {
    view_state.focused_view = eFocusedView_BriefTable;
  }

  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::InputTextWithHint("##ProcessFilter", "Filter", my_state.filter_text,
                           sizeof(my_state.filter_text));
  ImGui::SameLine();
  ImGui::Checkbox("Tree", &my_state.tree_mode);
  ImGuiTextFilter filter;
  if (my_state.filter_text[0] != '\0') {
    strncpy(filter.InputBuf, my_state.filter_text, sizeof(filter.InputBuf));
    filter.InputBuf[sizeof(filter.InputBuf) - 1] = '\0';
    filter.Build();
  }

  if (ImGui::BeginTable(
          "Processes", eBriefTableColumnId_Count,
          ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
              ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable |
              ImGuiTableFlags_Sortable | ImGuiTableFlags_NoSavedSettings |
              ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("Process ID", ImGuiTableColumnFlags_NoHide, 0.0f,
                            eBriefTableColumnId_Pid);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f,
                            eBriefTableColumnId_Name);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None, 0.0f,
                            eBriefTableColumnId_State);
    ImGui::TableSetupColumn("Threads",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_Threads);
    ImGui::TableSetupColumn("CPU Total (%)",
                            ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            eBriefTableColumnId_CpuTotalPerc);
    ImGui::TableSetupColumn("CPU User (%)",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_CpuUserPerc);
    ImGui::TableSetupColumn("CPU Kernel (%)",
                            ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            eBriefTableColumnId_CpuKernelPerc);
    ImGui::TableSetupColumn("RSS (Bytes)",
                            ImGuiTableColumnFlags_PreferSortDescending, 0.0f,
                            eBriefTableColumnId_MemRssBytes);
    ImGui::TableSetupColumn("Virtual Size (Bytes)",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_MemVirtBytes);
    ImGui::TableSetupColumn("I/O Read (KB/s)",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_IoReadKbPerSec);
    ImGui::TableSetupColumn("I/O Write (KB/s)",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_IoWriteKbPerSec);
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
      if (sort_specs->SpecsDirty) {
        my_state.sorted_by =
            static_cast<BriefTableColumnId>(sort_specs->Specs->ColumnUserID);
        my_state.sorted_order = sort_specs->Specs->SortDirection;
        sort_brief_table_lines(my_state, state.snapshot);
        sort_specs->SpecsDirty = false;
      }
    }

    // Build tree on demand when in tree mode
    BriefTreeNode *tree_roots = nullptr;
    if (my_state.tree_mode) {
      tree_roots =
          build_process_tree(ctx.frame_arena, my_state.lines, state.snapshot,
                             my_state.sorted_by, my_state.sorted_order);
    }

    if (my_state.tree_mode && tree_roots) {
      // Tree mode rendering
      draw_tree_nodes(ctx, view_state, state, tree_roots, my_state, filter);
    } else {
      // Flat mode rendering
      for (size_t i = 0; i < my_state.lines.size; ++i) {
        BriefTableLine &line = my_state.lines.data[i];
        // NOLINTNEXTLINE
        const ProcessStat &stat = state.snapshot.stats.data[line.state_index];

        // Skip non-matching processes when filter is active
        if (!filter.PassFilter(stat.comm)) continue;

        ImGui::TableNextRow();
        const ProcessDerivedStat &derived_stat =
            state.snapshot.derived_stats.data[line.state_index];

        {
          const bool is_selected = (my_state.selected_pid == line.pid);
          ImGui::TableSetColumnIndex(eBriefTableColumnId_Pid);
          char label[32];
          snprintf(label, sizeof(label), "%d", line.pid);
          if (ImGui::Selectable(label, is_selected,
                                ImGuiSelectableFlags_SpanAllColumns) ||
              ImGui::IsItemFocused()) {
            my_state.selected_pid = line.pid;
          }
          table_context_menu_draw(ctx, view_state, state, my_state, line.pid,
                                  stat, derived_stat, label);
        }
        data_columns_draw(stat, derived_stat);
      }
    }

    ImGui::EndTable();
  }

  // Ctrl+C to copy selected row
  if (my_state.selected_pid > 0 &&
      ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
    for (size_t i = 0; i < my_state.lines.size; ++i) {
      if (my_state.lines.data[i].pid == my_state.selected_pid) {
        // NOLINTNEXTLINE
        const ProcessStat &stat =
            state.snapshot.stats.data[my_state.lines.data[i].state_index];
        const ProcessDerivedStat &derived =
            state.snapshot.derived_stats
                .data[my_state.lines.data[i].state_index];
        copy_process_row(stat, derived);
        break;
      }
    }
  }

  // Del key to kill selected process
  if (my_state.selected_pid > 0 && ImGui::Shortcut(ImGuiKey_Delete)) {
    if (kill(my_state.selected_pid, SIGTERM) != 0) {
      snprintf(my_state.kill_error, sizeof(my_state.kill_error),
               "Failed to kill %d: %s", my_state.selected_pid, strerror(errno));
    }
  }

  // Show error popup if there's an error
  if (my_state.kill_error[0] != '\0') {
    ImGui::OpenPopup("Kill Error");
  }
  if (ImGui::BeginPopupModal("Kill Error", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s", my_state.kill_error);
    if (ImGui::Button("OK")) {
      my_state.kill_error[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
}
