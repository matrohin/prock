#include "library_viewer.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

const char *LIBRARY_COPY_HEADER = "Path\tMapped Size\tFile Size\n";

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_LIBRARIES = 5;

static void copy_library_row(const LibraryEntry &lib) {
  char buf[512];
  const unsigned long mapped_size = lib.addr_end - lib.addr_start;
  snprintf(buf, sizeof(buf), "%s%s\t%lu\t%ld", LIBRARY_COPY_HEADER,
           lib.path.data, mapped_size, lib.file_size);
  ImGui::SetClipboardText(buf);
}

static void copy_all_libraries(BumpArena &arena,
                               const LibraryViewerWindow &win) {
  copy_all_to_clipboard(
      arena, win.libraries.data, win.libraries.size, 320, LIBRARY_COPY_HEADER,
      [](char *ptr, size_t rem, const LibraryEntry &lib) {
        const unsigned long mapped_size = lib.addr_end - lib.addr_start;
        return snprintf(ptr, rem, "%s\t%lu\t%ld\n", lib.path.data, mapped_size,
                        lib.file_size);
      });
}

static void sort_libraries(LibraryViewerWindow &win) {
  sort_bidirectional(win.libraries.data, win.libraries.size, win.sorted_order,
                     [&](const LibraryEntry &a, const LibraryEntry &b) {
                       switch (win.sorted_by) {
                       case eLibraryViewerColumnId_Path:
                         return strcmp(a.path.data, b.path.data) < 0;
                       case eLibraryViewerColumnId_MappedSize:
                         return a.addr_end - a.addr_start <
                                b.addr_end - b.addr_start;
                       case eLibraryViewerColumnId_FileSize:
                         return a.file_size < b.file_size;
                       default:
                         return false;
                       }
                     });
}

static void send_library_request(Sync &sync, const Pid pid) {
  const LibraryRequest req = {pid};
  sync.on_demand_reader.library_request_queue.push(req);
  sync.on_demand_reader.library_cv.notify_one();
}

void library_viewer_request(LibraryViewerState &state, Sync &sync,
                            const Pid pid, const char *comm,
                            const ImGuiID dock_id,
                            const ProcessWindowFlags extra_flags) {
  if (process_window_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  LibraryViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  win->status = eLibraryViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_index = -1;

  send_library_request(sync, pid);

  common_views_sort_added(state.windows);
}

void library_viewer_update(LibraryViewerState &state, Sync &sync) {
  LibraryResponse response;
  while (sync.on_demand_reader.library_response_queue.pop(response)) {
    for (LibraryViewerWindow &win : state.windows) {
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eLibraryViewerStatus_Ready;
          win.libraries = Array<LibraryEntry>::copy_from(state.cur_arena,
                                                         response.libraries);
          for (uint32_t j = 0; j < win.libraries.size; ++j) {
            LibraryEntry &dst = win.libraries.data[j];
            dst.path = String::copy_from(state.cur_arena, dst.path);
          }
        } else {
          win.status = eLibraryViewerStatus_Error;
          win.error_code = response.error_code;
        }
        response.owner_arena.destroy();
        break;
      }
    }
  }

  // Compact arena if wasted too much
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_LIBRARIES) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (LibraryViewerWindow &win : state.windows) {
      if (win.libraries.size > 0) {
        win.libraries =
            Array<LibraryEntry>::copy_from(new_arena, win.libraries);
        for (uint32_t j = 0; j < win.libraries.size; ++j) {
          LibraryEntry &dst = win.libraries.data[j];
          dst.path = String::copy_from(new_arena, dst.path);
        }
      }
    }

    state.cur_arena = new_arena;
    state.updates_since_last_cleanup = 0;
    old_arena.destroy();
  }
}

void library_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  LibraryViewerState &my_state = view_state.library_viewer_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    LibraryViewerWindow &win = my_state.windows.data()[last];
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
               "Libraries: %s (%d) - %u libraries###Libraries%d",
               win.process_name, win.pid, win.libraries.size, win.pid);
    }

    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      // Content area - show previous data while loading, or error message
      if (win.status == eLibraryViewerStatus_Error) {
        draw_error_with_pkexec(win.error_code);
      } else if (win.libraries.size > 0) {
        ImGuiTextFilter filter = draw_filter_input(
            "##LibFilter", win.filter_text, sizeof(win.filter_text));
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
          win.status = eLibraryViewerStatus_Loading;
          send_library_request(*view_state.sync, win.pid);
        }
        if (ImGui::BeginTable("Libraries", eLibraryViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
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

          handle_table_sort_specs(win.sorted_by, win.sorted_order,
                                  [&] { sort_libraries(win); });

          for (uint32_t j = 0; j < win.libraries.size; ++j) {
            const LibraryEntry &lib = win.libraries.data[j];
            if (!filter.PassFilter(lib.path.data)) continue;
            const bool is_selected = win.selected_index == static_cast<int>(j);
            ImGui::TableNextRow();

            // Path with selection
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_Path);
            if (ImGui::Selectable(lib.path.data, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", lib.path.data);
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
    process_window_handle_focus(win.flags);
    ImGui::End();
    if (should_be_opened) {
      ++last;
    } else {
      ++my_state.updates_since_last_cleanup;
    }
  }
  my_state.windows.shrink_to(last);
}
