#include "environ_viewer.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

const char *ENVIRON_COPY_HEADER = "Name\tValue\n";

void copy_environ_row(const EnvironEntry &entry) {
  char buf[4400];
  snprintf(buf, sizeof(buf), "%s%s\t%s", ENVIRON_COPY_HEADER, entry.name,
           entry.value);
  ImGui::SetClipboardText(buf);
}

void copy_all_environ(BumpArena &arena, const EnvironViewerWindow &win) {
  size_t buf_size = 128 + win.entries.size * 4400;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", ENVIRON_COPY_HEADER);

  for (size_t i = 0; i < win.entries.size; ++i) {
    const EnvironEntry &entry = win.entries.data[i];
    ptr += snprintf(ptr, buf_size - (ptr - buf), "%s\t%s\n", entry.name,
                    entry.value);
  }
  ImGui::SetClipboardText(buf);
}

void sort_environ(EnvironViewerWindow &win) {
  if (win.entries.size == 0) return;

  const auto compare = [&](const EnvironEntry &a, const EnvironEntry &b) {
    switch (win.sorted_by) {
    case eEnvironViewerColumnId_Name:
      return strcmp(a.name, b.name) < 0;
    case eEnvironViewerColumnId_Value:
      return strcmp(a.value, b.value) < 0;
    default:
      return false;
    }
  };

  if (win.sorted_order == ImGuiSortDirection_Ascending) {
    std::stable_sort(win.entries.data, win.entries.data + win.entries.size,
                     compare);
  } else {
    std::stable_sort(win.entries.data, win.entries.data + win.entries.size,
                     [&](const EnvironEntry &a, const EnvironEntry &b) {
                       return compare(b, a);
                     });
  }
}

} // unnamed namespace

void environ_viewer_request(EnvironViewerState &state, Sync &sync,
                            const int pid, const char *comm,
                            const ImGuiID dock_id) {
  // Create new window
  EnvironViewerWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->status = eEnvironViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_index = -1;

  EnvironRequest req = {pid};
  sync.environ_request_queue.push(req);
  sync.library_cv.notify_one();

  common_views_sort_added(state.windows);
}

void environ_viewer_update(EnvironViewerState &state, Sync &sync) {
  // Process responses
  EnvironResponse response;
  while (sync.environ_response_queue.pop(response)) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
      EnvironViewerWindow &win = state.windows.data()[i];
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eEnvironViewerStatus_Ready;
          // Copy entries to our arena, including string data
          win.entries =
              Array<EnvironEntry>::create(state.cur_arena, response.entries.size);
          for (size_t j = 0; j < response.entries.size; ++j) {
            const EnvironEntry &src = response.entries.data[j];
            EnvironEntry &dst = win.entries.data[j];
            dst.name = state.cur_arena.alloc_string_copy(src.name, src.name_len);
            dst.name_len = src.name_len;
            dst.value = state.cur_arena.alloc_string_copy(src.value, src.value_len);
            dst.value_len = src.value_len;
          }
        } else {
          win.status = eEnvironViewerStatus_Error;
          win.error_code = response.error_code;
          snprintf(win.error_message, sizeof(win.error_message), "Error: %s",
                   strerror(response.error_code));
        }
        response.owner_arena.destroy();
        break;
      }
    }
  }

  // Compact arena if wasted too much
  if (state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (size_t i = 0; i < state.windows.size(); ++i) {
      EnvironViewerWindow &win = state.windows.data()[i];
      if (win.entries.size > 0) {
        Array<EnvironEntry> new_entries =
            Array<EnvironEntry>::create(new_arena, win.entries.size);
        for (size_t j = 0; j < win.entries.size; ++j) {
          const EnvironEntry &src = win.entries.data[j];
          EnvironEntry &dst = new_entries.data[j];
          dst.name = new_arena.alloc_string_copy(src.name, src.name_len);
          dst.name_len = src.name_len;
          dst.value = new_arena.alloc_string_copy(src.value, src.value_len);
          dst.value_len = src.value_len;
        }
        win.entries = new_entries;
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void environ_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  EnvironViewerState &my_state = view_state.environ_viewer_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    EnvironViewerWindow &win = my_state.windows.data()[last];
    char title[128];
    if (win.status == eEnvironViewerStatus_Error) {
      snprintf(title, sizeof(title), "Environment: %s (%d) - Error###Environ%d",
               win.process_name, win.pid, win.pid);
    } else if (win.status == eEnvironViewerStatus_Loading) {
      snprintf(title, sizeof(title),
               "Environment: %s (%d) - Loading...###Environ%d",
               win.process_name, win.pid, win.pid);
    } else {
      snprintf(title, sizeof(title),
               "Environment: %s (%d) - %zu variables###Environ%d",
               win.process_name, win.pid, win.entries.size, win.pid);
    }

    process_window_handle_docking_and_pos(view_state, win.dock_id,
                                          win.flags, title);

    bool should_be_opened = true;
    if (ImGui::Begin(title, &should_be_opened, COMMON_VIEW_FLAGS)) {
      process_window_check_close(win.flags, should_be_opened);

      // Content area - show previous data while loading, or error message
      if (win.status == eEnvironViewerStatus_Error) {
        ImGui::TextWrapped("%s", win.error_message);
        if (win.error_code == EACCES) {
          if (ImGui::Button("Restart with pkexec")) {
            execlp("pkexec", "pkexec", "/proc/self/exe", nullptr);
          }
        }
      } else if (win.entries.size > 0) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
          ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##EnvFilter", "Filter", win.filter_text,
                                 sizeof(win.filter_text));
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
          win.status = eEnvironViewerStatus_Loading;
          EnvironRequest req = {win.pid};
          view_state.sync->environ_request_queue.push(req);
          view_state.sync->library_cv.notify_one();
        }
        ImGuiTextFilter filter;
        if (win.filter_text[0] != '\0') {
          strncpy(filter.InputBuf, win.filter_text, sizeof(filter.InputBuf));
          filter.InputBuf[sizeof(filter.InputBuf) - 1] = '\0';
          filter.Build();
        }
        if (ImGui::BeginTable(
                "Environment", eEnvironViewerColumnId_Count,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
                    ImGuiTableFlags_ScrollY)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort,
                                  0.0f, eEnvironViewerColumnId_Name);
          ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_None, 0.0f,
                                  eEnvironViewerColumnId_Value);
          ImGui::TableHeadersRow();

          // Handle sorting
          if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty) {
              win.sorted_by = static_cast<EnvironViewerColumnId>(
                  sort_specs->Specs->ColumnUserID);
              win.sorted_order = sort_specs->Specs->SortDirection;
              sort_environ(win);
              sort_specs->SpecsDirty = false;
            }
          }

          for (size_t j = 0; j < win.entries.size; ++j) {
            const EnvironEntry &entry = win.entries.data[j];
            // Filter by name or value
            if (filter.IsActive() && !filter.PassFilter(entry.name) &&
                !filter.PassFilter(entry.value)) {
              continue;
            }
            const bool is_selected = (win.selected_index == static_cast<int>(j));
            ImGui::TableNextRow();

            // Name with selection
            ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Name);
            if (ImGui::Selectable(entry.name, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }

            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                copy_environ_row(entry);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_environ(ctx.frame_arena, win);
              }
              ImGui::EndPopup();
            }

            // Value
            ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
            ImGui::TextUnformatted(entry.value);
            if (ImGui::IsItemHovered() && entry.value_len > 50) {
              ImGui::SetTooltip("%s", entry.value);
            }
          }

          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (win.selected_index >= 0 &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
          copy_environ_row(win.entries.data[win.selected_index]);
        }
      }
    }
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += win.entries.size * sizeof(EnvironEntry);
    }
  }
  my_state.windows.shrink_to(last);
}
