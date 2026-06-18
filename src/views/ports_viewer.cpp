#include "ports_viewer.h"

#include "views/common.h"
#include "views/icons.h"
#include "views/notifications.h"
#include "views/socket_format.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>

static const char *PORTS_TITLE = "Ports";

const char *PORTS_COPY_HEADER =
    "Protocol\tLocal Address\tState\tPID\tProcess\n";

static void send_port_scan_request(Sync &sync) {
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.on_demand_reader.port_scan_request_queue.push({});
  }
  sync.on_demand_reader.request_read_cv.notify_one();
}

static void sort_ports(PortsViewerState &state) {
  sort_bidirectional(state.entries.data, state.entries.size, state.sorted_order,
                     [&](const PortEntry &a, const PortEntry &b) {
                       switch (state.sorted_by) {
                       case ePortsViewerColumnId_Protocol:
                         return a.sock.protocol < b.sock.protocol;
                       case ePortsViewerColumnId_LocalAddress:
                         if (a.sock.local_port != b.sock.local_port)
                           return a.sock.local_port < b.sock.local_port;
                         return a.sock.local_ip < b.sock.local_ip;
                       case ePortsViewerColumnId_State:
                         return a.sock.state < b.sock.state;
                       case ePortsViewerColumnId_Pid:
                         return a.pid < b.pid;
                       case ePortsViewerColumnId_Process:
                         return strcasecmp(a.comm, b.comm) < 0;
                       default:
                         return false;
                       }
                     });
}

void ports_viewer_update(PortsViewerState &state, Sync &sync) {
  PortScanResponse response;
  while (sync.on_demand_reader.port_scan_response_queue.pop(response)) {
    state.cur_arena.destroy();
    state.cur_arena = BumpArena::create();
    state.entries =
        Array<PortEntry>::copy_from(state.cur_arena, response.entries);
    state.permission_limited = response.permission_limited;
    state.scan_error_code = response.error_code;
    state.status = ePortsViewerStatus_Ready;
    if (state.selected_index >= static_cast<int>(state.entries.size)) {
      state.selected_index = -1;
    }
    sort_ports(state);
    state.last_updated = ImGui::GetTime();
    response.owner_arena.destroy();
  }
}

static void kill_selected(PortsViewerState &state, Notifications &notifications,
                          const int sig) {
  if (state.selected_index < 0 ||
      state.selected_index >= static_cast<int>(state.entries.size)) {
    return;
  }
  const Pid pid = state.entries.data[state.selected_index].pid;
  if (kill(pid, sig) == 0) {
    return;
  }
  const int err = errno;
  notify_error(notifications, err, "Failed to kill %d: %s", pid, strerror(err));
}

static String port_cell_text(BumpArena &arena, const PortEntry &e,
                             const int column) {
  switch (column) {
  case ePortsViewerColumnId_Protocol:
    return String::static_string(protocol_name(e.sock.protocol));
  case ePortsViewerColumnId_LocalAddress:
    return format_address(arena, e.sock, true);
  case ePortsViewerColumnId_State:
    return String::static_string(
        socket_state_name(e.sock.protocol, e.sock.state));
  case ePortsViewerColumnId_Pid:
    return String::sprintf(arena, "%d", e.pid);
  case ePortsViewerColumnId_Process:
    return String::static_string(e.comm);
  default:
    return String::static_string("");
  }
}

static void copy_port_row(Notifications &notifications, BumpArena &frame_arena,
                          const PortEntry &e) {
  const String local_addr = format_address(frame_arena, e.sock, true);
  const String buf = String::sprintf(
      frame_arena, "%s%s\t%s\t%s\t%d\t%s", PORTS_COPY_HEADER,
      protocol_name(e.sock.protocol), local_addr.data,
      socket_state_name(e.sock.protocol, e.sock.state), e.pid, e.comm);
  clipboard_copy_row(notifications, buf.data);
}

static void copy_all_ports(Notifications &notifications, BumpArena &arena,
                           const PortsViewerState &state) {
  copy_all_to_clipboard(
      notifications, arena, state.entries.data, state.entries.size, 128,
      PORTS_COPY_HEADER, [&arena](char *ptr, size_t rem, const PortEntry &e) {
        const String local_addr = format_address(arena, e.sock, true);
        return snprintf(ptr, rem, "%s\t%s\t%s\t%d\t%s\n",
                        protocol_name(e.sock.protocol), local_addr.data,
                        socket_state_name(e.sock.protocol, e.sock.state), e.pid,
                        e.comm);
      });
}

void ports_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  PortsViewerState &state = view_state.ports_viewer_state;

  // Dock as a tab next to the process table on first run, unless a saved
  // layout already places it somewhere.
  if (!state.layout_inited) {
    state.layout_inited = true;
    const ImGuiWindow *ports_win = ImGui::FindWindowByName(PORTS_TITLE);
    if (!ports_win || ports_win->DockId == 0) {
      const ImGuiWindow *table_win = ImGui::FindWindowByName("###ProcessTable");
      if (table_win && table_win->DockId != 0) {
        ImGui::SetNextWindowDockID(table_win->DockId, ImGuiCond_Always);
      }
    }
  }

  ImGui::SetNextWindowSize(ImVec2(640, 400), ImGuiCond_FirstUseEver);
  const bool visible = ImGui::Begin(PORTS_TITLE, nullptr, COMMON_VIEW_FLAGS);

  // Scan whenever the tab is brought to the foreground; data is otherwise
  // static until the user hits Refresh.
  if (visible && !state.was_visible) {
    state.status = ePortsViewerStatus_Loading;
    state.focus_filter = true;
    send_port_scan_request(*view_state.sync);
  }
  state.was_visible = visible;

  if (!visible) {
    ImGui::End();
    return;
  }

  ImGuiTextFilter filter;
  if (ImGui::BeginTable("Header", state.permission_limited ? 5 : 4,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
    if (state.permission_limited) {
      ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
    }
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch,
                            HEADER_SPACER_WEIGHT);
    ImGui::TableNextRow();

    if (state.focus_filter) {
      ImGui::SetKeyboardFocusHere();
      state.focus_filter = false;
    }

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    filter = draw_filter_input("##PortsFilter", state.filter_text,
                               sizeof(state.filter_text));

    if (state.permission_limited) {
      ImGui::TableNextColumn();
      if (ImGui::Button(ICON_MD_SHIELD " Run with privileges")) {
        restart_with_pkexec();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Some processes are owned by other users; restart with pkexec to "
            "see them.");
      }
    }

    ImGui::TableNextColumn();
    if (draw_refresh_button(state.status == ePortsViewerStatus_Loading)) {
      state.status = ePortsViewerStatus_Loading;
      send_port_scan_request(*view_state.sync);
    }

    ImGui::TableNextColumn();
    draw_last_updated(state.last_updated);

    ImGui::TableNextColumn(); // spacer

    ImGui::EndTable();
  }

  if (state.scan_error_code != 0) {
    draw_error_with_pkexec(state.scan_error_code);
  }

  const bool filter_active = filter.IsActive();
  bool selected_visible = false;
  if (ImGui::BeginTable("PortsTable", ePortsViewerColumnId_Count,
                        COMMON_TABLE_FLAGS)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Proto", ImGuiTableColumnFlags_WidthFixed, 50.0f,
                            ePortsViewerColumnId_Protocol);
    ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_None, 0.0f,
                            ePortsViewerColumnId_LocalAddress);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f,
                            ePortsViewerColumnId_State);
    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 60.0f,
                            ePortsViewerColumnId_Pid);
    ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthFixed, 160.0f,
                            ePortsViewerColumnId_Process);
    ImGui::TableHeadersRow();

    handle_table_sort_specs(state.sorted_by, state.sorted_order,
                            [&] { sort_ports(state); });

    for (uint32_t i = 0; i < state.entries.size; ++i) {
      const PortEntry &e = state.entries.data[i];
      const String local_addr = format_address(ctx.frame_arena, e.sock, true);

      // Only build the filter string when a filter is active; PassFilter on an
      // empty filter trivially matches every row.
      if (filter_active) {
        char filter_str[320];
        snprintf(filter_str, sizeof(filter_str), "%s %s %s %d %s",
                 protocol_name(e.sock.protocol), local_addr.data,
                 socket_state_name(e.sock.protocol, e.sock.state), e.pid,
                 e.comm);
        if (!filter.PassFilter(filter_str)) continue;
      }

      const bool is_selected = (state.selected_index == static_cast<int>(i));
      if (is_selected) selected_visible = true;
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(ePortsViewerColumnId_Protocol);
      if (ImGui::Selectable(protocol_name(e.sock.protocol), is_selected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        state.selected_index = static_cast<int>(i);
      }

      const int copy_column = table_context_column(ePortsViewerColumnId_Count);
      if (ImGui::BeginPopupContextItem()) {
        state.selected_index = static_cast<int>(i);
        const String cell = port_cell_text(ctx.frame_arena, e, copy_column);
        if (ImGui::MenuItemEx(copy_cell_menu_label(ctx.frame_arena, cell).data,
                              ICON_MD_CONTENT_COPY)) {
          clipboard_copy_cell(view_state.notifications, cell);
        }
        if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
          copy_port_row(view_state.notifications, ctx.frame_arena, e);
        }
        if (ImGui::MenuItem("Copy All")) {
          copy_all_ports(view_state.notifications, ctx.frame_arena, state);
        }
        ImGui::Separator();
        if (ImGui::MenuItemEx("Kill Process", ICON_MD_DELETE, "Del")) {
          kill_selected(state, view_state.notifications, SIGTERM);
        }
        if (ImGui::MenuItem("Force Kill")) {
          kill_selected(state, view_state.notifications, SIGKILL);
        }
        ImGui::EndPopup();
      }

      ImGui::TableSetColumnIndex(ePortsViewerColumnId_LocalAddress);
      ImGui::TextUnformatted(local_addr.data);

      ImGui::TableSetColumnIndex(ePortsViewerColumnId_State);
      ImGui::TextUnformatted(socket_state_name(e.sock.protocol, e.sock.state));

      ImGui::TableSetColumnIndex(ePortsViewerColumnId_Pid);
      ImGui::Text("%d", e.pid);

      ImGui::TableSetColumnIndex(ePortsViewerColumnId_Process);
      ImGui::TextUnformatted(e.comm);

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  // Honor Delete only when the selected row is visible in the current
  // (possibly filtered) list, so a hidden selection can't be killed blindly.
  if (selected_visible && ImGui::Shortcut(ImGuiKey_Delete)) {
    kill_selected(state, view_state.notifications, SIGTERM);
  }
  if (selected_visible && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
    copy_port_row(view_state.notifications, ctx.frame_arena,
                  state.entries.data[state.selected_index]);
  }

  ImGui::End();
}
