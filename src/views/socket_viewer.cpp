#include "socket_viewer.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

static const char *tcp_state_name(const int state) {
  switch (state) {
  case eTcpState_ESTABLISHED:
    return "ESTABLISHED";
  case eTcpState_SYN_SENT:
    return "SYN_SENT";
  case eTcpState_SYN_RECV:
    return "SYN_RECV";
  case eTcpState_FIN_WAIT1:
    return "FIN_WAIT1";
  case eTcpState_FIN_WAIT2:
    return "FIN_WAIT2";
  case eTcpState_TIME_WAIT:
    return "TIME_WAIT";
  case eTcpState_CLOSE:
    return "CLOSE";
  case eTcpState_CLOSE_WAIT:
    return "CLOSE_WAIT";
  case eTcpState_LAST_ACK:
    return "LAST_ACK";
  case eTcpState_LISTEN:
    return "LISTEN";
  case eTcpState_CLOSING:
    return "CLOSING";
  default:
    return "UNKNOWN";
  }
}

static const char *protocol_name(const int protocol) {
  switch (protocol) {
  case eSocketProtocol_TCP:
    return "TCP";
  case eSocketProtocol_UDP:
    return "UDP";
  case eSocketProtocol_TCP6:
    return "TCP6";
  case eSocketProtocol_UDP6:
    return "UDP6";
  default:
    return "???";
  }
}

// Format IPv4 address from network byte order
static void format_ipv4(char *buf, size_t buf_size, unsigned int ip,
                        unsigned short port) {
  snprintf(buf, buf_size, "%u.%u.%u.%u:%u", (ip >> 0) & 0xFF, (ip >> 8) & 0xFF,
           (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, port);
}

// Format IPv6 address
static void format_ipv6(char *buf, size_t buf_size, const unsigned char *ip,
                        unsigned short port) {
  // Check for IPv4-mapped IPv6 (::ffff:x.x.x.x)
  bool is_v4_mapped = true;
  for (int i = 0; i < 10; ++i) {
    if (ip[i] != 0) {
      is_v4_mapped = false;
      break;
    }
  }
  if (is_v4_mapped && ip[10] == 0xFF && ip[11] == 0xFF) {
    snprintf(buf, buf_size, "::ffff:%u.%u.%u.%u:%u", ip[12], ip[13], ip[14],
             ip[15], port);
    return;
  }

  // Check for loopback (::1)
  bool is_loopback = true;
  for (int i = 0; i < 15; ++i) {
    if (ip[i] != 0) {
      is_loopback = false;
      break;
    }
  }
  if (is_loopback && ip[15] == 1) {
    snprintf(buf, buf_size, "::1:%u", port);
    return;
  }

  // Check for all zeros (::)
  bool is_any = true;
  for (int i = 0; i < 16; ++i) {
    if (ip[i] != 0) {
      is_any = false;
      break;
    }
  }
  if (is_any) {
    snprintf(buf, buf_size, ":::%u", port);
    return;
  }

  // Full IPv6 format (simplified)
  snprintf(buf, buf_size,
           "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%"
           "02x%02x:%u",
           ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9],
           ip[10], ip[11], ip[12], ip[13], ip[14], ip[15], port);
}

static void format_address(char *buf, size_t buf_size, const SocketEntry &sock,
                           bool local) {
  const bool is_ipv6 = (sock.protocol == eSocketProtocol_TCP6 ||
                        sock.protocol == eSocketProtocol_UDP6);
  if (is_ipv6) {
    format_ipv6(buf, buf_size, local ? sock.local_ip6 : sock.remote_ip6,
                local ? sock.local_port : sock.remote_port);
  } else {
    format_ipv4(buf, buf_size, local ? sock.local_ip : sock.remote_ip,
                local ? sock.local_port : sock.remote_port);
  }
}

const char *SOCKET_COPY_HEADER =
    "Protocol\tLocal Address\tRemote Address\tState\tRecv-Q\tSend-Q\n";

static void copy_socket_row(const SocketEntry &sock) {
  char local_addr[64], remote_addr[64];
  format_address(local_addr, sizeof(local_addr), sock, true);
  format_address(remote_addr, sizeof(remote_addr), sock, false);

  const bool is_tcp = (sock.protocol == eSocketProtocol_TCP ||
                       sock.protocol == eSocketProtocol_TCP6);
  char buf[512];
  snprintf(buf, sizeof(buf), "%s%s\t%s\t%s\t%s\t%u\t%u", SOCKET_COPY_HEADER,
           protocol_name(sock.protocol), local_addr, remote_addr,
           is_tcp ? tcp_state_name(sock.state) : "-", sock.rx_queue,
           sock.tx_queue);
  ImGui::SetClipboardText(buf);
}

static void copy_all_sockets(BumpArena &arena, const SocketViewerWindow &win) {
  const size_t buf_size = 128 + win.sockets.size * 256;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", SOCKET_COPY_HEADER);

  for (size_t i = 0; i < win.sockets.size; ++i) {
    const SocketEntry &sock = win.sockets.data[i];
    char local_addr[64], remote_addr[64];
    format_address(local_addr, sizeof(local_addr), sock, true);
    format_address(remote_addr, sizeof(remote_addr), sock, false);

    const bool is_tcp = (sock.protocol == eSocketProtocol_TCP ||
                         sock.protocol == eSocketProtocol_TCP6);
    ptr += snprintf(ptr, buf_size - (ptr - buf), "%s\t%s\t%s\t%s\t%u\t%u\n",
                    protocol_name(sock.protocol), local_addr, remote_addr,
                    is_tcp ? tcp_state_name(sock.state) : "-", sock.rx_queue,
                    sock.tx_queue);
  }
  ImGui::SetClipboardText(buf);
}

static void sort_sockets(SocketViewerWindow &win) {
  if (win.sockets.size == 0) return;

  const auto compare = [&](const SocketEntry &a, const SocketEntry &b) {
    switch (win.sorted_by) {
    case eSocketViewerColumnId_Protocol:
      return a.protocol < b.protocol;
    case eSocketViewerColumnId_LocalAddress:
      if (a.local_ip != b.local_ip) return a.local_ip < b.local_ip;
      return a.local_port < b.local_port;
    case eSocketViewerColumnId_RemoteAddress:
      if (a.remote_ip != b.remote_ip) return a.remote_ip < b.remote_ip;
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
  };

  if (win.sorted_order == ImGuiSortDirection_Ascending) {
    std::stable_sort(win.sockets.data, win.sockets.data + win.sockets.size,
                     compare);
  } else {
    std::stable_sort(win.sockets.data, win.sockets.data + win.sockets.size,
                     [&](const SocketEntry &a, const SocketEntry &b) {
                       return compare(b, a);
                     });
  }
}

static void send_socket_request(Sync &sync, const int pid) {
  const SocketRequest req = {pid};
  sync.on_demand_reader.socket_request_queue.push(req);
  sync.on_demand_reader.library_cv.notify_one();
}

void socket_viewer_request(SocketViewerState &state, Sync &sync, const int pid,
                           const char *comm, const ImGuiID dock_id,
                           const ProcessWindowFlags extra_flags) {
  if (process_window_focus(state.windows, pid)) {
    return;
  }

  SocketViewerWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->status = eSocketViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_index = -1;

  send_socket_request(sync, pid);

  common_views_sort_added(state.windows);
}

void socket_viewer_update(SocketViewerState &state, Sync &sync) {
  // Process responses
  SocketResponse response;
  while (sync.on_demand_reader.socket_response_queue.pop(response)) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
      SocketViewerWindow &win = state.windows.data()[i];
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eSocketViewerStatus_Ready;
          win.sockets = Array<SocketEntry>::create(state.cur_arena,
                                                   response.sockets.size);
          memcpy(win.sockets.data, response.sockets.data,
                 response.sockets.size * sizeof(SocketEntry));
        } else {
          win.status = eSocketViewerStatus_Error;
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
      SocketViewerWindow &win = state.windows.data()[i];
      if (win.sockets.size > 0) {
        Array<SocketEntry> new_sockets =
            Array<SocketEntry>::create(new_arena, win.sockets.size);
        memcpy(new_sockets.data, win.sockets.data,
               win.sockets.size * sizeof(SocketEntry));
        win.sockets = new_sockets;
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void socket_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  SocketViewerState &my_state = view_state.socket_viewer_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    SocketViewerWindow &win = my_state.windows.data()[last];
    char title[128];
    if (win.status == eSocketViewerStatus_Error) {
      snprintf(title, sizeof(title), "Sockets: %s (%d) - Error###Sockets%d",
               win.process_name, win.pid, win.pid);
    } else if (win.status == eSocketViewerStatus_Loading) {
      snprintf(title, sizeof(title),
               "Sockets: %s (%d) - Loading...###Sockets%d", win.process_name,
               win.pid, win.pid);
    } else {
      snprintf(title, sizeof(title),
               "Sockets: %s (%d) - %zu sockets###Sockets%d", win.process_name,
               win.pid, win.sockets.size, win.pid);
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

      if (win.status == eSocketViewerStatus_Error) {
        ImGui::TextWrapped("%s", win.error_message);
      } else if (win.sockets.size > 0 ||
                 win.status == eSocketViewerStatus_Ready) {
        ImGuiTextFilter filter = draw_filter_input(
            "##SockFilter", win.filter_text, sizeof(win.filter_text));
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
          win.status = eSocketViewerStatus_Loading;
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
                                  [&]() { sort_sockets(win); });

          char local_addr[64], remote_addr[64];
          for (size_t j = 0; j < win.sockets.size; ++j) {
            const SocketEntry &sock = win.sockets.data[j];

            // Build filter string
            format_address(local_addr, sizeof(local_addr), sock, true);
            format_address(remote_addr, sizeof(remote_addr), sock, false);

            char filter_str[256];
            snprintf(filter_str, sizeof(filter_str), "%s %s %s %s",
                     protocol_name(sock.protocol), local_addr, remote_addr,
                     tcp_state_name(sock.state));
            if (!filter.PassFilter(filter_str)) continue;

            const bool is_selected =
                (win.selected_index == static_cast<int>(j));
            ImGui::TableNextRow();

            // Protocol
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_Protocol);
            if (ImGui::Selectable(protocol_name(sock.protocol), is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
              win.selected_index = static_cast<int>(j);
            }

            if (ImGui::BeginPopupContextItem()) {
              win.selected_index = static_cast<int>(j);
              if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                copy_socket_row(sock);
              }
              if (ImGui::MenuItem("Copy All")) {
                copy_all_sockets(ctx.frame_arena, win);
              }
              ImGui::EndPopup();
            }

            // Local Address
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_LocalAddress);
            ImGui::TextUnformatted(local_addr);

            // Remote Address
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_RemoteAddress);
            ImGui::TextUnformatted(remote_addr);

            // State
            ImGui::TableSetColumnIndex(eSocketViewerColumnId_State);
            const bool is_tcp = (sock.protocol == eSocketProtocol_TCP ||
                                 sock.protocol == eSocketProtocol_TCP6);
            if (is_tcp) {
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
          }

          ImGui::EndTable();

          // Ctrl+C to copy selected row
          if (win.selected_index >= 0 &&
              ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
            copy_socket_row(win.sockets.data[win.selected_index]);
          }
        }
      }
    }
    process_window_handle_focus(win.flags);
    ImGui::End();
    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += win.sockets.size * sizeof(SocketEntry);
    }
  }
  my_state.windows.shrink_to(last);
}
