#include "threads_viewer.h"

#include "base/algorithms.h"
#include "state.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/process_window_flags.h"
#include "views/ui.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "table_item.h"
#include "tracy/Tracy.hpp"

#include <cstring>

const char *THREAD_COPY_HEADER =
    "TID\tName\tState\tWchan\tCPU Total\tCPU Kernel\tLast CPU\n";

static String thread_cell_text(BumpArena &arena, const ThreadLine &line,
                               const int column) {
  switch (column) {
  case eThreadsViewerColumnId_Tid:
    return String::sprintf(arena, "%d", line.tid);
  case eThreadsViewerColumnId_Name:
    return String::static_string(line.comm);
  case eThreadsViewerColumnId_State:
    return String::sprintf(arena, "%c", line.state);
  case eThreadsViewerColumnId_Wchan:
    return String::static_string(line.wchan);
  case eThreadsViewerColumnId_CpuTotal:
    return String::sprintf(arena, "%.1f",
                           line.cpu_user_perc + line.cpu_kernel_perc);
  case eThreadsViewerColumnId_CpuKernel:
    return String::sprintf(arena, "%.1f", line.cpu_kernel_perc);
  case eThreadsViewerColumnId_LastCpu:
    return line.last_cpu < 0 ? String::static_string("-")
                             : String::sprintf(arena, "%d", line.last_cpu);
  default:
    return String::static_string("");
  }
}

static void copy_thread_row(Notifications &notifications,
                            BumpArena &frame_arena, const ThreadLine &line) {
  const String buf =
      String::sprintf(frame_arena, "%s%d\t%s\t%c\t%s\t%.1f\t%.1f\t%d",
                      THREAD_COPY_HEADER, line.tid, line.comm, line.state,
                      line.wchan, line.cpu_user_perc + line.cpu_kernel_perc,
                      line.cpu_kernel_perc, line.last_cpu);
  clipboard_copy_row(notifications, buf.data);
}

static void copy_all_threads(Notifications &notifications, BumpArena &arena,
                             const ThreadsViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.lines.data, win.lines.size, 256,
      THREAD_COPY_HEADER,
      [](char *ptr, const size_t rem, const ThreadLine &line) {
        return snprintf(ptr, rem, "%d\t%s\t%c\t%s\t%.1f\t%.1f\t%d\n", line.tid,
                        line.comm, line.state, line.wchan,
                        line.cpu_user_perc + line.cpu_kernel_perc,
                        line.cpu_kernel_perc, line.last_cpu);
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
  case eThreadsViewerColumnId_Wchan:
    return strcmp(a.wchan, b.wchan) < 0;
  case eThreadsViewerColumnId_CpuTotal:
    return a.cpu_user_perc + a.cpu_kernel_perc <
           b.cpu_user_perc + b.cpu_kernel_perc;
  case eThreadsViewerColumnId_CpuKernel:
    return a.cpu_kernel_perc < b.cpu_kernel_perc;
  case eThreadsViewerColumnId_LastCpu:
    return a.last_cpu < b.last_cpu;
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

void threads_viewer_update(FrameContext &ctx, ThreadsViewerState &state,
                           const State &state_data) {
  ZoneScoped;

  const double per_core_ticks = state_data.snapshot.per_core_ticks;
  const double interval_secs = state_data.snapshot.interval_secs;

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

    const Array<ThreadCpuSample> prev_threads = win.prev_threads;
    const Array<ThreadLine> old_lines = win.lines;

    // Build ThreadLine array from snapshot, in TID order as required by the
    // monotonic prev_threads walk below. Scratch for this update only, so it
    // lives in the frame arena.
    const Array<ThreadLine> fresh =
        Array<ThreadLine>::create(ctx.frame_arena, snap->threads.size);

    uint32_t prev_idx = 0;
    for (uint32_t i = 0; i < snap->threads.size; ++i) {
      const ProcessStat &thread = snap->threads.data[i];
      ThreadLine &line = fresh.data[i];

      line.tid = thread.pid;
      strncpy(line.comm, thread.comm, sizeof(line.comm) - 1);
      line.comm[sizeof(line.comm) - 1] = '\0';
      strncpy(line.wchan, thread.wchan, sizeof(line.wchan) - 1);
      line.wchan[sizeof(line.wchan) - 1] = '\0';
      line.state = thread.state;
      line.last_cpu = thread.last_cpu;
      line.cpu_user_perc = 0;
      line.cpu_kernel_perc = 0;

      // Find matching previous thread for delta computation
      while (prev_idx < prev_threads.size &&
             prev_threads.data[prev_idx].pid < thread.pid) {
        ++prev_idx;
      }

      if (prev_idx < prev_threads.size &&
          prev_threads.data[prev_idx].pid == thread.pid && per_core_ticks > 0) {
        const ThreadCpuSample &prev = prev_threads.data[prev_idx];
        const double effective_ticks = effective_core_ticks(
            thread.read_time, prev.read_time, per_core_ticks, interval_secs);
        line.cpu_user_perc =
            counter_rate(thread.utime, prev.utime, 100.0, effective_ticks);
        line.cpu_kernel_perc =
            counter_rate(thread.stime, prev.stime, 100.0, effective_ticks);
      }
    }

    // Store current CPU counters as prev for next update
    win.prev_threads =
        Array<ThreadCpuSample>::create(state.cur_arena, snap->threads.size);
    for (uint32_t i = 0; i < snap->threads.size; ++i) {
      const ProcessStat &t = snap->threads.data[i];
      win.prev_threads.data[i] = {t.pid, t.utime, t.stime, t.read_time};
    }

    // Rebuild lines in previous display order (dead threads dropped, new ones
    // appended) so the stable sort keeps tied rows where they were
    const Array<bool> taken = Array<bool>::create(ctx.frame_arena, fresh.size);
    win.lines = Array<ThreadLine>::create(state.cur_arena, fresh.size);
    uint32_t lines_count = 0;
    for (const ThreadLine &old_line : old_lines) {
      const uint32_t idx = bin_search_exact(
          fresh.size,
          [&fresh](const uint32_t mid) { return fresh.data[mid].tid; },
          old_line.tid);
      if (idx != UINT32_MAX) {
        win.lines.data[lines_count++] = fresh.data[idx];
        taken.data[idx] = true;
      }
    }
    for (uint32_t i = 0; i < fresh.size; ++i) {
      if (!taken.data[i]) {
        win.lines.data[lines_count++] = fresh.data[i];
      }
    }

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

    const String title =
        on_demand_viewer_title(ctx.frame_arena, win.status, "Threads",
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
        ImGuiTextFilter filter;
        if (ImGui::BeginTable("Header", 2, ImGuiTableFlags_SizingStretchSame)) {
          ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch,
                                  HEADER_SPACER_WEIGHT);
          ImGui::TableNextRow();

          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-FLT_MIN);
          ui_filter_input(filter, "##ThreadFilter", win.filter_text,
                          sizeof(win.filter_text));

          ImGui::TableNextColumn(); // spacer

          ImGui::EndTable();
        }

        if (ImGui::BeginTable("Threads", eThreadsViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
          ui_push_mono_font();
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("TID", ImGuiTableColumnFlags_DefaultSort,
                                  0.0f, eThreadsViewerColumnId_Tid);
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0f,
                                  eThreadsViewerColumnId_Name);
          ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None, 0.0f,
                                  eThreadsViewerColumnId_State);
          ImGui::TableSetupColumn("Wchan", ImGuiTableColumnFlags_None, 0.0f,
                                  eThreadsViewerColumnId_Wchan);
          ImGui::TableSetupColumn("CPU",
                                  ImGuiTableColumnFlags_PreferSortDescending,
                                  0.0f, eThreadsViewerColumnId_CpuTotal);
          ImGui::TableSetupColumn("Kernel",
                                  ImGuiTableColumnFlags_PreferSortDescending,
                                  0.0f, eThreadsViewerColumnId_CpuKernel);
          ImGui::TableSetupColumn("CPU#", ImGuiTableColumnFlags_None, 0.0f,
                                  eThreadsViewerColumnId_LastCpu);
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
                copy_thread_row(view_state.notifications, ctx.frame_arena,
                                line);
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

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Wchan);
            table_item_draw_text(line.wchan);

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_CpuTotal);
            table_item_draw_percent(
                scale_cpu_perc(line.cpu_user_perc + line.cpu_kernel_perc,
                               num_cpus, cpu_per_core));

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_CpuKernel);
            table_item_draw_percent(
                scale_cpu_perc(line.cpu_kernel_perc, num_cpus, cpu_per_core));

            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_LastCpu);
            if (line.last_cpu < 0) {
              ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "-");
            } else {
              table_item_draw_long(line.last_cpu);
            }
          }

          ui_pop_mono_font();
          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (win.selected_tid >= 0 &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
          for (uint32_t j = 0; j < win.lines.size; ++j) {
            if (win.lines.data[j].tid == win.selected_tid) {
              copy_thread_row(view_state.notifications, ctx.frame_arena,
                              win.lines.data[j]);
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
