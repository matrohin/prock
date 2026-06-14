#include "threads_viewer.h"

#include "state.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/process_window_flags.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "table_item.h"
#include "tracy/Tracy.hpp"

#include <cstring>

const char *THREAD_COPY_HEADER =
    "TID\tName\tState\tCPU Total\tCPU Kernel\tMemory\n";

static String thread_cell_text(BumpArena &arena, const ThreadLine &line,
                               const int column) {
  switch (column) {
  case eThreadsViewerColumnId_Tid:
    return String::sprintf(arena, "%d", line.tid);
  case eThreadsViewerColumnId_Name:
    return String::static_string(line.comm);
  case eThreadsViewerColumnId_State:
    return String::sprintf(arena, "%c", line.state);
  case eThreadsViewerColumnId_CpuTotal:
    return String::sprintf(arena, "%.1f",
                           line.cpu_user_perc + line.cpu_kernel_perc);
  case eThreadsViewerColumnId_CpuKernel:
    return String::sprintf(arena, "%.1f", line.cpu_kernel_perc);
  case eThreadsViewerColumnId_Memory:
    return String::sprintf(arena, "%ld", line.mem_resident_bytes);
  default:
    return String::static_string("");
  }
}

static void copy_thread_row(Notifications &notifications,
                            const ThreadLine &line) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s%d\t%s\t%c\t%.1f\t%.1f\t%ld",
           THREAD_COPY_HEADER, line.tid, line.comm, line.state,
           line.cpu_user_perc + line.cpu_kernel_perc, line.cpu_kernel_perc,
           line.mem_resident_bytes);
  clipboard_copy_row(notifications, buf);
}

static void copy_all_threads(Notifications &notifications, BumpArena &arena,
                             const ThreadsViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.lines.data, win.lines.size, 256,
      THREAD_COPY_HEADER, [](char *ptr, size_t rem, const ThreadLine &line) {
        return snprintf(ptr, rem, "%d\t%s\t%c\t%.1f\t%.1f\t%ld\n", line.tid,
                        line.comm, line.state,
                        line.cpu_user_perc + line.cpu_kernel_perc,
                        line.cpu_kernel_perc, line.mem_resident_bytes);
      });
}

static bool thread_line_is_less(const ThreadsViewerColumnId sorted_by,
                                const ThreadLine &a, const ThreadLine &b) {
  switch (sorted_by) {
  case eThreadsViewerColumnId_Tid:
    return a.tid < b.tid;
  case eThreadsViewerColumnId_Name:
    return strcmp(a.comm, b.comm) < 0;
  case eThreadsViewerColumnId_State:
    return a.state < b.state;
  case eThreadsViewerColumnId_CpuTotal:
    return a.cpu_user_perc + a.cpu_kernel_perc <
           b.cpu_user_perc + b.cpu_kernel_perc;
  case eThreadsViewerColumnId_CpuKernel:
    return a.cpu_kernel_perc < b.cpu_kernel_perc;
  case eThreadsViewerColumnId_Memory:
    return a.mem_resident_bytes < b.mem_resident_bytes;
  default:
    return false;
  }
}

static void sort_thread_lines(ThreadsViewerWindow &win) {
  sort_bidirectional(
      win.lines.data, win.lines.size, win.sorted_order,
      [sorted_by = win.sorted_by](const ThreadLine &a, const ThreadLine &b) {
        return thread_line_is_less(sorted_by, a, b);
      });
}

// Check if any other window still needs this PID watched. The closing window
// sits at index `survivors` and its pre-compaction copy at `cur`, so only the
// compacted head [0, survivors) and the untouched tail (cur, size) are
// distinct.
static bool pid_still_needed(const ThreadsViewerState &state, const Pid pid,
                             const uint32_t survivors, const uint32_t cur) {
  const ThreadsViewerWindow *data = state.windows.data();
  for (uint32_t i = 0; i < survivors; ++i)
    if (data[i].pid == pid) return true;
  for (uint32_t i = cur + 1; i < state.windows.size(); ++i)
    if (data[i].pid == pid) return true;
  return false;
}

void threads_viewer_open(ThreadsViewerState &state, Sync &sync, const Pid pid,
                         const char *comm, const ImGuiID dock_id,
                         const ProcessWindowFlags extra_flags) {
  if (process_window_focus(state.windows, pid)) {
    return;
  }

  sync.thread_watch_queue.push(pid);

  ThreadsViewerWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  win->status = eOnDemandViewerStatus_Loading;
  snprintf(win->process_name, sizeof(win->process_name), "%s", comm);
  win->selected_tid = -1;
  win->sorted_by = eThreadsViewerColumnId_CpuTotal;
  win->sorted_order = ImGuiSortDirection_Descending;

  common_views_sort_added(state.windows);
}

void threads_viewer_update(ThreadsViewerState &state, const State &state_data) {
  ZoneScoped;

  const long page_size = state_data.system.mem_page_size;
  const double ticks_in_second = state_data.system.ticks_in_second;

  // Process thread snapshots from the current update
  const Array<ThreadSnapshot> &snapshots = state_data.snapshot.thread_snapshots;

  for (uint32_t w = 0; w < state.windows.size(); ++w) {
    ThreadsViewerWindow &win = state.windows.data()[w];

    // Find matching snapshot
    const ThreadSnapshot *snap = nullptr;
    for (uint32_t s = 0; s < snapshots.size; ++s) {
      if (snapshots.data[s].pid == win.pid) {
        snap = &snapshots.data[s];
        break;
      }
    }

    if (!snap || snap->threads.size == 0) {
      continue;
    }

    win.status = eOnDemandViewerStatus_Ready;

    // Account for wasted memory from old arrays
    state.wasted_bytes += win.lines.size * sizeof(ThreadLine) +
                          win.prev_threads.size * sizeof(ThreadCpuSample);

    SteadyTimePoint prev_at{SteadyClock::duration{win.prev_at_ns}};
    const Array<ThreadCpuSample> prev_threads = win.prev_threads;

    // Build ThreadLine array from snapshot
    win.lines = Array<ThreadLine>::create(state.cur_arena, snap->threads.size);

    const SteadyTimePoint now = state_data.snapshot.at;
    const double time_delta =
        std::chrono::duration_cast<Seconds>(now - prev_at).count();
    const double ticks_passed = ticks_in_second * time_delta;

    uint32_t prev_idx = 0;
    for (uint32_t i = 0; i < snap->threads.size; ++i) {
      const ProcessStat &thread = snap->threads.data[i];
      ThreadLine &line = win.lines.data[i];

      line.tid = thread.pid;
      strncpy(line.comm, thread.comm, sizeof(line.comm) - 1);
      line.comm[sizeof(line.comm) - 1] = '\0';
      line.state = thread.state;
      line.mem_resident_bytes = thread.statm_resident * page_size;
      line.cpu_user_perc = 0;
      line.cpu_kernel_perc = 0;

      // Find matching previous thread for delta computation
      while (prev_idx < prev_threads.size &&
             prev_threads.data[prev_idx].pid < thread.pid) {
        ++prev_idx;
      }

      if (prev_idx < prev_threads.size &&
          prev_threads.data[prev_idx].pid == thread.pid && ticks_passed > 0) {
        const ThreadCpuSample &prev = prev_threads.data[prev_idx];
        if (thread.utime >= prev.utime) {
          line.cpu_user_perc = (thread.utime - prev.utime) / ticks_passed * 100;
        }
        if (thread.stime >= prev.stime) {
          line.cpu_kernel_perc =
              (thread.stime - prev.stime) / ticks_passed * 100;
        }
      }
    }

    // Store current CPU counters as prev for next update
    win.prev_threads =
        Array<ThreadCpuSample>::create(state.cur_arena, snap->threads.size);
    for (uint32_t i = 0; i < snap->threads.size; ++i) {
      const ProcessStat &t = snap->threads.data[i];
      win.prev_threads.data[i] = {t.pid, t.utime, t.stime};
    }
    win.prev_at_ns = now.time_since_epoch().count();

    // Apply current sorting
    if (win.sorted_order != ImGuiSortDirection_None) {
      sort_thread_lines(win);
    }
  }

  // Compact arena if wasted too much
  if (state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (ThreadsViewerWindow &win : state.windows) {
      if (win.lines.size > 0) {
        win.lines = Array<ThreadLine>::copy_from(new_arena, win.lines);
      }
      if (win.prev_threads.size > 0) {
        win.prev_threads =
            Array<ThreadCpuSample>::copy_from(new_arena, win.prev_threads);
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void threads_viewer_draw(FrameContext &ctx, ViewState &view_state,
                         const State &state) {
  ZoneScoped;
  ThreadsViewerState &my_state = view_state.threads_viewer_state;
  const int num_cpus = static_cast<int>(state.snapshot.cpu_stats.size) - 1;
  const bool cpu_per_core = view_state.preferences_state.cpu_per_core;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    ThreadsViewerWindow &win = my_state.windows.data()[last];

    const String title = on_demand_viewer_title(
        ctx.frame_arena, win.status, "Threads", "threads [Live]",
        win.lines.size, win.process_name, win.pid);
    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title.data);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title.data, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      if (win.status == eOnDemandViewerStatus_Error) {
        ImGui::TextWrapped("%s", win.error_message);
      } else if (win.lines.size > 0) {
        ImGuiTextFilter filter = draw_filter_input(
            "##ThreadFilter", win.filter_text, sizeof(win.filter_text));

        if (ImGui::BeginTable("Threads", eThreadsViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("TID", ImGuiTableColumnFlags_DefaultSort,
                                  0.0f, eThreadsViewerColumnId_Tid);
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f,
                                  eThreadsViewerColumnId_Name);
          ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None, 0.0f,
                                  eThreadsViewerColumnId_State);
          ImGui::TableSetupColumn("CPU",
                                  ImGuiTableColumnFlags_PreferSortDescending,
                                  0.0f, eThreadsViewerColumnId_CpuTotal);
          ImGui::TableSetupColumn("Kernel",
                                  ImGuiTableColumnFlags_PreferSortDescending,
                                  0.0f, eThreadsViewerColumnId_CpuKernel);
          ImGui::TableSetupColumn("Memory",
                                  ImGuiTableColumnFlags_PreferSortDescending,
                                  0.0f, eThreadsViewerColumnId_Memory);
          ImGui::TableHeadersRow();

          handle_table_sort_specs(win.sorted_by, win.sorted_order,
                                  [&] { sort_thread_lines(win); });

          for (uint32_t j = 0; j < win.lines.size; ++j) {
            const ThreadLine &line = win.lines.data[j];

            if (!filter.PassFilter(line.comm)) continue;

            const bool is_selected = win.selected_tid == line.tid;
            ImGui::TableNextRow();

            // TID column with selection
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Tid);
            char label[32];
            snprintf(label, sizeof(label), "%d", line.tid);
            if (ImGui::Selectable(label, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_tid = line.tid;
            }

            const int copy_column =
                table_context_column(eThreadsViewerColumnId_Count);
            if (ImGui::BeginPopupContextItem()) {
              win.selected_tid = line.tid;
              const String cell =
                  thread_cell_text(ctx.frame_arena, line, copy_column);
              if (ImGui::MenuItemEx(
                      copy_cell_menu_label(ctx.frame_arena, cell).data,
                      ICON_MD_CONTENT_COPY)) {
                clipboard_copy_cell(view_state.notifications, cell);
              }
              if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
                copy_thread_row(view_state.notifications, line);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_threads(view_state.notifications, ctx.frame_arena,
                                 win);
              }
              ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Name);
            table_item_draw_text(line.comm);

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_State);
            table_item_draw_state(line.state);

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_CpuTotal);
            table_item_draw_percent(
                scale_cpu_perc(line.cpu_user_perc + line.cpu_kernel_perc,
                               num_cpus, cpu_per_core));

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_CpuKernel);
            table_item_draw_percent(
                scale_cpu_perc(line.cpu_kernel_perc, num_cpus, cpu_per_core));

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Memory);
            table_item_draw_memory(line.mem_resident_bytes);
          }

          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (win.selected_tid >= 0 &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
          for (uint32_t j = 0; j < win.lines.size; ++j) {
            if (win.lines.data[j].tid == win.selected_tid) {
              copy_thread_row(view_state.notifications, win.lines.data[j]);
              break;
            }
          }
        }
      } else {
        ImGui::TextDisabled("No thread data available yet...");
      }
    }
    process_window_handle_focus(win.flags);
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      // Remove from watched list if no other window needs this PID
      if (!pid_still_needed(my_state, win.pid, last, i)) {
        view_state.sync->thread_unwatch_queue.push(win.pid);
      }
      my_state.wasted_bytes += win.lines.size * sizeof(ThreadLine) +
                               win.prev_threads.size * sizeof(ThreadCpuSample);
    }
  }
  my_state.windows.shrink_to(last);
}
