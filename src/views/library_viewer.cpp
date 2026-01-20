#include "library_viewer.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

const char *LIBRARY_COPY_HEADER = "Path\tMapped Size\tFile Size\n";

void copy_library_row(const LibraryEntry &lib) {
  char buf[512];
  unsigned long mapped_size = lib.addr_end - lib.addr_start;
  snprintf(buf, sizeof(buf), "%s%s\t%lu\t%ld", LIBRARY_COPY_HEADER, lib.path,
           mapped_size, lib.file_size);
  ImGui::SetClipboardText(buf);
}

void copy_all_libraries(BumpArena &arena, const LibraryViewerWindow &win) {
  size_t buf_size = 128 + win.libraries.size * 320;
  char *buf = static_cast<char *>(arena.alloc_raw(buf_size, 1));
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", LIBRARY_COPY_HEADER);

  for (size_t i = 0; i < win.libraries.size; ++i) {
    const LibraryEntry &lib = win.libraries.data[i];
    unsigned long mapped_size = lib.addr_end - lib.addr_start;
    ptr += snprintf(ptr, buf_size - (ptr - buf), "%s\t%lu\t%ld\n", lib.path,
                    mapped_size, lib.file_size);
  }
  ImGui::SetClipboardText(buf);
}

void sort_libraries(LibraryViewerWindow &win) {
  if (win.libraries.size == 0) return;

  const auto compare = [&](const LibraryEntry &a, const LibraryEntry &b) {
    switch (win.sorted_by) {
    case eLibraryViewerColumnId_Path:
      return strcmp(a.path, b.path) < 0;
    case eLibraryViewerColumnId_MappedSize:
      return (a.addr_end - a.addr_start) < (b.addr_end - b.addr_start);
    case eLibraryViewerColumnId_FileSize:
      return a.file_size < b.file_size;
    default:
      return false;
    }
  };

  if (win.sorted_order == ImGuiSortDirection_Ascending) {
    std::stable_sort(win.libraries.data,
                     win.libraries.data + win.libraries.size, compare);
  } else {
    std::stable_sort(win.libraries.data,
                     win.libraries.data + win.libraries.size,
                     [&](const LibraryEntry &a, const LibraryEntry &b) {
                       return compare(b, a);
                     });
  }
}

} // unnamed namespace

void library_viewer_request(LibraryViewerState &state, Sync &sync,
                            const int pid, const char *comm) {
  // Check if window exists for pid - reopen if found
  for (size_t i = 0; i < state.windows.size(); ++i) {
    if (state.windows.data()[i].pid == pid) {
      state.windows.data()[i].open = true;
      return;
    }
  }

  // Create new window
  LibraryViewerWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->open = true;
  win->status = eLibraryViewerStatus_Loading;
  win->pid = pid;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->process_name[sizeof(win->process_name) - 1] = '\0';
  win->libraries = {};
  win->error_code = 0;
  win->error_message[0] = '\0';
  win->sorted_by = eLibraryViewerColumnId_Path;
  win->sorted_order = ImGuiSortDirection_Ascending;
  win->selected_index = -1;
  win->filter_text[0] = '\0';

  LibraryRequest req = {pid};
  sync.library_request_queue.push(req);
  sync.library_cv.notify_one();
}

void library_viewer_update(LibraryViewerState &state, Sync &sync) {
  // Process responses
  LibraryResponse response;
  while (sync.library_response_queue.pop(response)) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
      LibraryViewerWindow &win = state.windows.data()[i];
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eLibraryViewerStatus_Ready;
          // Copy libraries to our arena
          win.libraries = Array<LibraryEntry>::create(state.cur_arena,
                                                      response.libraries.size);
          memcpy(win.libraries.data, response.libraries.data,
                 response.libraries.size * sizeof(LibraryEntry));
        } else {
          win.status = eLibraryViewerStatus_Error;
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
      LibraryViewerWindow &win = state.windows.data()[i];
      if (win.libraries.size > 0) {
        Array<LibraryEntry> new_libs =
            Array<LibraryEntry>::create(new_arena, win.libraries.size);
        memcpy(new_libs.data, win.libraries.data,
               win.libraries.size * sizeof(LibraryEntry));
        win.libraries = new_libs;
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void library_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  LibraryViewerState &my_state = view_state.library_viewer_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    LibraryViewerWindow &win = my_state.windows.data()[last];
    if (!win.open) {
      my_state.wasted_bytes += sizeof(LibraryViewerWindow);
      my_state.wasted_bytes += win.libraries.size * sizeof(LibraryEntry);
      continue;
    }

    char title[128];
    if (win.status == eLibraryViewerStatus_Error) {
      snprintf(title, sizeof(title), "Libraries: %s (%d) - Error###Libraries%d",
               win.process_name, win.pid, win.pid);
    } else if (win.status == eLibraryViewerStatus_Loading) {
      snprintf(title, sizeof(title),
               "Libraries: %s (%d) - Loading...###Libraries%d",
               win.process_name, win.pid, win.pid);
    } else {
      snprintf(title, sizeof(title),
               "Libraries: %s (%d) - %zu libraries###Libraries%d",
               win.process_name, win.pid, win.libraries.size, win.pid);
    }
    view_state.cascade.next_if_new(title);

    if (ImGui::Begin(title, &win.open, COMMON_VIEW_FLAGS)) {
      if (ImGui::IsWindowFocused()) {
        view_state.focused_view = eFocusedView_LibraryViewer;
        my_state.focused_window_pid = win.pid;
      }

      // Content area - show previous data while loading, or error message
      if (win.status == eLibraryViewerStatus_Error) {
        ImGui::TextWrapped("%s", win.error_message);
        if (win.error_code == EACCES) {
          if (ImGui::Button("Restart with pkexec")) {
            execlp("pkexec", "pkexec", "/proc/self/exe", nullptr);
          }
        }
      } else if (win.libraries.size > 0) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F)) {
          ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##LibFilter", "Filter", win.filter_text,
                                 sizeof(win.filter_text));
        ImGuiTextFilter filter;
        if (win.filter_text[0] != '\0') {
          strncpy(filter.InputBuf, win.filter_text, sizeof(filter.InputBuf));
          filter.InputBuf[sizeof(filter.InputBuf) - 1] = '\0';
          filter.Build();
        }
        if (ImGui::BeginTable(
                "Libraries", eLibraryViewerColumnId_Count,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
                    ImGuiTableFlags_ScrollY)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_DefaultSort,
                                  0.0f, eLibraryViewerColumnId_Path);
          ImGui::TableSetupColumn("Mapped Size",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  100.0f, eLibraryViewerColumnId_MappedSize);
          ImGui::TableSetupColumn("File Size",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  100.0f, eLibraryViewerColumnId_FileSize);
          ImGui::TableHeadersRow();

          // Handle sorting
          if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty) {
              win.sorted_by = static_cast<LibraryViewerColumnId>(
                  sort_specs->Specs->ColumnUserID);
              win.sorted_order = sort_specs->Specs->SortDirection;
              sort_libraries(win);
              sort_specs->SpecsDirty = false;
            }
          }

          for (size_t j = 0; j < win.libraries.size; ++j) {
            const LibraryEntry &lib = win.libraries.data[j];
            if (!filter.PassFilter(lib.path)) continue;
            const bool is_selected = (win.selected_index == static_cast<int>(j));
            ImGui::TableNextRow();

            // Path with selection
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_Path);
            if (ImGui::Selectable(lib.path, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", lib.path);
            }

            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                copy_library_row(lib);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_libraries(ctx.frame_arena, win);
              }
              ImGui::EndPopup();
            }

            // Mapped Size (memory range)
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_MappedSize);
            unsigned long mapped_size = lib.addr_end - lib.addr_start;
            if (mapped_size >= 1024 * 1024) {
              ImGui::Text("%.1f MB", mapped_size / (1024.0 * 1024.0));
            } else if (mapped_size >= 1024) {
              ImGui::Text("%.1f KB", mapped_size / 1024.0);
            } else {
              ImGui::Text("%lu B", mapped_size);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("0x%lx - 0x%lx", lib.addr_start, lib.addr_end);
            }

            // File Size
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_FileSize);
            if (lib.file_size >= 0) {
              if (lib.file_size >= 1024 * 1024) {
                ImGui::Text("%.1f MB", lib.file_size / (1024.0 * 1024.0));
              } else if (lib.file_size >= 1024) {
                ImGui::Text("%.1f KB", lib.file_size / 1024.0);
              } else {
                ImGui::Text("%ld B", lib.file_size);
              }
            } else {
              ImGui::TextDisabled("N/A");
            }
          }

          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (win.selected_index >= 0 &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
          copy_library_row(win.libraries.data[win.selected_index]);
        }
      }
    }
    ImGui::End();
    ++last;
  }
  my_state.windows.shrink_to(last);
}
