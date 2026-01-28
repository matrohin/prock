#include "brief_table.h"

#include "views/common.h"
#include "views/cpu_chart.h"
#include "views/environ_viewer.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/net_chart.h"
#include "views/socket_viewer.h"
#include "views/threads_viewer.h"
#include "views/view_state.h"

#include "state.h"

#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <signal.h>

// Highlight durations (must match brief_table_logic.cpp)
static constexpr int64_t NEW_PROCESS_HIGHLIGHT_NS = 2'000'000'000; // 2 seconds

// Highlight colors (RGBA, values 0-255)
// TODO: Change colors based on dark/light themes
static constexpr ImU32 NEW_PROCESS_COLOR = IM_COL32(0, 140, 0, 60);
static constexpr ImU32 DEAD_PROCESS_COLOR = IM_COL32(180, 50, 50, 60);

const char *PROCESS_COPY_HEADER =
    "PID\tName\tState\tThreads\tCPU Total\tCPU User\tCPU Kernel\tRSS "
    "(KB)\tVirt (KB)\tI/O Read (KB/s)\tI/O Write (KB/s)\tNet Recv (KB/s)\tNet "
    "Send (KB/s)\n";

static void open_all_windows(const int pid, const char *comm,
                             ViewState &view_state) {
  const ImGuiID dock_id =
      process_host_open(view_state.process_host_state, pid, comm);
  if (dock_id == 0) return;
  constexpr ProcessWindowFlags no_focus = eProcessWindowFlags_NoFocusOnAppearing;
  cpu_chart_add(view_state.cpu_chart_state, pid, comm, dock_id);
  mem_chart_add(view_state.mem_chart_state, pid, comm, dock_id, no_focus);
  io_chart_add(view_state.io_chart_state, pid, comm, dock_id, no_focus);
  net_chart_add(view_state.net_chart_state, pid, comm, dock_id, no_focus);
  library_viewer_request(view_state.library_viewer_state, *view_state.sync, pid,
                         comm, dock_id, no_focus);
  environ_viewer_request(view_state.environ_viewer_state, *view_state.sync, pid,
                         comm, dock_id, no_focus);
  threads_viewer_open(view_state.threads_viewer_state, *view_state.sync, pid,
                      comm, dock_id, no_focus);
  socket_viewer_request(view_state.socket_viewer_state, *view_state.sync, pid,
                        comm, dock_id, no_focus);
}

static void copy_process_row(const BriefTableLine &line) {
  const ProcessDerivedStat &derived = line.derived_stat;
  char buf[512];
  snprintf(buf, sizeof(buf),
           "%s%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f\t%.1f\t"
           "%.1f",
           PROCESS_COPY_HEADER, line.pid, line.comm, line.state,
           line.num_threads, derived.cpu_user_perc + derived.cpu_kernel_perc,
           derived.cpu_user_perc, derived.cpu_kernel_perc,
           derived.mem_resident_bytes / 1024.0,
           derived.mem_virtual_bytes / 1024.0, derived.io_read_kb_per_sec,
           derived.io_write_kb_per_sec, derived.net_recv_kb_per_sec,
           derived.net_send_kb_per_sec);
  ImGui::SetClipboardText(buf);
}

static void copy_all_processes(BumpArena &arena,
                               const BriefTableState &my_state) {
  // Header + all rows
  const size_t buf_size = 256 + my_state.lines.size * 256;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", PROCESS_COPY_HEADER);

  for (size_t i = 0; i < my_state.lines.size; ++i) {
    const BriefTableLine &line = my_state.lines.data[i];
    const ProcessDerivedStat &derived = line.derived_stat;
    ptr += snprintf(ptr, buf_size - (ptr - buf),
                    "%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%."
                    "1f\t%.1f\t%.1f\n",
                    line.pid, line.comm, line.state, line.num_threads,
                    derived.cpu_user_perc + derived.cpu_kernel_perc,
                    derived.cpu_user_perc, derived.cpu_kernel_perc,
                    derived.mem_resident_bytes / 1024.0,
                    derived.mem_virtual_bytes / 1024.0,
                    derived.io_read_kb_per_sec, derived.io_write_kb_per_sec,
                    derived.net_recv_kb_per_sec, derived.net_send_kb_per_sec);
  }
  ImGui::SetClipboardText(buf);
}

static void table_context_menu_draw(FrameContext &ctx, ViewState &view_state,
                                    BriefTableState &my_state,
                                    const BriefTableLine &line,
                                    const char *label) {
  const int pid = line.pid;
  if (ImGui::BeginPopupContextItem(label)) {
    my_state.selected_pid = pid;
    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
      copy_process_row(line);
    }
    if (ImGui::MenuItem("Copy All")) {
      copy_all_processes(ctx.frame_arena, my_state);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("CPU Chart")) {
      cpu_chart_add(view_state.cpu_chart_state, pid, line.comm);
    }
    if (ImGui::MenuItem("Memory Chart")) {
      mem_chart_add(view_state.mem_chart_state, pid, line.comm);
    }
    if (ImGui::MenuItem("I/O Chart")) {
      io_chart_add(view_state.io_chart_state, pid, line.comm);
    }
    if (ImGui::MenuItem("Network Chart")) {
      net_chart_add(view_state.net_chart_state, pid, line.comm);
    }
    if (ImGui::MenuItem("Show Loaded Libraries")) {
      library_viewer_request(view_state.library_viewer_state, *view_state.sync,
                             pid, line.comm);
    }
    if (ImGui::MenuItem("Show Environment")) {
      environ_viewer_request(view_state.environ_viewer_state, *view_state.sync,
                             pid, line.comm);
    }
    if (ImGui::MenuItem("Show Threads")) {
      threads_viewer_open(view_state.threads_viewer_state, *view_state.sync,
                          pid, line.comm);
    }
    if (ImGui::MenuItem("Show Sockets")) {
      socket_viewer_request(view_state.socket_viewer_state, *view_state.sync,
                            pid, line.comm);
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

static void data_columns_draw(const BriefTableLine &line) {
  const ProcessDerivedStat &derived_stat = line.derived_stat;
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Name))
    ImGui::Text("%s", line.comm);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_State)) {
    ImGui::Text("%c", line.state);
    if (ImGui::IsItemHovered()) {
      const char *desc = get_state_tooltip(line.state);
      if (desc) ImGui::SetTooltip("%s", desc);
    }
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Threads))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%ld", line.num_threads);
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
    format_memory_bytes(derived_stat.mem_virtual_bytes, buf, sizeof(buf));
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", buf);
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoReadKbPerSec))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.io_read_kb_per_sec);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoWriteKbPerSec))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.io_write_kb_per_sec);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_NetRecvKbPerSec))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.net_recv_kb_per_sec);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_NetSendKbPerSec))
    ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                       derived_stat.net_send_kb_per_sec);
}

void brief_table_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state) {
  ZoneScoped;
  BriefTableState &my_state = view_state.brief_table_state;

  char title[64];
  snprintf(title, sizeof(title), "Process Table (%zu processes)###ProcessTable",
           my_state.lines.size);
  ImGui::Begin(title, nullptr, COMMON_VIEW_FLAGS);

  ImGuiTextFilter filter = draw_filter_input(
      "##ProcessFilter", my_state.filter_text, sizeof(my_state.filter_text));
  ImGui::SameLine();
  bool reset_sort_to_pid = false;
  if (ImGui::Checkbox("Tree", &my_state.tree_mode) && my_state.tree_mode) {
    // Reset to PID sorting when entering tree mode
    my_state.sorted_by = eBriefTableColumnId_Pid;
    my_state.sorted_order = ImGuiSortDirection_Ascending;
    reset_sort_to_pid = true;
    sort_brief_table_tree(my_state, ctx.frame_arena);
  }

  if (ImGui::BeginTable(
          "Processes", eBriefTableColumnId_Count,
          ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
              ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable |
              ImGuiTableFlags_Sortable | ImGuiTableFlags_Borders)) {
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
    ImGui::TableSetupColumn("Net Recv (KB/s)",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_NetRecvKbPerSec);
    ImGui::TableSetupColumn("Net Send (KB/s)",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide,
                            0.0f, eBriefTableColumnId_NetSendKbPerSec);
    if (reset_sort_to_pid) {
      ImGui::TableSetColumnSortDirection(eBriefTableColumnId_Pid,
                                         ImGuiSortDirection_Ascending, false);
    }
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
      if (sort_specs->SpecsDirty) {
        my_state.sorted_by =
            static_cast<BriefTableColumnId>(sort_specs->Specs->ColumnUserID);
        my_state.sorted_order = sort_specs->Specs->SortDirection;
        if (my_state.sorted_by != eBriefTableColumnId_Pid ||
            my_state.sorted_order != ImGuiSortDirection_Ascending) {
          my_state.tree_mode = false; // Disable tree mode when user sorts
        }
        if (!my_state.tree_mode) {
          sort_brief_table_lines(my_state);
        }
        sort_specs->SpecsDirty = false;
      }
    }

    const int64_t now_ns = state.snapshot.at.time_since_epoch().count();
    int current_tree_depth = 0;  // Track depth for TreePop management
    int collapsed_at_depth = -1; // -1 means no collapsed parent; otherwise skip
                                 // children deeper than this

    for (size_t i = 0; i < my_state.lines.size; ++i) {
      const BriefTableLine &line = my_state.lines.data[i];
      const bool is_dead = line.death_time_ns != 0;
      const bool is_new =
          !is_dead && now_ns - line.first_seen_ns < NEW_PROCESS_HIGHLIGHT_NS;

      char label[32];
      snprintf(label, sizeof(label), "%d", line.pid);

      // Skip non-matching processes when filter is active
      if (!filter.PassFilter(line.comm) && !filter.PassFilter(label)) continue;

      // In tree mode, handle collapsed parent tracking and TreePop
      if (my_state.tree_mode) {
        // If we've returned to or above a collapsed node's depth, stop
        // skipping
        if (collapsed_at_depth >= 0 && line.tree_depth <= collapsed_at_depth) {
          collapsed_at_depth = -1;
        }

        // Skip children of collapsed nodes
        if (collapsed_at_depth >= 0 && line.tree_depth > collapsed_at_depth) {
          continue;
        }

        // Pop back to the correct depth before rendering this node
        while (current_tree_depth > line.tree_depth) {
          ImGui::TreePop();
          --current_tree_depth;
        }
      }

      ImGui::TableNextRow();

      // Apply row highlighting
      if (is_dead) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, DEAD_PROCESS_COLOR);
      } else if (is_new) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, NEW_PROCESS_COLOR);
      }

      const bool is_selected = my_state.selected_pid == line.pid;
      ImGui::TableSetColumnIndex(eBriefTableColumnId_Pid);

      if (my_state.tree_mode) {
        // Check if this node has children (next line has greater depth)
        const bool has_children =
            (i + 1 < my_state.lines.size) &&
            (my_state.lines.data[i + 1].tree_depth > line.tree_depth);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
                                   ImGuiTreeNodeFlags_DefaultOpen |
                                   ImGuiTreeNodeFlags_OpenOnArrow;
        if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf;
        if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;

        const bool node_open = ImGui::TreeNodeEx(label, flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
          my_state.selected_pid = line.pid;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
            !ImGui::IsItemToggledOpen()) {
          open_all_windows(line.pid, line.comm, view_state);
        }
        table_context_menu_draw(ctx, view_state, my_state, line, label);
        data_columns_draw(line);

        if (node_open && has_children) {
          // Children will follow; track depth for later TreePop
          ++current_tree_depth;
        } else if (node_open) {
          // Leaf node that was opened - pop immediately
          ImGui::TreePop();
        } else if (has_children) {
          // Node is collapsed - skip its children
          collapsed_at_depth = line.tree_depth;
        }
      } else {
        if (ImGui::Selectable(label, is_selected,
                              ImGuiSelectableFlags_SpanAllColumns) ||
            ImGui::IsItemFocused()) {
          my_state.selected_pid = line.pid;
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
          open_all_windows(line.pid, line.comm, view_state);
        }

        table_context_menu_draw(ctx, view_state, my_state, line, label);
        data_columns_draw(line);
      }
    }

    // Pop any remaining tree levels
    while (current_tree_depth > 0) {
      ImGui::TreePop();
      --current_tree_depth;
    }

    ImGui::EndTable();
  }

  // Shortcuts for per-process actions:
  if (my_state.selected_pid > 0) {
    // Ctrl+C to copy selected row
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
      for (size_t i = 0; i < my_state.lines.size; ++i) {
        if (my_state.lines.data[i].pid == my_state.selected_pid) {
          copy_process_row(my_state.lines.data[i]);
          break;
        }
      }
    }

    // Del key to kill selected process
    if (my_state.selected_pid > 0 && ImGui::Shortcut(ImGuiKey_Delete)) {
      if (kill(my_state.selected_pid, SIGTERM) != 0) {
        snprintf(my_state.kill_error, sizeof(my_state.kill_error),
                 "Failed to kill %d: %s", my_state.selected_pid,
                 strerror(errno));
      }
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
