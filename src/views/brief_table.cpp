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
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>

// Highlight durations (must match brief_table_logic.cpp)
static constexpr int64_t NEW_PROCESS_HIGHLIGHT_NS = 2'000'000'000; // 2 seconds

enum FilterResult {
  FilterResult_NoMatch = 0,
  FilterResult_Match = 1,
  FilterResult_SubtreeMatch = 2,  // Token started with '+'
};

static FilterResult imgui_filter_pass_filter_ext(const ImGuiTextFilter &filter,
                                                  const char *text) {
  if (filter.Filters.empty())
    return FilterResult_Match;

  if (text == nullptr)
    text = "";

  FilterResult result = FilterResult_NoMatch;
  for (int i = 0; i < filter.Filters.Size; i++) {
    const ImGuiTextFilter::ImGuiTextRange &f = filter.Filters[i];
    if (f.empty())
      continue;

    const char *filter_begin = f.b;
    const char *filter_end = f.e;
    bool is_subtree = false;

    // Check for '+' prefix
    if (filter_begin < filter_end && *filter_begin == '+') {
      is_subtree = true;
      filter_begin++;
    }

    if (filter_begin >= filter_end)
      continue;

    // Exclusion filter (-)
    if (*filter_begin == '-') {
      if (ImStristr(text, nullptr, filter_begin + 1, filter_end) != nullptr)
        return FilterResult_NoMatch;
      continue;
    }

    // Grep filter
    if (ImStristr(text, nullptr, filter_begin, filter_end) != nullptr) {
      if (is_subtree)
        return FilterResult_SubtreeMatch;
      result = FilterResult_Match;
    }
  }
  return (filter.CountGrep > 0) ? result : FilterResult_Match;
}

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

static bool get_process_affinity(int pid, uint64_t &mask, int num_cpus) {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  if (sched_getaffinity(pid, sizeof(cpu_set), &cpu_set) != 0) return false;
  mask = 0;
  for (int i = 0; i < num_cpus && i < 64; ++i) {
    if (CPU_ISSET(i, &cpu_set)) mask |= (1ULL << i);
  }
  return true;
}

static bool set_process_affinity(int pid, uint64_t mask, char *err,
                                 size_t err_sz, int *err_code) {
  if (mask == 0) {
    snprintf(err, err_sz, "At least one CPU must be selected");
    *err_code = 0;
    return false;
  }
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  for (int i = 0; i < 64; ++i) {
    if (mask & (1ULL << i)) CPU_SET(i, &cpu_set);
  }
  if (sched_setaffinity(pid, sizeof(cpu_set), &cpu_set) != 0) {
    *err_code = errno;
    snprintf(err, err_sz, "Failed to set affinity for PID %d: %s", pid,
             strerror(errno));
    return false;
  }
  return true;
}

static int get_process_nice(int pid) {
  errno = 0;
  int nice = getpriority(PRIO_PROCESS, pid);
  return (nice == -1 && errno != 0) ? 0 : nice;
}

static bool set_process_nice(int pid, int nice_val, char *err, size_t err_sz,
                             int *err_code) {
  if (setpriority(PRIO_PROCESS, pid, nice_val) != 0) {
    *err_code = errno;
    snprintf(err, err_sz, "Failed to set priority for PID %d: %s", pid,
             strerror(errno));
    return false;
  }
  return true;
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
                                    const char *label, int num_cpus) {
  const int pid = line.pid;
  if (ImGui::BeginPopupContextItem(label)) {
    my_state.selected_pid = pid;
    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
      copy_process_row(line);
    }
    if (ImGui::MenuItem("Copy All")) {
      copy_all_processes(ctx.frame_arena, my_state);
    }
    if (ImGui::MenuItem("Filter to subtree")) {
      size_t len = strlen(my_state.filter_text);
      // Append comma if filter not empty
      if (len > 0 && len < sizeof(my_state.filter_text) - 2) {
        my_state.filter_text[len++] = ',';
      }
      snprintf(my_state.filter_text + len, sizeof(my_state.filter_text) - len,
               "+%s", line.comm);
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
    if (ImGui::MenuItem("Set Affinity...")) {
      my_state.control_edit_pid = pid;
      get_process_affinity(pid, my_state.affinity_edit_mask, num_cpus);
      my_state.show_affinity_popup = true;
    }
    if (ImGui::MenuItem("Set Priority...")) {
      my_state.control_edit_pid = pid;
      my_state.priority_edit_nice = get_process_nice(pid);
      my_state.show_priority_popup = true;
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

static void compute_filter_visibility(BriefTableState &my_state,
                                      const ImGuiTextFilter &filter) {
  // First pass: mark direct matches
  for (size_t i = 0; i < my_state.lines.size; ++i) {
    BriefTableLine &line = my_state.lines.data[i];
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", line.pid);

    FilterResult name_result = imgui_filter_pass_filter_ext(filter, line.comm);
    FilterResult pid_result = imgui_filter_pass_filter_ext(filter, pid_str);

    // Take the "better" result (SubtreeMatch > Match > NoMatch)
    FilterResult result = (name_result > pid_result) ? name_result : pid_result;

    if (result == FilterResult_SubtreeMatch)
      line.filter_state = 3;  // Subtree root
    else if (result == FilterResult_Match)
      line.filter_state = 1;  // Regular match
    else
      line.filter_state = 0;  // Hidden
  }

  // Second pass: propagate subtree visibility to descendants
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < my_state.lines.size; ++i) {
      BriefTableLine &line = my_state.lines.data[i];
      if (line.filter_state != 0) continue;

      // Check if parent is a subtree member
      for (size_t j = 0; j < my_state.lines.size; ++j) {
        if (my_state.lines.data[j].pid == line.ppid &&
            my_state.lines.data[j].filter_state == 3) {
          line.filter_state = 3;  // Subtree member
          changed = true;
          break;
        }
      }
    }
  }

  // Third pass (tree mode only): propagate visibility to ancestors
  // Iterate in REVERSE - in reverse DFS order, a shallower depth after a
  // deeper visible node means this node is an ancestor of that visible node
  if (my_state.tree_mode) {
    int last_visible_depth = -1;
    for (size_t i = my_state.lines.size; i-- > 0;) {
      BriefTableLine &line = my_state.lines.data[i];
      const int depth = line.tree_depth;

      // If at shallower depth than last visible, this is an ancestor
      if (depth < last_visible_depth && line.filter_state == 0) {
        line.filter_state = 2; // ancestor
      }

      // Track depth of visible nodes
      if (line.filter_state != 0) {
        last_visible_depth = depth;
      }
    }
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

static void affinity_popup_draw(BriefTableState &my_state, int num_cpus) {
  if (my_state.show_affinity_popup) {
    ImGui::OpenPopup("Set CPU Affinity");
    my_state.show_affinity_popup = false;
  }
  if (ImGui::BeginPopupModal("Set CPU Affinity", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::Text("PID: %d", my_state.control_edit_pid);
    ImGui::Separator();

    if (ImGui::Button("Select All")) {
      my_state.affinity_edit_mask = (num_cpus >= 64)
                                        ? ~0ULL
                                        : ((1ULL << num_cpus) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
      my_state.affinity_edit_mask = 0;
    }
    ImGui::Separator();

    // Display checkboxes in a grid (8 per row)
    constexpr int cpus_per_row = 8;
    for (int i = 0; i < num_cpus && i < 64; ++i) {
      if (i > 0 && i % cpus_per_row != 0) ImGui::SameLine();
      bool checked = (my_state.affinity_edit_mask & (1ULL << i)) != 0;
      char label[16];
      snprintf(label, sizeof(label), "CPU %d", i);
      if (ImGui::Checkbox(label, &checked)) {
        if (checked)
          my_state.affinity_edit_mask |= (1ULL << i);
        else
          my_state.affinity_edit_mask &= ~(1ULL << i);
      }
    }
    ImGui::Separator();

    if (ImGui::Button("Apply")) {
      if (set_process_affinity(my_state.control_edit_pid,
                               my_state.affinity_edit_mask,
                               my_state.process_error,
                               sizeof(my_state.process_error),
                               &my_state.process_error_code)) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

static void priority_popup_draw(BriefTableState &my_state) {
  if (my_state.show_priority_popup) {
    ImGui::OpenPopup("Set Process Priority");
    my_state.show_priority_popup = false;
  }
  if (ImGui::BeginPopupModal("Set Process Priority", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::Text("PID: %d", my_state.control_edit_pid);
    ImGui::Separator();

    ImGui::Text("Nice value (-20 = highest, +19 = lowest):");
    ImGui::SliderInt("##nice", &my_state.priority_edit_nice, -20, 19);

    if (my_state.priority_edit_nice < 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 180, 0, 255));
      ImGui::Text("Warning: Requires root privileges");
      ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (ImGui::Button("Apply")) {
      if (set_process_nice(my_state.control_edit_pid, my_state.priority_edit_nice,
                           my_state.process_error,
                           sizeof(my_state.process_error),
                           &my_state.process_error_code)) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

static void process_error_popup_draw(BriefTableState &my_state) {
  if (my_state.process_error[0] != '\0') {
    ImGui::OpenPopup("Process Error");
  }
  if (ImGui::BeginPopupModal("Process Error", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      my_state.process_error[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::Text("%s", my_state.process_error);
    if (my_state.process_error_code == EACCES) {
      if (ImGui::Button("Restart with pkexec")) {
        restart_with_pkexec();
      }
      ImGui::SameLine();
    }
    if (ImGui::Button("OK")) {
      my_state.process_error[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
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
  const int num_cpus = static_cast<int>(state.snapshot.cpu_stats.size) - 1;
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

    const bool filter_active = filter.IsActive();
    if (filter_active) {
      compute_filter_visibility(my_state, filter);
    }

    for (size_t i = 0; i < my_state.lines.size; ++i) {
      const BriefTableLine &line = my_state.lines.data[i];
      const bool is_dead = line.death_time_ns != 0;
      const bool is_new =
          !is_dead && now_ns - line.first_seen_ns < NEW_PROCESS_HIGHLIGHT_NS;

      char label[32];
      snprintf(label, sizeof(label), "%d", line.pid);

      // Skip hidden processes (filter_state computed for both tree and flat modes)
      if (filter_active && line.filter_state == 0) continue;

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

      // Gray out ancestor processes that don't match filter but have matching
      // descendants
      const bool is_grayed = filter_active && line.filter_state == 2;
      if (is_grayed) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      }

      if (my_state.tree_mode) {
        // Check if this node has children (next line has greater depth and
        // visible)
        bool has_children = false;
        if (i + 1 < my_state.lines.size) {
          // In filtered mode, we need to check for visible children
          for (size_t j = i + 1; j < my_state.lines.size; ++j) {
            const BriefTableLine &next = my_state.lines.data[j];
            if (next.tree_depth <= line.tree_depth) break;
            if (!filter_active || next.filter_state != 0) {
              has_children = true;
              break;
            }
          }
        }

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
        table_context_menu_draw(ctx, view_state, my_state, line, label, num_cpus);
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

        table_context_menu_draw(ctx, view_state, my_state, line, label, num_cpus);
        data_columns_draw(line);
      }

      if (is_grayed) {
        ImGui::PopStyleColor();
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

  // Draw popups for affinity/priority controls
  affinity_popup_draw(my_state, num_cpus);
  priority_popup_draw(my_state);
  process_error_popup_draw(my_state);

  // Show error popup if there's an error
  if (my_state.kill_error[0] != '\0') {
    ImGui::OpenPopup("Kill Error");
  }
  if (ImGui::BeginPopupModal("Kill Error", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      my_state.kill_error[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::Text("%s", my_state.kill_error);
    if (ImGui::Button("OK")) {
      my_state.kill_error[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
}
