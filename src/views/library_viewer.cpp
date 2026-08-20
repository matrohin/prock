#include "library_viewer.h"

#include "base/containers.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/shortcut.h"
#include "views/table_item.h"
#include "views/ui.h"
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
  sort_bidirectional(
      win.libraries.data, win.libraries.size, win.od.sorted_order,
      [&](const LibraryEntry &a, const LibraryEntry &b) {
        switch (win.od.sorted_by) {
        case eLibraryViewerColumnId_Path:
          return strcmp(a.path.data, b.path.data) < 0;
        case eLibraryViewerColumnId_MappedSize:
          return a.addr_end - a.addr_start < b.addr_end - b.addr_start;
        case eLibraryViewerColumnId_FileSize:
          return a.file_size < b.file_size;
        default:
          return false;
        }
      });
}

static Array<LibraryEntry> copy_libraries(BumpArena &arena,
                                          const Array<LibraryEntry> &src) {
  Array<LibraryEntry> dst = Array<LibraryEntry>::copy_from(arena, src);
  for (LibraryEntry &lib : dst) {
    lib.path = String::copy_from(arena, lib.path);
  }
  return dst;
}

static bool send_library_request(Sync &sync, const Pid pid) {
  return on_demand_send_request(
      sync, sync.on_demand_reader.library_request_queue, LibraryRequest{pid});
}

void library_viewer_request(LibraryViewerState &state, Sync &sync,
                            const Pid pid, const char *comm,
                            const ImGuiID dock_id,
                            const ProcessWindowFlags extra_flags) {
  if (on_demand_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  LibraryViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  on_demand_window_init(win->od, pid, comm, dock_id, extra_flags);

  if (!send_library_request(sync, pid)) {
    on_demand_mark_request_dropped(win->od);
  }

  on_demand_sort_added(state.windows);
}

void library_viewer_update(LibraryViewerState &state, Sync &sync) {
  LibraryResponse response;
  while (sync.on_demand_reader.library_response_queue.pop(response)) {
    LibraryViewerWindow *win = on_demand_find(state.windows, response.pid);
    if (win && on_demand_apply_response(win->od, response.error_code)) {
      if (win->libraries.size > 0) ++state.updates_since_last_cleanup;
      win->libraries = copy_libraries(state.cur_arena, response.libraries);
      sort_libraries(*win);
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
        win.libraries = copy_libraries(new_arena, win.libraries);
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

    bool keep_open = true;
    if (on_demand_window_begin(view_state, win.od, "Libraries",
                               win.libraries.size, ctx.frame_arena,
                               keep_open)) {
      // Content area - show previous data while loading, or error message
      if (win.od.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.od.error_code);
      } else if (win.libraries.size > 0) {
        ImGuiTextFilter filter;
        bool refresh = false;
        if (on_demand_toolbar_begin(win.od, filter, "##LibFilter")) {
          refresh = on_demand_toolbar_end(win.od);
        }
        if (refresh) {
          on_demand_refresh_status(
              win.od, send_library_request(*view_state.sync, win.od.pid));
        }
        if (ImGui::BeginTable("Libraries", eLibraryViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
          ui_push_mono_font();
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_DefaultSort,
                                  0.0f, eLibraryViewerColumnId_Path);
          ImGui::TableSetupColumn("Mapped Size",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eLibraryViewerColumnId_MappedSize);
          ImGui::TableSetupColumn("File Size",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eLibraryViewerColumnId_FileSize);
          ImGui::TableHeadersRow();

          handle_table_sort_specs(win.od.sorted_by, win.od.sorted_order,
                                  [&] { sort_libraries(win); });

          for (uint32_t j = 0; j < win.libraries.size; ++j) {
            const LibraryEntry &lib = win.libraries.data[j];
            if (!filter.PassFilter(lib.path.data)) continue;
            const bool is_selected =
                win.od.selected_index == static_cast<int>(j);
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_Path);
            const String shown = ui_path_fit(ctx.frame_arena, lib.path,
                                             ImGui::GetContentRegionAvail().x);
            const String label =
                String::sprintf(ctx.frame_arena, "%s###row", shown.data);
            if (ImGui::Selectable(label.data, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns) ||
                ImGui::IsItemFocused()) {
              win.od.selected_index = static_cast<int>(j);
            }
            if (shown.data != lib.path.data && ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", lib.path.data);
            }

            if (ui_context_menu(is_selected, win.od.context_menu_column,
                                eLibraryViewerColumnId_Count)) {
              win.od.selected_index = static_cast<int>(j);
              const String cell = library_cell_text(ctx.frame_arena, lib,
                                                    win.od.context_menu_column);
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
            table_item_draw_memory(
                static_cast<double>(lib.addr_end - lib.addr_start));

            // File Size
            ImGui::TableSetColumnIndex(eLibraryViewerColumnId_FileSize);
            if (lib.file_size >= 0) {
              table_item_draw_memory(static_cast<double>(lib.file_size));
            } else {
              table_item_draw_dim("N/A");
            }
            ImGui::PopID();
          }

          ui_pop_mono_font();
          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row
        if (shortcut_copy_row(win.od.selected_index, win.libraries.size)) {
          copy_library_row(view_state.notifications, ctx.frame_arena,
                           win.libraries.data[win.od.selected_index]);
        }
      }
    }
    on_demand_window_end(win.od);
    if (keep_open) {
      ++last;
    } else {
      ++my_state.updates_since_last_cleanup;
    }
  }
  my_state.windows.shrink_to(last);
}
