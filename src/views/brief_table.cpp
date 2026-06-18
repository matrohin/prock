#include "brief_table.h"

#include "views/common.h"
#include "views/cpu_chart.h"
#include "views/environ_viewer.h"
#include "views/icons.h"
#include "views/io_chart.h"
#include "views/library_viewer.h"
#include "views/mem_chart.h"
#include "views/smaps_viewer.h"
#include "views/socket_viewer.h"
#include "views/threads_viewer.h"
#include "views/view_state.h"

#include "sources/sync.h"
#include "state.h"
#include "themes.h"

#include "imgui_internal.h"
#include "table_item.h"
#include "tracy/Tracy.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

// Highlight durations (must match brief_table_logic.cpp)
static constexpr int64_t NEW_PROCESS_HIGHLIGHT_NS = 2'000'000'000; // 2 seconds
static constexpr int64_t TYPE_SEARCH_TIMEOUT_NS = 1'000'000'000;   // 1 second

static const char *CPU_AFFINITY_TITLE = "Set CPU Affinity";
static const char *PROCESS_PRIORITY_TITLE = "Set Process Priority";

enum FilterResult {
  FilterResult_NoMatch = 0,
  FilterResult_Match = 1,
  FilterResult_SubtreeMatch = 2, // Token started with '+'
};

static FilterResult imgui_filter_pass_filter_ext(const ImGuiTextFilter &filter,
                                                 const char *text) {
  if (filter.Filters.empty()) return FilterResult_Match;

  if (text == nullptr) text = "";

  FilterResult result = FilterResult_NoMatch;
  for (int i = 0; i < filter.Filters.Size; i++) {
    const ImGuiTextFilter::ImGuiTextRange &f = filter.Filters[i];
    if (f.empty()) continue;

    const char *filter_begin = f.b;
    const char *filter_end = f.e;
    bool is_subtree = false;

    // Check for '+' prefix
    if (filter_begin < filter_end && *filter_begin == '+') {
      is_subtree = true;
      filter_begin++;
    }

    if (filter_begin >= filter_end) continue;

    // Exclusion filter (-)
    if (*filter_begin == '-') {
      if (ImStristr(text, nullptr, filter_begin + 1, filter_end) != nullptr)
        return FilterResult_NoMatch;
      continue;
    }

    // Grep filter
    if (ImStristr(text, nullptr, filter_begin, filter_end) != nullptr) {
      if (is_subtree) return FilterResult_SubtreeMatch;
      result = FilterResult_Match;
    }
  }
  return filter.CountGrep > 0 ? result : FilterResult_Match;
}

const char *PROCESS_COPY_HEADER =
    "PID\tName\tState\tThreads\tCPU Total\tCPU User\tCPU Kernel\tRSS "
    "(KB)\tVirt (KB)\tI/O Read (KB/s)\tI/O Write (KB/s)\tCommand Line\n";

static void open_all_windows(const Pid pid, const char *comm,
                             ViewState &view_state) {
  const ImGuiID dock_id =
      process_host_open(view_state.process_host_state, pid, comm);
  if (dock_id == 0) return;
  constexpr ProcessWindowFlags no_focus =
      eProcessWindowFlags_NoFocusOnAppearing;
  cpu_chart_add(view_state.cpu_chart_state, pid, comm, dock_id);
  mem_chart_add(view_state.mem_chart_state, pid, comm, dock_id, no_focus);
  io_chart_add(view_state.io_chart_state, pid, comm, dock_id, no_focus);
  library_viewer_request(view_state.library_viewer_state, *view_state.sync, pid,
                         comm, dock_id, no_focus);
  environ_viewer_request(view_state.environ_viewer_state, *view_state.sync, pid,
                         comm, dock_id, no_focus);
  threads_viewer_open(view_state.threads_viewer_state, *view_state.sync, pid,
                      comm, dock_id, no_focus);
  socket_viewer_request(view_state.socket_viewer_state, *view_state.sync, pid,
                        comm, dock_id, no_focus);
  smaps_viewer_request(view_state.smaps_viewer_state, *view_state.sync, pid,
                       comm, dock_id, no_focus);
}

static String process_cell_text(BumpArena &arena, const BriefTableLine &line,
                                const int column) {
  const ProcessDerivedStat &derived = line.derived_stat;
  switch (column) {
  case eBriefTableColumnId_Pid:
    return String::sprintf(arena, "%d", line.pid);
  case eBriefTableColumnId_Name:
    return String::static_string(line.name.data);
  case eBriefTableColumnId_State:
    return String::sprintf(arena, "%c", line.state);
  case eBriefTableColumnId_Threads:
    return String::sprintf(arena, "%ld", line.num_threads);
  case eBriefTableColumnId_CpuTotalPerc:
    return String::sprintf(arena, "%.1f",
                           derived.cpu_user_perc + derived.cpu_kernel_perc);
  case eBriefTableColumnId_CpuUserPerc:
    return String::sprintf(arena, "%.1f", derived.cpu_user_perc);
  case eBriefTableColumnId_CpuKernelPerc:
    return String::sprintf(arena, "%.1f", derived.cpu_kernel_perc);
  case eBriefTableColumnId_MemRssBytes:
    return String::sprintf(arena, "%.0f", derived.mem_resident_bytes / 1024.0);
  case eBriefTableColumnId_MemVirtBytes:
    return String::sprintf(arena, "%.0f", derived.mem_virtual_bytes / 1024.0);
  case eBriefTableColumnId_IoReadKbPerSec:
    return String::sprintf(arena, "%.1f", derived.io_read_kb_per_sec);
  case eBriefTableColumnId_IoWriteKbPerSec:
    return String::sprintf(arena, "%.1f", derived.io_write_kb_per_sec);
  case eBriefTableColumnId_CmdLine:
    return String::static_string(line.cmdline);
  default:
    return String::static_string("");
  }
}

static void copy_process_row(Notifications &notifications, BumpArena &arena,
                             const BriefTableLine &line) {
  const ProcessDerivedStat &derived = line.derived_stat;
  const String str = String::sprintf(
      arena, "%s%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f\t%s",
      PROCESS_COPY_HEADER, line.pid, line.name.data, line.state,
      line.num_threads, derived.cpu_user_perc + derived.cpu_kernel_perc,
      derived.cpu_user_perc, derived.cpu_kernel_perc,
      derived.mem_resident_bytes / 1024.0, derived.mem_virtual_bytes / 1024.0,
      derived.io_read_kb_per_sec, derived.io_write_kb_per_sec, line.cmdline);
  clipboard_copy_row(notifications, str.data);
}

static bool get_process_affinity(const Pid pid, uint64_t &mask,
                                 const int num_cpus) {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  if (sched_getaffinity(pid, sizeof(cpu_set), &cpu_set) != 0) return false;
  mask = 0;
  for (int i = 0; i < num_cpus && i < 64; ++i) {
    if (CPU_ISSET(i, &cpu_set)) mask |= (1ULL << i);
  }
  return true;
}

static bool set_process_affinity(Notifications &notifications, const Pid pid,
                                 const uint64_t mask) {
  if (mask == 0) {
    notify_error(notifications, 0, "At least one CPU must be selected");
    return false;
  }
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  for (int i = 0; i < 64; ++i) {
    if (mask & (1ULL << i)) CPU_SET(i, &cpu_set);
  }
  if (sched_setaffinity(pid, sizeof(cpu_set), &cpu_set) != 0) {
    const int err = errno;
    notify_error(notifications, err, "Failed to set affinity for PID %d: %s",
                 pid, strerror(err));
    return false;
  }
  return true;
}

static int get_process_nice(const Pid pid) {
  errno = 0;
  const int nice = getpriority(PRIO_PROCESS, pid);
  return nice == -1 && errno != 0 ? 0 : nice;
}

static bool set_process_nice(Notifications &notifications, const Pid pid,
                             const int nice_val) {
  if (setpriority(PRIO_PROCESS, pid, nice_val) != 0) {
    const int err = errno;
    notify_error(notifications, err, "Failed to set priority for PID %d: %s",
                 pid, strerror(err));
    return false;
  }
  return true;
}

static void copy_all_processes(Notifications &notifications, BumpArena &arena,
                               const BriefTableState &my_state) {
  // name and cmdline are each bounded by the 4 KB /proc/[pid]/cmdline buffer.
  copy_all_to_clipboard(
      notifications, arena, my_state.lines.data, my_state.lines.size,
      2 * 4096 + 256, PROCESS_COPY_HEADER,
      [](char *ptr, size_t rem, const BriefTableLine &line) {
        const ProcessDerivedStat &derived = line.derived_stat;
        return snprintf(
            ptr, rem,
            "%d\t%s\t%c\t%ld\t%.1f\t%.1f\t%.1f\t%.0f\t%.0f\t%.1f\t%.1f\t%s\n",
            line.pid, line.name.data, line.state, line.num_threads,
            derived.cpu_user_perc + derived.cpu_kernel_perc,
            derived.cpu_user_perc, derived.cpu_kernel_perc,
            derived.mem_resident_bytes / 1024.0,
            derived.mem_virtual_bytes / 1024.0, derived.io_read_kb_per_sec,
            derived.io_write_kb_per_sec, line.cmdline);
      });
}

// Ask the on-demand reader to run gcore. dump_dir is the configured folder;
// when empty (the user cleared the preference) it falls back to
// default_dump_dir(). The dump lands at "<out_path>.<pid>" because gcore
// appends the pid to its -o base.
static void send_dump_request(Sync &sync, const char *dump_dir, const Pid pid,
                              const char *comm) {
  char default_dir[512];
  if (!dump_dir || dump_dir[0] == '\0') {
    default_dump_dir(default_dir, sizeof(default_dir));
    dump_dir = default_dir;
  }

  char safe_comm[64];
  uint32_t si = 0;
  for (const char *c = comm; c && *c && si < sizeof(safe_comm) - 1; ++c) {
    const char ch = *c;
    safe_comm[si++] =
        (ch == '/' || ch == ' ' || ch == '\t' || ch == ':') ? '_' : ch;
  }
  safe_comm[si] = '\0';

  char timestamp[32];
  const time_t now = time(nullptr);
  struct tm tm_now;
  localtime_r(&now, &tm_now);
  strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm_now);

  DumpRequest req = {};
  req.pid = pid;
  snprintf(req.out_path, sizeof(req.out_path), "%s/core.%s.%s", dump_dir,
           safe_comm, timestamp);

  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.on_demand_reader.dump_request_queue.push(req);
  }
  sync.on_demand_reader.request_read_cv.notify_one();
}

// Drain gcore results pushed by the on-demand reader and report each as a
// toast.
void brief_table_dump_update(Notifications &notifications, Sync &sync) {
  DumpResponse r;
  while (sync.on_demand_reader.dump_response_queue.pop(r)) {
    if (r.error_code == 0 && !r.gcore_missing && r.exit_status == 0) {
      notify_info(notifications, "Wrote core to %s.%d", r.out_path, r.pid);
    } else if (r.gcore_missing) {
      notify_error(notifications, 0,
                   "gcore not found - install gdb to enable core dumps");
    } else if (r.error_code != 0) {
      notify_error(notifications, r.error_code, "Failed to dump %d: %s", r.pid,
                   strerror(r.error_code));
    } else {
      // gcore ran but failed: most likely ptrace permission. Offer pkexec when
      // we are not already root.
      notify_error(notifications, geteuid() == 0 ? 0 : EPERM,
                   "gcore failed for PID %d (exit %d)", r.pid, r.exit_status);
    }
  }
}

static void table_context_menu_draw(FrameContext &ctx, ViewState &view_state,
                                    BriefTableState &my_state,
                                    const BriefTableLine &line,
                                    const char *label, const int num_cpus) {
  const Pid pid = line.pid;
  const int copy_column = table_context_column(eBriefTableColumnId_Count);
  if (ImGui::BeginPopupContextItem(label)) {
    my_state.selected_pid = pid;
    const String cell = process_cell_text(ctx.frame_arena, line, copy_column);
    if (ImGui::MenuItemEx(copy_cell_menu_label(ctx.frame_arena, cell).data,
                          ICON_MD_CONTENT_COPY)) {
      clipboard_copy_cell(view_state.notifications, cell);
    }
    if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
      copy_process_row(view_state.notifications, ctx.frame_arena, line);
    }
    if (ImGui::MenuItem("Copy All")) {
      copy_all_processes(view_state.notifications, ctx.frame_arena, my_state);
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Filter to subtree", ICON_MD_FILTER_ALT)) {
      uint32_t len = static_cast<uint32_t>(strlen(my_state.filter_text));
      // Append comma if filter not empty
      if (len > 0 && len < sizeof(my_state.filter_text) - 2) {
        my_state.filter_text[len++] = ',';
      }
      snprintf(my_state.filter_text + len, sizeof(my_state.filter_text) - len,
               "+%s", line.name.data);
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Open All Windows", ICON_MD_OPEN_IN_NEW)) {
      open_all_windows(pid, line.name.data, view_state);
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("CPU Chart", ICON_MD_SHOW_CHART)) {
      cpu_chart_add(view_state.cpu_chart_state, pid, line.name.data);
    }
    if (ImGui::MenuItem("Memory Chart")) {
      mem_chart_add(view_state.mem_chart_state, pid, line.name.data);
    }
    if (ImGui::MenuItem("I/O Chart")) {
      io_chart_add(view_state.io_chart_state, pid, line.name.data);
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Loaded Libraries", ICON_MD_SEARCH)) {
      library_viewer_request(view_state.library_viewer_state, *view_state.sync,
                             pid, line.name.data);
    }
    if (ImGui::MenuItem("Environment")) {
      environ_viewer_request(view_state.environ_viewer_state, *view_state.sync,
                             pid, line.name.data);
    }
    if (ImGui::MenuItem("Threads")) {
      threads_viewer_open(view_state.threads_viewer_state, *view_state.sync,
                          pid, line.name.data);
    }
    if (ImGui::MenuItem("Sockets")) {
      socket_viewer_request(view_state.socket_viewer_state, *view_state.sync,
                            pid, line.name.data);
    }
    if (ImGui::MenuItem("Memory Maps")) {
      smaps_viewer_request(view_state.smaps_viewer_state, *view_state.sync, pid,
                           line.name.data);
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Set Affinity...", ICON_MD_SETTINGS)) {
      my_state.control_edit_pid = pid;
      get_process_affinity(pid, my_state.affinity_edit_mask, num_cpus);
      my_state.show_affinity_popup = true;
    }
    if (ImGui::MenuItem("Set Priority...")) {
      my_state.control_edit_pid = pid;
      my_state.priority_edit_nice = get_process_nice(pid);
      my_state.show_priority_popup = true;
    }
    if (ImGui::MenuItem("Suspend Process")) {
      if (kill(pid, SIGSTOP) != 0) {
        const int err = errno;
        notify_error(view_state.notifications, err, "Failed to suspend %d: %s",
                     pid, strerror(err));
      }
    }
    if (ImGui::MenuItem("Resume Process")) {
      if (kill(pid, SIGCONT) != 0) {
        const int err = errno;
        notify_error(view_state.notifications, err, "Failed to resume %d: %s",
                     pid, strerror(err));
      }
    }
    if (ImGui::MenuItem("Create Dump File")) {
      send_dump_request(*view_state.sync, view_state.preferences_state.dump_dir,
                        pid, line.name.data);
      ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Kill Process", ICON_MD_DELETE, "Del") ||
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
      if (kill(pid, SIGTERM) != 0) {
        const int err = errno;
        notify_error(view_state.notifications, err, "Failed to kill %d: %s",
                     pid, strerror(err));
      }
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::MenuItem("Force Kill")) {
      if (kill(pid, SIGKILL) != 0) {
        const int err = errno;
        notify_error(view_state.notifications, err, "Failed to kill %d: %s",
                     pid, strerror(err));
      }
    }
    if (ImGui::MenuItem("Kill Process Tree")) {
      // Collect all descendant PIDs by walking ppid relationships
      Pid tree_pids[4096];
      int tree_count = 0;
      tree_pids[tree_count++] = pid;
      for (int ti = 0; ti < tree_count; ++ti) {
        const Pid parent = tree_pids[ti];
        for (uint32_t li = 0; li < my_state.lines.size; ++li) {
          const BriefTableLine &l = my_state.lines.data[li];
          if (l.ppid == parent && l.pid != parent &&
              tree_count < (int)(sizeof(tree_pids) / sizeof(tree_pids[0]))) {
            tree_pids[tree_count++] = l.pid;
          }
        }
      }
      // Kill in reverse order (children first, parent last).
      for (int ti = tree_count - 1; ti >= 0; --ti) {
        if (kill(tree_pids[ti], SIGKILL) != 0) {
          const int err = errno;
          notify_error(view_state.notifications, err, "Failed to kill %d: %s",
                       tree_pids[ti], strerror(err));
        }
      }
    }
    ImGui::EndPopup();
  }
}

static void compute_filter_visibility(FrameContext &ctx,
                                      const BriefTableState &my_state,
                                      const ImGuiTextFilter &filter) {
  // First pass: mark direct matches
  for (BriefTableLine &line : my_state.lines) {
    const String pid_str = String::sprintf(ctx.frame_arena, "%d", line.pid);

    FilterResult name_result =
        imgui_filter_pass_filter_ext(filter, line.name.data);
    FilterResult pid_result =
        imgui_filter_pass_filter_ext(filter, pid_str.data);

    // Take the "better" result (SubtreeMatch > Match > NoMatch)
    const FilterResult result =
        name_result > pid_result ? name_result : pid_result;

    if (result == FilterResult_SubtreeMatch)
      line.filter_state = 3; // Subtree root
    else if (result == FilterResult_Match)
      line.filter_state = 1; // Regular match
    else
      line.filter_state = 0; // Hidden
  }

  // Second pass: propagate subtree visibility to descendants
  bool changed = true;
  while (changed) {
    changed = false;
    for (BriefTableLine &line : my_state.lines) {
      if (line.filter_state != 0) continue;

      // Check if parent is a subtree member
      for (uint32_t j = 0; j < my_state.lines.size; ++j) {
        if (my_state.lines.data[j].pid == line.ppid &&
            my_state.lines.data[j].filter_state == 3) {
          line.filter_state = 3; // Subtree member
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
    for (uint32_t i = my_state.lines.size; i-- > 0;) {
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

// Right-aligned I/O rate cell with dynamic units (B/s..GB/s). Reuses the chart
// axis formatter so the table and charts ladder units identically.
static void draw_io_rate(const double kb_per_sec) {
  char buf[32];
  format_io_rate_kb(kb_per_sec, buf, sizeof(buf), nullptr);
  ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", buf);
}

static void data_columns_draw(const BriefTableLine &line, const int num_cpus,
                              const bool cpu_per_core) {
  const ProcessDerivedStat &derived_stat = line.derived_stat;
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Name)) {
    ImGui::TextUnformatted(line.name.data);
    if (line.cmdline[0] != '\0' && ImGui::IsItemHovered())
      ImGui::SetItemTooltip("%s", line.cmdline);
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_State)) {
    table_item_draw_state(line.state);
  }
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_Threads))
    table_item_draw_long(line.num_threads);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuTotalPerc))
    table_item_draw_percent(scale_cpu_perc(derived_stat.cpu_user_perc +
                                               derived_stat.cpu_kernel_perc,
                                           num_cpus, cpu_per_core));
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuUserPerc))
    table_item_draw_percent(
        scale_cpu_perc(derived_stat.cpu_user_perc, num_cpus, cpu_per_core));
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CpuKernelPerc))
    table_item_draw_percent(
        scale_cpu_perc(derived_stat.cpu_kernel_perc, num_cpus, cpu_per_core));
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemRssBytes))
    table_item_draw_memory(derived_stat.mem_resident_bytes);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_MemVirtBytes))
    table_item_draw_memory(derived_stat.mem_virtual_bytes);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoReadKbPerSec))
    draw_io_rate(derived_stat.io_read_kb_per_sec);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_IoWriteKbPerSec))
    draw_io_rate(derived_stat.io_write_kb_per_sec);
  if (ImGui::TableSetColumnIndex(eBriefTableColumnId_CmdLine)) {
    ImGui::TextUnformatted(line.cmdline);
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("%s", line.cmdline);
  }
}

static void affinity_popup_draw(FrameContext &ctx, BriefTableState &my_state,
                                Notifications &notifications,
                                const int num_cpus) {
  if (my_state.show_affinity_popup) {
    ImGui::OpenPopup(CPU_AFFINITY_TITLE);
    my_state.show_affinity_popup = false;
  }
  if (ImGui::BeginPopupModal(CPU_AFFINITY_TITLE, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    popup_close_on_escape();
    ImGui::Text("PID: %d", my_state.control_edit_pid);
    ImGui::Separator();

    if (ImGui::Button("Select All")) {
      my_state.affinity_edit_mask =
          (num_cpus >= 64) ? ~0ULL : ((1ULL << num_cpus) - 1);
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
      const String label = String::sprintf(ctx.frame_arena, "CPU %d", i);
      if (ImGui::Checkbox(label.data, &checked)) {
        if (checked)
          my_state.affinity_edit_mask |= (1ULL << i);
        else
          my_state.affinity_edit_mask &= ~(1ULL << i);
      }
    }
    ImGui::Separator();

    if (ImGui::Button("Apply")) {
      if (set_process_affinity(notifications, my_state.control_edit_pid,
                               my_state.affinity_edit_mask)) {
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

static void priority_popup_draw(BriefTableState &my_state,
                                Notifications &notifications) {
  if (my_state.show_priority_popup) {
    ImGui::OpenPopup(PROCESS_PRIORITY_TITLE);
    my_state.show_priority_popup = false;
  }
  if (ImGui::BeginPopupModal(PROCESS_PRIORITY_TITLE, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    popup_close_on_escape();
    ImGui::Text("PID: %d", my_state.control_edit_pid);
    ImGui::Separator();

    ImGui::Text("Nice value (-20 = highest, +19 = lowest):");
    ImGui::SliderInt("##nice", &my_state.priority_edit_nice, -20, 19);

    if (my_state.priority_edit_nice < 0) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            app_color_u32(eAppColor_WarningText));
      ImGui::Text("Warning: Requires root privileges");
      ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (ImGui::Button("Apply")) {
      if (set_process_nice(notifications, my_state.control_edit_pid,
                           my_state.priority_edit_nice)) {
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

static Array<int> compute_visible_indices(FrameContext &ctx,
                                          const BriefTableState &my_state,
                                          const bool filter_active,
                                          BumpArena &arena) {
  Array<int> buf = Array<int>::create(arena, my_state.lines.size);
  int count = 0;

  if (!my_state.tree_mode) {
    // Flat mode: skip only filtered rows
    for (uint32_t i = 0; i < my_state.lines.size; ++i) {
      if (filter_active && my_state.lines.data[i].filter_state == 0) continue;
      buf.data[count++] = static_cast<int>(i);
    }
  } else {
    // Tree mode: skip filtered rows AND children of collapsed nodes
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    ImGuiStorage *storage = window->DC.StateStorage;
    int collapsed_at_depth = -1;

    for (uint32_t i = 0; i < my_state.lines.size; ++i) {
      const BriefTableLine &line = my_state.lines.data[i];

      if (filter_active && line.filter_state == 0) continue;

      // If we've returned to or above a collapsed node's depth, stop skipping
      if (collapsed_at_depth >= 0 && line.tree_depth <= collapsed_at_depth) {
        collapsed_at_depth = -1;
      }

      // Skip children of collapsed nodes
      if (collapsed_at_depth >= 0 && line.tree_depth > collapsed_at_depth) {
        continue;
      }

      buf.data[count++] = static_cast<int>(i);

      // Check if this node has visible children
      bool has_children = false;
      for (uint32_t j = i + 1; j < my_state.lines.size; ++j) {
        const BriefTableLine &next = my_state.lines.data[j];
        if (next.tree_depth <= line.tree_depth) break;
        if (!filter_active || next.filter_state != 0) {
          has_children = true;
          break;
        }
      }

      if (has_children) {
        // Check ImGui storage for open/close state (DefaultOpen = 1)
        const String pid_str = String::sprintf(ctx.frame_arena, "%d", line.pid);
        const ImGuiID id = ImGui::GetID(pid_str.data);
        const bool is_open = storage->GetInt(id, 1) != 0;
        if (!is_open) {
          collapsed_at_depth = line.tree_depth;
        }
      }
    }
  }

  return Array<int>{buf.data, static_cast<uint32_t>(count)};
}

void brief_table_draw(FrameContext &ctx, ViewState &view_state,
                      const State &state) {
  ZoneScoped;
  BriefTableState &my_state = view_state.brief_table_state;
  int focus_scroll_to_idx = -1;

  const String title = String::sprintf(
      ctx.frame_arena, "Process Table (%u processes)###ProcessTable",
      my_state.lines.size);
  // Bring the table to front (selecting its tab if docked, uncollapsing it)
  // before Begin, otherwise a hidden window skips its body and the filter
  // input below is never submitted to receive focus.
  if (my_state.focus_filter_requested) {
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowCollapsed(false);
  }
  ImGui::Begin(title.data, nullptr, COMMON_VIEW_FLAGS);

  if (my_state.focus_filter_requested) {
    ImGui::SetKeyboardFocusHere(); // targets the filter input below
    my_state.focus_filter_requested = false;
  }

  const int num_cpus = static_cast<int>(state.snapshot.cpu_stats.size) - 1;
  const bool cpu_per_core = view_state.preferences_state.cpu_per_core;
  bool reset_sort_to_pid = false;

  ImGuiTextFilter filter;
  if (ImGui::BeginTable("Header", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch,
                            HEADER_SPACER_WEIGHT);
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    filter = draw_filter_input("##ProcessFilter", my_state.filter_text,
                               sizeof(my_state.filter_text));

    ImGui::TableNextColumn();
    if (ImGui::Checkbox("Tree", &my_state.tree_mode) && my_state.tree_mode) {
      // Reset to PID sorting when entering tree mode
      my_state.sorted_by = eBriefTableColumnId_Pid;
      my_state.sorted_order = ImGuiSortDirection_Ascending;
      reset_sort_to_pid = true;
      sort_brief_table_tree(my_state, ctx.frame_arena);
    }

    ImGui::TableNextColumn(); // spacer

    ImGui::EndTable();
  }

  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
      ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable |
      ImGuiTableFlags_Sortable | ImGuiTableFlags_Borders |
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_HighlightHoveredColumn;
  if (ImGui::BeginTable("Processes", eBriefTableColumnId_Count, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_NoHide, 0.0f,
                            eBriefTableColumnId_Pid);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f,
                            eBriefTableColumnId_Name);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50.0f,
                            eBriefTableColumnId_State);
    ImGui::TableSetupColumn("Threads",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide |
                                ImGuiTableColumnFlags_WidthFixed,
                            60.0f, eBriefTableColumnId_Threads);
    ImGui::TableSetupColumn("CPU Total",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_WidthFixed,
                            85.0f, eBriefTableColumnId_CpuTotalPerc);
    ImGui::TableSetupColumn("CPU User",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide |
                                ImGuiTableColumnFlags_WidthFixed,
                            85.0f, eBriefTableColumnId_CpuUserPerc);
    ImGui::TableSetupColumn("CPU Kernel",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_WidthFixed,
                            90.0f, eBriefTableColumnId_CpuKernelPerc);
    ImGui::TableSetupColumn("RSS",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_WidthFixed,
                            90.0f, eBriefTableColumnId_MemRssBytes);
    ImGui::TableSetupColumn("Virtual Size",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_WidthFixed,
                            90.0f, eBriefTableColumnId_MemVirtBytes);
    ImGui::TableSetupColumn("I/O Read",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide |
                                ImGuiTableColumnFlags_WidthFixed,
                            85.0f, eBriefTableColumnId_IoReadKbPerSec);
    ImGui::TableSetupColumn("I/O Write",
                            ImGuiTableColumnFlags_PreferSortDescending |
                                ImGuiTableColumnFlags_DefaultHide |
                                ImGuiTableColumnFlags_WidthFixed,
                            85.0f, eBriefTableColumnId_IoWriteKbPerSec);
    ImGui::TableSetupColumn("Command Line", ImGuiTableColumnFlags_None, 0.0f,
                            eBriefTableColumnId_CmdLine);
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

    const bool filter_active = filter.IsActive();
    if (filter_active) {
      compute_filter_visibility(ctx, my_state, filter);
    }

    const Array<int> visible_indices =
        compute_visible_indices(ctx, my_state, filter_active, ctx.frame_arena);

    // Type-to-search: handle keyboard input when table is focused
    if (!ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
      // Reset search after timeout
      if (now_ns - my_state.type_search_time_ns > TYPE_SEARCH_TIMEOUT_NS) {
        my_state.type_search[0] = '\0';
      }
      // Capture typed characters
      ImGuiIO &io = ImGui::GetIO();
      for (int n = 0; n < io.InputQueueCharacters.Size; ++n) {
        const ImWchar c = io.InputQueueCharacters[n];
        if (c >= 32 && c < 127) { // Printable ASCII
          const uint32_t len =
              static_cast<uint32_t>(strlen(my_state.type_search));
          if (len < sizeof(my_state.type_search) - 1) {
            my_state.type_search[len] = static_cast<char>(c);
            my_state.type_search[len + 1] = '\0';
            my_state.type_search_time_ns = now_ns;
            // Find current selection's index
            int current_idx = 0;
            for (uint32_t j = 0; j < my_state.lines.size; ++j) {
              if (my_state.lines.data[j].pid == my_state.selected_pid) {
                current_idx = static_cast<int>(j);
                break;
              }
            }
            // Search starting from next position (if first char) or current (if
            // refining) First char: start from next to find something different
            // from current selection Subsequent chars: start from current to
            // refine the existing match
            const uint32_t total = my_state.lines.size;
            if (len == 0) current_idx += 1;
            // total > 0 guards the % total below (table can be empty before the
            // first snapshot arrives while still holding keyboard focus).
            for (uint32_t offset = 0; total > 0 && offset <= total; ++offset) {
              const uint32_t j = (current_idx + offset) % total;
              const BriefTableLine &l = my_state.lines.data[j];
              if (filter_active && l.filter_state == 0) continue;
              if (strncasecmp(l.name.data, my_state.type_search, len + 1) ==
                  0) {
                focus_scroll_to_idx = static_cast<int>(j);
                my_state.selected_pid = l.pid;
                break;
              }
            }
          }
        }
      }
    }

    // Convert focus_scroll_to_idx (line index) to clipper index
    int focus_clipper_idx = -1;
    if (focus_scroll_to_idx >= 0) {
      for (uint32_t ci = 0; ci < visible_indices.size; ++ci) {
        if (visible_indices.data[ci] == focus_scroll_to_idx) {
          focus_clipper_idx = static_cast<int>(ci);
          break;
        }
      }
    }

    ImGuiWindow *window = ImGui::GetCurrentWindow();
    const float base_indent = window->DC.Indent.x;
    const float indent_spacing = ImGui::GetStyle().IndentSpacing;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_indices.size));
    if (focus_clipper_idx >= 0) clipper.IncludeItemByIndex(focus_clipper_idx);

    while (clipper.Step()) {
      for (int ci = clipper.DisplayStart; ci < clipper.DisplayEnd; ++ci) {
        const int i = visible_indices.data[ci];
        const BriefTableLine &line = my_state.lines.data[i];
        const bool is_dead = line.death_time_ns != 0;
        const bool is_new =
            !is_dead && now_ns - line.first_seen_ns < NEW_PROCESS_HIGHLIGHT_NS;

        const String label = String::sprintf(ctx.frame_arena, "%d", line.pid);

        // Set indent for tree mode before TableNextRow
        if (my_state.tree_mode) {
          window->DC.Indent.x = base_indent + line.tree_depth * indent_spacing;
        }

        ImGui::TableNextRow();

        // Apply row highlighting
        if (is_dead) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                 app_color_u32(eAppColor_DeadProcessRow));
        } else if (is_new) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                 app_color_u32(eAppColor_NewProcessRow));
        }

        const bool is_selected = my_state.selected_pid == line.pid;
        ImGui::TableSetColumnIndex(eBriefTableColumnId_Pid);

        // Gray out ancestor processes that don't match filter but have matching
        // descendants
        const bool is_grayed = filter_active && line.filter_state == 2;
        if (is_grayed) {
          ImGui::PushStyleColor(
              ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        if (ci == focus_clipper_idx) {
          ImGui::SetScrollHereY(0.5f);
          ImGui::SetKeyboardFocusHere(0);
        }

        if (my_state.tree_mode) {
          // Check if this node has children (scan original lines, not
          // visible_indices)
          bool has_children = false;
          for (uint32_t j = i + 1; j < my_state.lines.size; ++j) {
            const BriefTableLine &next = my_state.lines.data[j];
            if (next.tree_depth <= line.tree_depth) break;
            if (!filter_active || next.filter_state != 0) {
              has_children = true;
              break;
            }
          }

          ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
                                     ImGuiTreeNodeFlags_DefaultOpen |
                                     ImGuiTreeNodeFlags_OpenOnArrow |
                                     ImGuiTreeNodeFlags_NoTreePushOnOpen;
          if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf;
          if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;

          ImGui::TreeNodeEx(label.data, flags);

          if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            my_state.selected_pid = line.pid;
          }
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
              !ImGui::IsItemToggledOpen()) {
            open_all_windows(line.pid, line.name.data, view_state);
          }
          if (is_grayed) ImGui::PopStyleColor();
          table_context_menu_draw(ctx, view_state, my_state, line, label.data,
                                  num_cpus);
          if (is_grayed)
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          data_columns_draw(line, num_cpus, cpu_per_core);
        } else {
          if (ImGui::Selectable(label.data, is_selected,
                                ImGuiSelectableFlags_SpanAllColumns) ||
              ImGui::IsItemFocused()) {
            my_state.selected_pid = line.pid;
          }

          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            open_all_windows(line.pid, line.name.data, view_state);
          }

          if (is_grayed) ImGui::PopStyleColor();
          table_context_menu_draw(ctx, view_state, my_state, line, label.data,
                                  num_cpus);
          if (is_grayed)
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          data_columns_draw(line, num_cpus, cpu_per_core);
        }

        if (is_grayed) {
          ImGui::PopStyleColor();
        }
      }
    }

    // Restore indent after clipper loop
    window->DC.Indent.x = base_indent;

    ImGui::EndTable();
  }

  // Shortcuts for per-process actions:
  if (my_state.selected_pid > 0) {
    // Ctrl+C to copy selected row
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
      for (BriefTableLine &line : my_state.lines) {
        if (line.pid == my_state.selected_pid) {
          copy_process_row(view_state.notifications, ctx.frame_arena, line);
          break;
        }
      }
    }

    // Del key to kill selected process
    if (ImGui::Shortcut(ImGuiKey_Delete)) {
      if (kill(my_state.selected_pid, SIGTERM) != 0) {
        const int err = errno;
        notify_error(view_state.notifications, err, "Failed to kill %d: %s",
                     my_state.selected_pid, strerror(err));
      }
    }
  }

  // Draw popups for affinity/priority controls
  affinity_popup_draw(ctx, my_state, view_state.notifications, num_cpus);
  priority_popup_draw(my_state, view_state.notifications);

  ImGui::End();
}
