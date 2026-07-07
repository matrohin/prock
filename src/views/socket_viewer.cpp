#include "socket_viewer.h"

#include "base/string.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/socket_format.h"
#include "views/table_item.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

#include <cstring>

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_SOCKETS = 5;

const char *SOCKET_COPY_HEADER =
    "Protocol\tLocal Address\tRemote Address\tState\tRecv-Q\tSend-Q\n";

static String socket_cell_text(BumpArena &arena, const SocketEntry &sock,
                               const int column) {
  switch (column) {
  case eSocketViewerColumnId_Protocol:
    return String::static_string(protocol_name(sock.protocol));
  case eSocketViewerColumnId_LocalAddress:
    return format_address(arena, sock, true);
  case eSocketViewerColumnId_RemoteAddress:
    return format_address(arena, sock, false);
  case eSocketViewerColumnId_State:
    return String::static_string(socket_state_name(sock.protocol, sock.state));
  case eSocketViewerColumnId_RecvQ:
    return String::sprintf(arena, "%u", sock.rx_queue);
  case eSocketViewerColumnId_SendQ:
    return String::sprintf(arena, "%u", sock.tx_queue);
  default:
    return String::static_string("");
  }
}

static void copy_socket_row(Notifications &notifications,
                            BumpArena &frame_arena, const SocketEntry &sock) {
  const String local_addr = format_address(frame_arena, sock, true);
  const String remote_addr = format_address(frame_arena, sock, false);

  const String buf = String::sprintf(
      frame_arena, "%s%s\t%s\t%s\t%s\t%u\t%u", SOCKET_COPY_HEADER,
      protocol_name(sock.protocol), local_addr.data, remote_addr.data,
      socket_state_name(sock.protocol, sock.state), sock.rx_queue,
      sock.tx_queue);
  clipboard_copy_row(notifications, buf.data);
}

static void copy_all_sockets(Notifications &notifications, BumpArena &arena,
                             const SocketViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.sockets.data, win.sockets.size, 256,
      SOCKET_COPY_HEADER,
      [&arena](char *ptr, const size_t rem, const SocketEntry &sock) {
        const String local_addr = format_address(arena, sock, true);
        const String remote_addr = format_address(arena, sock, false);
        return snprintf(ptr, rem, "%s\t%s\t%s\t%s\t%u\t%u\n",
                        protocol_name(sock.protocol), local_addr.data,
                        remote_addr.data,
                        socket_state_name(sock.protocol, sock.state),
                        sock.rx_queue, sock.tx_queue);
      });
}

static void sort_sockets(const SocketViewerWindow &win) {
  sort_bidirectional(win.sockets.data, win.sockets.size, win.sorted_order,
                     [&](const SocketEntry &a, const SocketEntry &b) {
                       switch (win.sorted_by) {
                       case eSocketViewerColumnId_Protocol:
                         return a.protocol < b.protocol;
                       case eSocketViewerColumnId_LocalAddress: {
                         const int cmp = compare_address(a, b, true);
                         if (cmp != 0) return cmp < 0;
                         return a.local_port < b.local_port;
                       }
                       case eSocketViewerColumnId_RemoteAddress: {
                         const int cmp = compare_address(a, b, false);
                         if (cmp != 0) return cmp < 0;
                         return a.remote_port < b.remote_port;
                       }
                       case eSocketViewerColumnId_State:
                         return a.state < b.state;
                       case eSocketViewerColumnId_RecvQ:
                         return a.rx_queue < b.rx_queue;
                       case eSocketViewerColumnId_SendQ:
                         return a.tx_queue < b.tx_queue;
                       default:
                         return false;
                       }
                     });
}

static void send_socket_request(Sync &sync, const Pid pid) {
  const SocketRequest req = {pid};
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.on_demand_reader.socket_request_queue.push(req);
  }
  sync.on_demand_reader.request_read_cv.notify_one();
}

void socket_viewer_request(SocketViewerState &state, Sync &sync, const Pid pid,
                           const char *comm, const ImGuiID dock_id,
                           const ProcessWindowFlags extra_flags) {
  if (process_window_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  SocketViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  win->status = eOnDemandViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  snprintf(win->process_name, sizeof(win->process_name), "%s", comm);
  win->selected_index = -1;
  win->last_updated = 0.0;

  send_socket_request(sync, pid);

  common_views_sort_added(state.windows);
}

void socket_viewer_update(SocketViewerState &state, Sync &sync) {
  SocketResponse response;
  while (sync.on_demand_reader.socket_response_queue.pop(response)) {
    for (SocketViewerWindow &win : state.windows) {
      if (win.pid == response.pid) {
        if (response.error_code == 0 && response.netlink_error_code == 0) {
          win.status = eOnDemandViewerStatus_Ready;
          win.sockets = Array<SocketEntry>::create(state.cur_arena,
                                                   response.sockets.size);
          memcpy(win.sockets.data, response.sockets.data,
                 response.sockets.size * sizeof(SocketEntry));
          sort_sockets(win);
          win.last_updated = ImGui::GetTime();
        } else {
          win.status = eOnDemandViewerStatus_Error;
          win.error_code = response.error_code;
          win.netlink_error_code = response.netlink_error_code;
        }
        break;
      }
    }
    response.owner_arena.destroy();
  }

  // Compact arena if wasted too much
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_SOCKETS) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (SocketViewerWindow &win : state.windows) {
      win.sockets = Array<SocketEntry>::copy_from(new_arena, win.sockets);
    }

    state.cur_arena = new_arena;
    state.updates_since_last_cleanup = 0;
    old_arena.destroy();
  }
}

void socket_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  SocketViewerState &my_state = view_state.socket_viewer_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    SocketViewerWindow &win = my_state.windows.data()[last];
    const String title =
        on_demand_viewer_title(ctx.frame_arena, win.status, "Sockets",
                               win.sockets.size, win.process_name, win.pid);
    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title.data);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title.data, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      if (win.status == eOnDemandViewerStatus_Error) {
        if (win.netlink_error_code != 0) {
          draw_socket_query_error(win.netlink_error_code);
        } else {
          draw_error_with_pkexec(win.error_code);
        }
      } else if (win.sockets.size > 0 ||
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
          draw_filter_input(filter, "##SockFilter", win.filter_text,
                            sizeof(win.filter_text));

          ImGui::TableNextColumn();
          if (draw_refresh_button()) {
            win.status = eOnDemandViewerStatus_Loading;
            send_socket_request(*view_state.sync, win.pid);
          }

          ImGui::TableNextColumn();
          draw_last_updated(win.last_updated);

          ImGui::TableNextColumn(); // spacer

          ImGui::EndTable();
        }

        if (win.sockets.size == 0) {
          ImGui::TextDisabled("No sockets");
        } else if (ImGui::BeginTable("Sockets", eSocketViewerColumnId_Count,
                                     COMMON_TABLE_FLAGS)) {
          push_mono_font();
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Proto", ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eSocketViewerColumnId_Protocol);
          ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_None,
                                  0.0f, eSocketViewerColumnId_LocalAddress);
          ImGui::TableSetupColumn("Remote Address", ImGuiTableColumnFlags_None,
                                  0.0f, eSocketViewerColumnId_RemoteAddress);
          ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eSocketViewerColumnId_State);
          ImGui::TableSetupColumn("Recv-Q",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eSocketViewerColumnId_RecvQ);
          ImGui::TableSetupColumn("Send-Q",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  0.0f, eSocketViewerColumnId_SendQ);
          ImGui::TableHeadersRow();

          handle_table_sort_specs(win.sorted_by, win.sorted_order,
                                  [&] { sort_sockets(win); });

          for (uint32_t j = 0; j < win.sockets.size; ++j) {
            const SocketEntry &sock = win.sockets.data[j];

            // Build filter string
            const String local_addr =
                format_address(ctx.frame_arena, sock, true);
            const String remote_addr =
                format_address(ctx.frame_arena, sock, false);

            const String filter_str = String::sprintf(
                ctx.frame_arena, "%s %s %s %s", protocol_name(sock.protocol),
                local_addr.data, remote_addr.data,
                socket_state_name(sock.protocol, sock.state));
            if (!filter.PassFilter(filter_str.data)) continue;

            const bool is_selected = win.selected_index == static_cast<int>(j);
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableNextRow();

            // Protocol
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_Protocol);
            if (ImGui::Selectable(protocol_name(sock.protocol), is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }

            const int copy_column =
                table_context_column(eSocketViewerColumnId_Count);
            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              const String cell =
                  socket_cell_text(ctx.frame_arena, sock, copy_column);
              if (ImGui::MenuItemEx(
                      copy_cell_menu_label(ctx.frame_arena, cell).data,
                      ICON_MD_CONTENT_COPY)) {
                clipboard_copy_cell(view_state.notifications, cell);
              }
              if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
                copy_socket_row(view_state.notifications, ctx.frame_arena,
                                sock);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_sockets(view_state.notifications, ctx.frame_arena,
                                 win);
              }
              ImGui::EndPopup();
            }

            // Local Address
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_LocalAddress);
            ImGui::TextUnformatted(local_addr.data);

            // Remote Address
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_RemoteAddress);
            ImGui::TextUnformatted(remote_addr.data);

            // State
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_State);
            ImGui::TextUnformatted(
                socket_state_name(sock.protocol, sock.state));

            // Recv-Q
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_RecvQ);
            if (sock.rx_queue > 0) {
              table_item_draw_long(sock.rx_queue);
            } else {
              table_item_draw_dim("0");
            }

            // Send-Q
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_SendQ);
            if (sock.tx_queue > 0) {
              table_item_draw_long(sock.tx_queue);
            } else {
              table_item_draw_dim("0");
            }
            ImGui::PopID();
          }

          pop_mono_font();
          ImGui::EndTable();

          // Ctrl+C to copy selected row
          if (copy_row_shortcut(win.selected_index, win.sockets.size)) {
            copy_socket_row(view_state.notifications, ctx.frame_arena,
                            win.sockets.data[win.selected_index]);
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
