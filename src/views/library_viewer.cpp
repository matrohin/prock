#include "library_viewer.h"

#include "views/common.h"
#include "views/icons.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

#include <cstring>

const char *LIBRARY_COPY_HEADER = "Path\tMapped Size\tFile Size\n";

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_LIBRARIES = 5;

static String library_cell_text(BumpArena &arena, const LibraryEntry &lib,
                                const int column) {
  switch (column) {
  case eLibraryViewerColumnId_Path:
    return lib.path;
  case eLibraryViewerColumnId_MappedSize:
    return String::sprintf(arena, "%lu", lib.addr_end - lib.addr_start);
  case eLibraryViewerColumnId_FileSize:
    return String::sprintf(arena, "%ld", lib.file_size);
  default:
    return String::static_string("");
  }
}

static void copy_library_row(Notifications &notifications,
                             BumpArena &frame_arena, const LibraryEntry &lib) {
  const unsigned long mapped_size = lib.addr_end - lib.addr_start;
  const String str =
      String::sprintf(frame_arena, "%s%s\t%lu\t%ld", LIBRARY_COPY_HEADER,
                      lib.path.data, mapped_size, lib.file_size);
  clipboard_copy_row(notifications, str.data);
}

static void copy_all_libraries(Notifications &notifications, BumpArena &arena,
                               const LibraryViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.libraries.data, win.libraries.size, 320,
      LIBRARY_COPY_HEADER,
      [](char *ptr, const size_t rem, const LibraryEntry &lib) {
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
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.on_demand_reader.library_request_queue.push(req);
  }
  sync.on_demand_reader.request_read_cv.notify_one();
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
  win->status = eOnDemandViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  snprintf(win->process_name, sizeof(win->process_name), "%s", comm);
  win->selected_index = -1;
  win->last_updated = 0.0;

  send_library_request(sync, pid);

  common_views_sort_added(state.windows);
}

void library_viewer_update(LibraryViewerState &state, Sync &sync) {
  LibraryResponse response;
  while (sync.on_demand_reader.library_response_queue.pop(response)) {
    for (LibraryViewerWindow &win : state.windows) {
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eOnDemandViewerStatus_Ready;
          win.libraries = Array<LibraryEntry>::copy_from(state.cur_arena,
                                                         response.libraries);
          for (uint32_t j = 0; j < win.libraries.size; ++j) {
            LibraryEntry &dst = win.libraries.data[j];
            dst.path = String::copy_from(state.cur_arena, dst.path);
          }
          sort_libraries(win);
          win.last_updated = ImGui::GetTime();
        } else {
          win.status = eOnDemandViewerStatus_Error;
          win.error_code = response.error_code;
        }
        break;
      }
    }
    response.owner_arena.destroy();
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
    const String title = on_demand_viewer_title(
        ctx.frame_arena, win.status, "Libraries", "libraries",
        win.libraries.size, win.process_name, win.pid);

    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title.data);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title.data, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      // Content area - show previous data while loading, or error message
      if (win.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.error_code);
      } else if (win.libraries.size > 0) {
        ImGuiTextFilter filter;
        if (ImGui::BeginTable("Header", 4, ImGuiTableFlags_SizingStretchSame)) {
          ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
          ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
          ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch,
                                  HEADER_SPACER_WEIGHT);
          ImGui::TableNextRow();

          ImGui::TableNextColumn();
          ImGui::SetNextItemWidth(-FLT_MIN);
          draw_filter_input(filter, "##LibFilter", win.filter_text,
                            sizeof(win.filter_text));

          ImGui::TableNextColumn();
          if (draw_refresh_button()) {
            win.status = eOnDemandViewerStatus_Loading;
            send_library_request(*view_state.sync, win.pid);
          }
          ImGui::TableNextColumn();
          draw_last_updated(win.last_updated);

          ImGui::TableNextColumn(); // spacer

          ImGui::EndTable();
        }
        if (ImGui::BeginTable("Libraries", eLibraryViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
          push_mono_font();
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

            const int copy_column =
                table_context_column(eLibraryViewerColumnId_Count);
            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              const String cell =
                  library_cell_text(ctx.frame_arena, lib, copy_column);
              if (ImGui::MenuItemEx(
                      copy_cell_menu_label(ctx.frame_arena, cell).data,
                      ICON_MD_CONTENT_COPY)) {
                clipboard_copy_cell(view_state.notifications, cell);
              }
              if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
                copy_library_row(view_state.notifications, ctx.frame_arena,
                                 lib);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_libraries(view_state.notifications, ctx.frame_arena,
                                   win);
              }
              ImGui::EndPopup();
            }

            // Mapped Size (memory range)
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_MappedSize);
            unsigned long mapped_size = lib.addr_end - lib.addr_start;
            if (mapped_size >= 1024UL * 1024) {
              ImGui::Text("%.1f MB", mapped_size / (1024.0 * 1024.0));
            } else if (mapped_size >= 1024) {
              ImGui::Text("%.1f KB", mapped_size / 1024.0);
            } else {
              ImGui::Text("%lu B", mapped_size);
            }

            // File Size
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_FileSize);
            if (lib.file_size >= 0) {
              if (lib.file_size >= 1024L * 1024) {
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

          pop_mono_font();
          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (copy_row_shortcut(win.selected_index, win.libraries.size)) {
          copy_library_row(view_state.notifications, ctx.frame_arena,
                           win.libraries.data[win.selected_index]);
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
