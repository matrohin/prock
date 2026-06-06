#include "socket_viewer.h"

#include "base/string.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/socket_format.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

#include <cerrno>
#include <cstring>

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_SOCKETS = 5;

const char *SOCKET_COPY_HEADER =
    "Protocol\tLocal Address\tRemote Address\tState\tRecv-Q\tSend-Q\n";

static void copy_socket_row(BumpArena &frame_arena, const SocketEntry &sock) {
  const String local_addr = format_address(frame_arena, sock, true);
  const String remote_addr = format_address(frame_arena, sock, false);

  const String buf = String::sprintf(
      frame_arena, "%s%s\t%s\t%s\t%s\t%u\t%u", SOCKET_COPY_HEADER,
      protocol_name(sock.protocol), local_addr.data, remote_addr.data,
      is_tcp(sock.protocol) ? tcp_state_name(sock.state) : "-", sock.rx_queue,
      sock.tx_queue);
  ImGui::SetClipboardText(buf.data);
}

static void copy_all_sockets(BumpArena &arena, const SocketViewerWindow &win) {
  copy_all_to_clipboard(
      arena, win.sockets.data, win.sockets.size, 256, SOCKET_COPY_HEADER,
      [&arena](char *ptr, size_t rem, const SocketEntry &sock) {
        const String local_addr = format_address(arena, sock, true);
        const String remote_addr = format_address(arena, sock, false);
        return snprintf(
            ptr, rem, "%s\t%s\t%s\t%s\t%u\t%u\n", protocol_name(sock.protocol),
            local_addr.data, remote_addr.data,
            is_tcp(sock.protocol) ? tcp_state_name(sock.state) : "-",
            sock.rx_queue, sock.tx_queue);
      });
}

static void sort_sockets(const SocketViewerWindow &win) {
  sort_bidirectional(win.sockets.data, win.sockets.size, win.sorted_order,
                     [&](const SocketEntry &a, const SocketEntry &b) {
                       switch (win.sorted_by) {
                       case eSocketViewerColumnId_Protocol:
                         return a.protocol < b.protocol;
                       case eSocketViewerColumnId_LocalAddress:
                         if (a.local_ip != b.local_ip)
                           return a.local_ip < b.local_ip;
                         return a.local_port < b.local_port;
                       case eSocketViewerColumnId_RemoteAddress:
                         if (a.remote_ip != b.remote_ip)
                           return a.remote_ip < b.remote_ip;
                         return a.remote_port < b.remote_port;
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
  sync.on_demand_reader.socket_request_queue.push(req);
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
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_index = -1;

  send_socket_request(sync, pid);

  common_views_sort_added(state.windows);
}

void socket_viewer_update(SocketViewerState &state, Sync &sync) {
  SocketResponse response;
  while (sync.on_demand_reader.socket_response_queue.pop(response)) {
    for (SocketViewerWindow &win : state.windows) {
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eOnDemandViewerStatus_Ready;
          win.sockets = Array<SocketEntry>::create(state.cur_arena,
                                                   response.sockets.size);
          memcpy(win.sockets.data, response.sockets.data,
                 response.sockets.size * sizeof(SocketEntry));
        } else {
          win.status = eOnDemandViewerStatus_Error;
          win.error_code = response.error_code;
        }
        response.owner_arena.destroy();
        break;
      }
    }
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
    const String title = on_demand_viewer_title(
        ctx.frame_arena, win.status, "Sockets", "sockets", win.sockets.size,
        win.process_name, win.pid);
    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title.data);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title.data, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      if (win.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.error_code);
      } else if (win.sockets.size > 0 ||
                 win.status == eOnDemandViewerStatus_Ready) {
        ImGuiTextFilter filter = draw_filter_input(
            "##SockFilter", win.filter_text, sizeof(win.filter_text));
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
          win.status = eOnDemandViewerStatus_Loading;
          send_socket_request(*view_state.sync, win.pid);
        }

        if (win.sockets.size == 0) {
          ImGui::TextDisabled("No sockets");
        } else if (ImGui::BeginTable("Sockets", eSocketViewerColumnId_Count,
                                     COMMON_TABLE_FLAGS)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Proto", ImGuiTableColumnFlags_WidthFixed,
                                  50.0f, eSocketViewerColumnId_Protocol);
          ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_None,
                                  0.0f, eSocketViewerColumnId_LocalAddress);
          ImGui::TableSetupColumn("Remote Address", ImGuiTableColumnFlags_None,
                                  0.0f, eSocketViewerColumnId_RemoteAddress);
          ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed,
                                  90.0f, eSocketViewerColumnId_State);
          ImGui::TableSetupColumn("Recv-Q",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  60.0f, eSocketViewerColumnId_RecvQ);
          ImGui::TableSetupColumn("Send-Q",
                                  ImGuiTableColumnFlags_PreferSortDescending |
                                      ImGuiTableColumnFlags_WidthFixed,
                                  60.0f, eSocketViewerColumnId_SendQ);
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
                local_addr.data, remote_addr.data, tcp_state_name(sock.state));
            if (!filter.PassFilter(filter_str.data)) continue;

            const bool is_selected =
                (win.selected_index == static_cast<int>(j));
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableNextRow();

            // Protocol
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_Protocol);
            if (ImGui::Selectable(protocol_name(sock.protocol), is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }

            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              if (ImGui::MenuItemEx("Copy", ICON_MD_CONTENT_COPY, "Ctrl+C")) {
                copy_socket_row(ctx.frame_arena, sock);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_sockets(ctx.frame_arena, win);
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
            if (is_tcp(sock.protocol)) {
              ImGui::TextUnformatted(tcp_state_name(sock.state));
            } else {
              ImGui::TextDisabled("-");
            }

            // Recv-Q
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_RecvQ);
            if (sock.rx_queue > 0) {
              ImGui::Text("%u", sock.rx_queue);
            } else {
              ImGui::TextDisabled("0");
            }

            // Send-Q
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_SendQ);
            if (sock.tx_queue > 0) {
              ImGui::Text("%u", sock.tx_queue);
            } else {
              ImGui::TextDisabled("0");
            }
            ImGui::PopID();
          }

          ImGui::EndTable();

          // Ctrl+C to copy selected row
          if (win.selected_index >= 0 &&
              ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
            copy_socket_row(ctx.frame_arena,
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
