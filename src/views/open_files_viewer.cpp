#include "open_files_viewer.h"

#include "base/containers.h"
#include "base/string.h"
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

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_OPEN_FILES = 5;

const char *OPEN_FILES_COPY_HEADER = "FD\tType\tAccess\tSize\tPath\n";

static const char *fd_type_name(const FdType type) {
  switch (type) {
  case eFdType_File:
    return "file";
  case eFdType_Dir:
    return "dir";
  case eFdType_Char:
    return "char";
  case eFdType_Block:
    return "block";
  case eFdType_Socket:
    return "socket";
  case eFdType_Pipe:
    return "pipe";
  case eFdType_Anon:
    return "anon";
  case eFdType_Other:
    return "other";
  }
  return "";
}

static const char *fd_access_name(const FdAccess access) {
  switch (access) {
  case eFdAccess_Read:
    return "r";
  case eFdAccess_Write:
    return "w";
  case eFdAccess_ReadWrite:
    return "rw";
  case eFdAccess_Unknown:
    return "";
  }
  return "";
}

static String format_fd_size(BumpArena &arena, const long size) {
  if (size < 0) return String::static_string("-");
  char buf[32];
  format_memory_bytes(static_cast<double>(size), buf, sizeof(buf));
  return String::copy_from(arena, buf);
}

static String open_file_cell_text(BumpArena &arena, const OpenFileEntry &file,
                                  const int column) {
  switch (column) {
  case eOpenFilesViewerColumnId_Fd:
    return String::sprintf(arena, "%d", file.fd);
  case eOpenFilesViewerColumnId_Type:
    return String::static_string(fd_type_name(file.type));
  case eOpenFilesViewerColumnId_Access:
    return String::static_string(fd_access_name(file.access));
  case eOpenFilesViewerColumnId_Size:
    return format_fd_size(arena, file.size);
  case eOpenFilesViewerColumnId_Path:
    return file.path;
  default:
    return String::static_string("");
  }
}

static void copy_open_file_row(Notifications &notifications,
                               BumpArena &frame_arena,
                               const OpenFileEntry &file) {
  const String size = format_fd_size(frame_arena, file.size);
  const String str =
      String::sprintf(frame_arena, "%s%d\t%s\t%s\t%s\t%s",
                      OPEN_FILES_COPY_HEADER, file.fd, fd_type_name(file.type),
                      fd_access_name(file.access), size.data, file.path.data);
  clipboard_copy_row(notifications, str.data);
}

static void copy_all_open_files(Notifications &notifications, BumpArena &arena,
                                const OpenFilesViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.files.data, win.files.size, 320,
      OPEN_FILES_COPY_HEADER,
      [&arena](char *ptr, const size_t rem, const OpenFileEntry &file) {
        const String size = format_fd_size(arena, file.size);
        return snprintf(ptr, rem, "%d\t%s\t%s\t%s\t%s\n", file.fd,
                        fd_type_name(file.type), fd_access_name(file.access),
                        size.data, file.path.data);
      });
}

static void sort_open_files(const OpenFilesViewerWindow &win) {
  sort_bidirectional(win.files.data, win.files.size, win.od.sorted_order,
                     [&](const OpenFileEntry &a, const OpenFileEntry &b) {
                       switch (win.od.sorted_by) {
                       case eOpenFilesViewerColumnId_Fd:
                         return a.fd < b.fd;
                       case eOpenFilesViewerColumnId_Type:
                         return a.type < b.type;
                       case eOpenFilesViewerColumnId_Access:
                         return a.access < b.access;
                       case eOpenFilesViewerColumnId_Size:
                         return a.size < b.size;
                       case eOpenFilesViewerColumnId_Path:
                         return strcmp(a.path.data, b.path.data) < 0;
                       default:
                         return false;
                       }
                     });
}

static Array<OpenFileEntry> copy_files(BumpArena &arena,
                                       const Array<OpenFileEntry> &src) {
  Array<OpenFileEntry> dst = Array<OpenFileEntry>::copy_from(arena, src);
  for (OpenFileEntry &file : dst) {
    file.path = String::copy_from(arena, file.path);
  }
  return dst;
}

static bool send_open_files_request(Sync &sync, const Pid pid) {
  return on_demand_send_request(sync,
                                sync.on_demand_reader.open_files_request_queue,
                                OpenFilesRequest{pid});
}

void open_files_viewer_request(OpenFilesViewerState &state, Sync &sync,
                               const Pid pid, const char *comm,
                               const ImGuiID dock_id,
                               const ProcessWindowFlags extra_flags) {
  if (on_demand_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  OpenFilesViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  on_demand_window_init(win->od, pid, comm, dock_id, extra_flags);
  win->od.sorted_by = eOpenFilesViewerColumnId_Fd;
  win->od.sorted_order = ImGuiSortDirection_Ascending;

  if (!send_open_files_request(sync, pid)) {
    on_demand_mark_request_dropped(win->od);
  }

  on_demand_sort_added(state.windows);
}

void open_files_viewer_update(OpenFilesViewerState &state, Sync &sync) {
  OpenFilesResponse response;
  while (sync.on_demand_reader.open_files_response_queue.pop(response)) {
    OpenFilesViewerWindow *win = on_demand_find(state.windows, response.pid);
    if (win && on_demand_apply_response(win->od, response.error_code)) {
      if (win->files.size > 0) ++state.updates_since_last_cleanup;
      win->files = copy_files(state.cur_arena, response.files);
      sort_open_files(*win);
    }
    response.owner_arena.destroy();
  }

  // Compact arena if wasted too much
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_OPEN_FILES) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (OpenFilesViewerWindow &win : state.windows) {
      if (win.files.size > 0) {
        win.files = copy_files(new_arena, win.files);
      }
    }

    state.cur_arena = new_arena;
    state.updates_since_last_cleanup = 0;
    old_arena.destroy();
  }
}

void open_files_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  OpenFilesViewerState &my_state = view_state.open_files_viewer_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    OpenFilesViewerWindow &win = my_state.windows.data()[last];

    bool keep_open = true;
    if (on_demand_window_begin(view_state, win.od, "Open Files", win.files.size,
                               ctx.frame_arena, keep_open)) {
      if (win.od.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.od.error_code);
      } else if (win.files.size > 0 ||
                 win.od.status == eOnDemandViewerStatus_Ready) {
        ImGuiTextFilter filter;
        bool refresh = false;
        if (on_demand_toolbar_begin(win.od, filter, "##OpenFilesFilter")) {
          refresh = on_demand_toolbar_end(win.od);
        }
        if (refresh) {
          on_demand_refresh_status(
              win.od, send_open_files_request(*view_state.sync, win.od.pid));
        }

        if (win.files.size == 0) {
          ImGui::TextDisabled("No open files");
        } else if (ImGui::BeginTable("OpenFiles",
                                     eOpenFilesViewerColumnId_Count,
                                     COMMON_TABLE_FLAGS)) {
          ui_push_mono_font();
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("FD",
                                  ImGuiTableColumnFlags_DefaultSort |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eOpenFilesViewerColumnId_Fd);
          ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eOpenFilesViewerColumnId_Type);
          ImGui::TableSetupColumn("Access", ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eOpenFilesViewerColumnId_Access);
          ImGui::TableSetupColumn("Size",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eOpenFilesViewerColumnId_Size);
          ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch,
                                  0.0f, eOpenFilesViewerColumnId_Path);
          ImGui::TableHeadersRow();

          handle_table_sort_specs(win.od.sorted_by, win.od.sorted_order,
                                  [&] { sort_open_files(win); });

          for (uint32_t j = 0; j < win.files.size; ++j) {
            const OpenFileEntry &file = win.files.data[j];

            const String filter_str =
                String::sprintf(ctx.frame_arena, "%d %s %s %s", file.fd,
                                fd_type_name(file.type),
                                fd_access_name(file.access), file.path.data);
            if (!filter.PassFilter(filter_str.data)) continue;

            const bool is_selected =
                win.od.selected_index == static_cast<int>(j);
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableNextRow();

            // FD
            ImGui::TableSetColumnIndex(eOpenFilesViewerColumnId_Fd);
            const String fd_label =
                String::sprintf(ctx.frame_arena, "%d", file.fd);
            if (ImGui::Selectable(fd_label.data, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns) ||
                ImGui::IsItemFocused()) {
              win.od.selected_index = static_cast<int>(j);
            }

            if (ui_context_menu(is_selected, win.od.context_menu_column,
                                eOpenFilesViewerColumnId_Count)) {
              win.od.selected_index = static_cast<int>(j);
              const String cell = open_file_cell_text(
                  ctx.frame_arena, file, win.od.context_menu_column);
              if (ImGui::MenuItemEx(
                      copy_cell_menu_label(ctx.frame_arena, cell).data,
                      ICON_MD_CONTENT_COPY)) {
                clipboard_copy_cell(view_state.notifications, cell);
              }
              if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
                copy_open_file_row(view_state.notifications, ctx.frame_arena,
                                   file);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_open_files(view_state.notifications, ctx.frame_arena,
                                    win);
              }
              ImGui::EndPopup();
            }

            // Type
            ImGui::TableSetColumnIndex(eOpenFilesViewerColumnId_Type);
            ImGui::TextUnformatted(fd_type_name(file.type));

            // Access
            ImGui::TableSetColumnIndex(eOpenFilesViewerColumnId_Access);
            if (file.access == eFdAccess_Unknown) {
              ImGui::TextDisabled("-");
            } else {
              ImGui::TextUnformatted(fd_access_name(file.access));
            }

            // Size
            ImGui::TableSetColumnIndex(eOpenFilesViewerColumnId_Size);
            if (file.size < 0) {
              table_item_draw_dim("-");
            } else {
              table_item_draw_memory(static_cast<double>(file.size));
            }

            // Path
            ImGui::TableSetColumnIndex(eOpenFilesViewerColumnId_Path);
            ui_path_text(ctx.frame_arena, file.path);
            ImGui::PopID();
          }

          ui_pop_mono_font();
          ImGui::EndTable();

          // Ctrl+C to copy selected row
          if (shortcut_copy_row(win.od.selected_index, win.files.size)) {
            copy_open_file_row(view_state.notifications, ctx.frame_arena,
                               win.files.data[win.od.selected_index]);
          }
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
