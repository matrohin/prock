#include "open_files_viewer.h"

#include "base/string.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/table_item.h"
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
  sort_bidirectional(win.files.data, win.files.size, win.sorted_order,
                     [&](const OpenFileEntry &a, const OpenFileEntry &b) {
                       switch (win.sorted_by) {
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

static void send_open_files_request(Sync &sync, const Pid pid) {
  const OpenFilesRequest req = {pid};
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.on_demand_reader.open_files_request_queue.push(req);
  }
  sync.on_demand_reader.request_read_cv.notify_one();
}

void open_files_viewer_request(OpenFilesViewerState &state, Sync &sync,
                               const Pid pid, const char *comm,
                               const ImGuiID dock_id,
                               const ProcessWindowFlags extra_flags) {
  if (process_window_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  OpenFilesViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  win->status = eOnDemandViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  snprintf(win->process_name, sizeof(win->process_name), "%s", comm);
  win->selected_index = -1;
  win->sorted_by = eOpenFilesViewerColumnId_Fd;
  win->sorted_order = ImGuiSortDirection_Ascending;
  win->last_updated = 0.0;

  send_open_files_request(sync, pid);

  common_views_sort_added(state.windows);
}

void open_files_viewer_update(OpenFilesViewerState &state, Sync &sync) {
  OpenFilesResponse response;
  while (sync.on_demand_reader.open_files_response_queue.pop(response)) {
    for (OpenFilesViewerWindow &win : state.windows) {
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eOnDemandViewerStatus_Ready;
          win.files =
              Array<OpenFileEntry>::copy_from(state.cur_arena, response.files);
          for (uint32_t j = 0; j < win.files.size; ++j) {
            win.files.data[j].path =
                String::copy_from(state.cur_arena, win.files.data[j].path);
          }
          sort_open_files(win);
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
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_OPEN_FILES) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (OpenFilesViewerWindow &win : state.windows) {
      if (win.files.size > 0) {
        win.files = Array<OpenFileEntry>::copy_from(new_arena, win.files);
        for (uint32_t j = 0; j < win.files.size; ++j) {
          win.files.data[j].path =
              String::copy_from(new_arena, win.files.data[j].path);
        }
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
    const String title =
        on_demand_viewer_title(ctx.frame_arena, win.status, "Open Files",
                               win.files.size, win.process_name, win.pid);
    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title.data);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title.data, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      if (win.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.error_code);
      } else if (win.files.size > 0 ||
                 win.status == eOnDemandViewerStatus_Ready) {
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
          draw_filter_input(filter, "##OpenFilesFilter", win.filter_text,
                            sizeof(win.filter_text));

          ImGui::TableNextColumn();
          if (draw_refresh_button()) {
            win.status = eOnDemandViewerStatus_Loading;
            send_open_files_request(*view_state.sync, win.pid);
          }

          ImGui::TableNextColumn();
          draw_last_updated(win.last_updated);

          ImGui::TableNextColumn(); // spacer

          ImGui::EndTable();
        }

        if (win.files.size == 0) {
          ImGui::TextDisabled("No open files");
        } else if (ImGui::BeginTable("OpenFiles",
                                     eOpenFilesViewerColumnId_Count,
                                     COMMON_TABLE_FLAGS)) {
          push_mono_font();
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

          handle_table_sort_specs(win.sorted_by, win.sorted_order,
                                  [&] { sort_open_files(win); });

          for (uint32_t j = 0; j < win.files.size; ++j) {
            const OpenFileEntry &file = win.files.data[j];

            const String filter_str =
                String::sprintf(ctx.frame_arena, "%d %s %s %s", file.fd,
                                fd_type_name(file.type),
                                fd_access_name(file.access), file.path.data);
            if (!filter.PassFilter(filter_str.data)) continue;

            const bool is_selected = win.selected_index == static_cast<int>(j);
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableNextRow();

            // FD
            ImGui::TableSetColumnIndex(eOpenFilesViewerColumnId_Fd);
            const String fd_label =
                String::sprintf(ctx.frame_arena, "%d", file.fd);
            if (ImGui::Selectable(fd_label.data, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }

            const int copy_column =
                table_context_column(eOpenFilesViewerColumnId_Count);
            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              const String cell =
                  open_file_cell_text(ctx.frame_arena, file, copy_column);
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
            ImGui::TextUnformatted(file.path.data);
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", file.path.data);
            }
            ImGui::PopID();
          }

          pop_mono_font();
          ImGui::EndTable();

          // Ctrl+C to copy selected row
          if (copy_row_shortcut(win.selected_index, win.files.size)) {
            copy_open_file_row(view_state.notifications, ctx.frame_arena,
                               win.files.data[win.selected_index]);
          }
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
