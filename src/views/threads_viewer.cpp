#include "threads_viewer.h"

#include "state.h"
#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cstring>

const char *THREAD_COPY_HEADER = "TID\tName\tState\tCPU Total\tCPU Kernel\tMemory\n";

static void copy_thread_row(const ProcessStat &thread,
                            const ThreadDerivedStat &derived) {
  char buf[512];
  snprintf(buf, sizeof(buf), "%s%d\t%s\t%c\t%.1f\t%.1f\t%ld", THREAD_COPY_HEADER,
           thread.pid, thread.comm, thread.state,
           derived.cpu_user_perc + derived.cpu_kernel_perc,
           derived.cpu_kernel_perc, derived.mem_resident_bytes);
  ImGui::SetClipboardText(buf);
}

static void copy_all_threads(BumpArena &arena, const ThreadsViewerWindow &win) {
  const size_t buf_size = 128 + win.threads.size * 256;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", THREAD_COPY_HEADER);

  for (size_t i = 0; i < win.threads.size; ++i) {
    const ProcessStat &thread = win.threads.data[i];
    const ThreadDerivedStat &derived = win.derived.data[i];
    ptr += snprintf(ptr, buf_size - (ptr - buf), "%d\t%s\t%c\t%.1f\t%.1f\t%ld\n",
                    thread.pid, thread.comm, thread.state,
                    derived.cpu_user_perc + derived.cpu_kernel_perc,
                    derived.cpu_kernel_perc, derived.mem_resident_bytes);
  }
  ImGui::SetClipboardText(buf);
}

static void sort_threads(ThreadsViewerWindow &win) {
  if (win.threads.size == 0) return;

  // Create index array for sorting
  size_t *indices =
      static_cast<size_t *>(alloca(win.threads.size * sizeof(size_t)));
  for (size_t i = 0; i < win.threads.size; ++i) {
    indices[i] = i;
  }

  const auto compare = [&](size_t a, size_t b) {
    const ProcessStat &ta = win.threads.data[a];
    const ProcessStat &tb = win.threads.data[b];
    const ThreadDerivedStat &da = win.derived.data[a];
    const ThreadDerivedStat &db = win.derived.data[b];

    switch (win.sorted_by) {
    case eThreadsViewerColumnId_Tid:
      return ta.pid < tb.pid;
    case eThreadsViewerColumnId_Name:
      return strcmp(ta.comm, tb.comm) < 0;
    case eThreadsViewerColumnId_State:
      return ta.state < tb.state;
    case eThreadsViewerColumnId_CpuTotal:
      return (da.cpu_user_perc + da.cpu_kernel_perc) <
             (db.cpu_user_perc + db.cpu_kernel_perc);
    case eThreadsViewerColumnId_CpuKernel:
      return da.cpu_kernel_perc < db.cpu_kernel_perc;
    case eThreadsViewerColumnId_Memory:
      return da.mem_resident_bytes < db.mem_resident_bytes;
    default:
      return false;
    }
  };

  if (win.sorted_order == ImGuiSortDirection_Ascending) {
    std::stable_sort(indices, indices + win.threads.size, compare);
  } else {
    std::stable_sort(indices, indices + win.threads.size,
                     [&](size_t a, size_t b) { return compare(b, a); });
  }

  // Apply sorted order (in-place reorder using temp storage)
  ProcessStat *temp_threads =
      static_cast<ProcessStat *>(alloca(win.threads.size * sizeof(ProcessStat)));
  ThreadDerivedStat *temp_derived = static_cast<ThreadDerivedStat *>(
      alloca(win.threads.size * sizeof(ThreadDerivedStat)));

  for (size_t i = 0; i < win.threads.size; ++i) {
    temp_threads[i] = win.threads.data[indices[i]];
    temp_derived[i] = win.derived.data[indices[i]];
  }
  memcpy(win.threads.data, temp_threads,
         win.threads.size * sizeof(ProcessStat));
  memcpy(win.derived.data, temp_derived,
         win.threads.size * sizeof(ThreadDerivedStat));
}

// Add a PID to the watched list for thread gathering
static bool add_watched_pid(Sync &sync, int pid) {
  // Check if already watched
  for (int i = 0; i < MAX_WATCHED_PIDS; ++i) {
    if (sync.watched_pids[i].load() == pid) {
      return true;  // Already watching
    }
  }

  // Find empty slot
  for (int i = 0; i < MAX_WATCHED_PIDS; ++i) {
    int expected = 0;
    if (sync.watched_pids[i].compare_exchange_strong(expected, pid)) {
      sync.watched_pids_count.fetch_add(1);
      return true;
    }
  }
  return false;  // No empty slot
}

// Remove a PID from the watched list
static void remove_watched_pid(Sync &sync, int pid) {
  for (int i = 0; i < MAX_WATCHED_PIDS; ++i) {
    int expected = pid;
    if (sync.watched_pids[i].compare_exchange_strong(expected, 0)) {
      sync.watched_pids_count.fetch_sub(1);
      return;
    }
  }
}

// Check if any window still needs this PID watched
static bool pid_still_needed(const ThreadsViewerState &state, int pid,
                             size_t exclude_idx) {
  for (size_t i = 0; i < state.windows.size(); ++i) {
    if (i != exclude_idx && state.windows.data()[i].pid == pid) {
      return true;
    }
  }
  return false;
}

void threads_viewer_open(ThreadsViewerState &state, Sync &sync, const int pid,
                         const char *comm, const ImGuiID dock_id,
                         const ProcessWindowFlags extra_flags) {
  // Check if window already exists for this PID
  for (size_t i = 0; i < state.windows.size(); ++i) {
    if (state.windows.data()[i].pid == pid) {
      return;  // Already open
    }
  }

  if (!add_watched_pid(sync, pid)) {
    return;  // Failed to add to watched list
  }

  ThreadsViewerWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  win->status = eThreadsViewerStatus_Loading;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_tid = -1;
  win->sorted_by = eThreadsViewerColumnId_CpuTotal;
  win->sorted_order = ImGuiSortDirection_Descending;

  common_views_sort_added(state.windows);
}

void threads_viewer_update(ThreadsViewerState &state,
                           const State & /*state_data*/, Sync & /*sync*/) {
  ZoneScoped;

  // Thread snapshots are processed in threads_viewer_process_snapshot
  // which is called from main.cpp before views_update.
  // This function handles arena compaction.

  // Compact arena if wasted too much
  if (state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (size_t i = 0; i < state.windows.size(); ++i) {
      ThreadsViewerWindow &win = state.windows.data()[i];
      if (win.threads.size > 0) {
        Array<ProcessStat> new_threads =
            Array<ProcessStat>::create(new_arena, win.threads.size);
        memcpy(new_threads.data, win.threads.data,
               win.threads.size * sizeof(ProcessStat));
        win.threads = new_threads;

        Array<ThreadDerivedStat> new_derived =
            Array<ThreadDerivedStat>::create(new_arena, win.derived.size);
        memcpy(new_derived.data, win.derived.data,
               win.derived.size * sizeof(ThreadDerivedStat));
        win.derived = new_derived;
      }
      if (win.prev_threads.size > 0) {
        Array<ProcessStat> new_prev =
            Array<ProcessStat>::create(new_arena, win.prev_threads.size);
        memcpy(new_prev.data, win.prev_threads.data,
               win.prev_threads.size * sizeof(ProcessStat));
        win.prev_threads = new_prev;
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

// Called from state update to process thread snapshots
void threads_viewer_process_snapshot(ThreadsViewerState &state,
                                     const State &state_data,
                                     const Array<ThreadSnapshot> &snapshots) {
  const long page_size = state_data.system.mem_page_size;
  const double ticks_in_second = state_data.system.ticks_in_second;

  for (size_t w = 0; w < state.windows.size(); ++w) {
    ThreadsViewerWindow &win = state.windows.data()[w];

    // Find matching snapshot
    const ThreadSnapshot *snap = nullptr;
    for (size_t s = 0; s < snapshots.size; ++s) {
      if (snapshots.data[s].pid == win.pid) {
        snap = &snapshots.data[s];
        break;
      }
    }

    if (!snap || snap->threads.size == 0) {
      continue;
    }

    win.status = eThreadsViewerStatus_Ready;

    // Save previous data for delta computation
    state.wasted_bytes +=
        win.threads.size * sizeof(ProcessStat) +
        win.derived.size * sizeof(ThreadDerivedStat) +
        win.prev_threads.size * sizeof(ProcessStat);

    SteadyTimePoint prev_at{SteadyClock::duration{win.prev_at_ns}};
    Array<ProcessStat> prev_threads = win.prev_threads;

    // Copy new thread data
    win.threads = Array<ProcessStat>::create(state.cur_arena, snap->threads.size);
    memcpy(win.threads.data, snap->threads.data,
           snap->threads.size * sizeof(ProcessStat));

    // Compute derived stats
    win.derived =
        Array<ThreadDerivedStat>::create(state.cur_arena, snap->threads.size);

    const SteadyTimePoint now = state_data.snapshot.at;
    const double time_delta =
        std::chrono::duration_cast<Seconds>(now - prev_at).count();
    const double ticks_passed = ticks_in_second * time_delta;

    size_t prev_idx = 0;
    for (size_t i = 0; i < win.threads.size; ++i) {
      ThreadDerivedStat &derived = win.derived.data[i];
      const ProcessStat &thread = win.threads.data[i];
      derived.mem_resident_bytes = thread.statm_resident * page_size;

      // Find matching previous thread for delta computation
      while (prev_idx < prev_threads.size &&
             prev_threads.data[prev_idx].pid < thread.pid) {
        ++prev_idx;
      }

      if (prev_idx < prev_threads.size &&
          prev_threads.data[prev_idx].pid == thread.pid && ticks_passed > 0) {
        const ProcessStat &prev = prev_threads.data[prev_idx];
        if (thread.utime >= prev.utime) {
          derived.cpu_user_perc =
              (thread.utime - prev.utime) / ticks_passed * 100;
        }
        if (thread.stime >= prev.stime) {
          derived.cpu_kernel_perc =
              (thread.stime - prev.stime) / ticks_passed * 100;
        }
      }
    }

    // Store current as prev for next update
    win.prev_threads =
        Array<ProcessStat>::create(state.cur_arena, win.threads.size);
    memcpy(win.prev_threads.data, win.threads.data,
           win.threads.size * sizeof(ProcessStat));
    win.prev_at_ns = now.time_since_epoch().count();

    // Apply current sorting
    if (win.sorted_order != ImGuiSortDirection_None) {
      sort_threads(win);
    }
  }
}

void threads_viewer_draw(FrameContext &ctx, ViewState &view_state,
                         const State & /*state*/) {
  ZoneScoped;
  ThreadsViewerState &my_state = view_state.threads_viewer_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    ThreadsViewerWindow &win = my_state.windows.data()[last];

    char title[128];
    if (win.status == eThreadsViewerStatus_Error) {
      snprintf(title, sizeof(title), "Threads: %s (%d) - Error###Threads%d",
               win.process_name, win.pid, win.pid);
    } else if (win.status == eThreadsViewerStatus_Loading) {
      snprintf(title, sizeof(title),
               "Threads: %s (%d) - Loading...###Threads%d", win.process_name,
               win.pid, win.pid);
    } else {
      snprintf(title, sizeof(title),
               "Threads: %s (%d) - %zu threads [Live]###Threads%d",
               win.process_name, win.pid, win.threads.size, win.pid);
    }

    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title);

    bool should_be_opened = true;
    ImGuiWindowFlags win_flags = COMMON_VIEW_FLAGS;
    if (win.flags & eProcessWindowFlags_NoFocusOnAppearing) {
      win_flags |= ImGuiWindowFlags_NoFocusOnAppearing;
      win.flags &= ~eProcessWindowFlags_NoFocusOnAppearing;
    }
    if (ImGui::Begin(title, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      if (win.status == eThreadsViewerStatus_Error) {
        ImGui::TextWrapped("%s", win.error_message);
      } else if (win.threads.size > 0) {
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
          ImGui::TableSetupColumn("CPU%",
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
                                  [&]() { sort_threads(win); });

          for (size_t j = 0; j < win.threads.size; ++j) {
            const ProcessStat &thread = win.threads.data[j];
            const ThreadDerivedStat &derived = win.derived.data[j];

            if (!filter.PassFilter(thread.comm)) continue;

            const bool is_selected = (win.selected_tid == thread.pid);
            ImGui::TableNextRow();

            // TID column with selection
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Tid);
            char label[32];
            snprintf(label, sizeof(label), "%d", thread.pid);
            if (ImGui::Selectable(label, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_tid = thread.pid;
            }

            if (ImGui::BeginPopupContextItem()) {
              win.selected_tid = thread.pid;
              if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                copy_thread_row(thread, derived);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_threads(ctx.frame_arena, win);
              }
              ImGui::EndPopup();
            }

            // Name column
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Name);
            ImGui::Text("%s", thread.comm);

            // State column
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_State);
            ImGui::Text("%c", thread.state);
            if (ImGui::IsItemHovered()) {
              const char *desc = get_state_tooltip(thread.state);
              if (desc) ImGui::SetTooltip("%s", desc);
            }

            // CPU% column
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_CpuTotal);
            ImGui::TextAligned(
                1.0f, ImGui::GetColumnWidth(), "%.1f",
                derived.cpu_user_perc + derived.cpu_kernel_perc);

            // Kernel column
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_CpuKernel);
            ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%.1f",
                               derived.cpu_kernel_perc);

            // Memory column
            ImGui::TableSetColumnIndex(eThreadsViewerColumnId_Memory);
            char mem_buf[32];
            format_memory_bytes(derived.mem_resident_bytes, mem_buf,
                                sizeof(mem_buf));
            ImGui::TextAligned(1.0f, ImGui::GetColumnWidth(), "%s", mem_buf);
          }

          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (win.selected_tid >= 0 &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
          for (size_t j = 0; j < win.threads.size; ++j) {
            if (win.threads.data[j].pid == win.selected_tid) {
              copy_thread_row(win.threads.data[j], win.derived.data[j]);
              break;
            }
          }
        }
      } else {
        ImGui::TextDisabled("No thread data available yet...");
      }
    }
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      // Remove from watched list if no other window needs this PID
      if (!pid_still_needed(my_state, win.pid, i)) {
        remove_watched_pid(*view_state.sync, win.pid);
      }
      my_state.wasted_bytes +=
          win.threads.size * sizeof(ProcessStat) +
          win.derived.size * sizeof(ThreadDerivedStat) +
          win.prev_threads.size * sizeof(ProcessStat);
    }
  }
  my_state.windows.shrink_to(last);
}
