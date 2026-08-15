#include "properties_viewer.h"

#include "state/raw_stats.h"
#include "state/state.h"
#include "views/common.h"
#include "views/icons.h"
#include "views/ui.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "on_demand_common.h"
#include "tracy/Tracy.hpp"

#include <cstdint>
#include <cstdio>
#include <mutex>

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES = 5;

enum PropSection {
  ePropSection_Process,
  ePropSection_User,
  ePropSection_Paths,
  ePropSection_Placement,
  ePropSection_Security,
};

static const char *PROP_SECTIONS[] = {"Process", "User", "Paths", "Placement",
                                      "Security"};

struct PropEntry {
  const char *label;
  String value;
  int section;
};

static const char *seccomp_label(const int mode) {
  switch (mode) {
  case 0:
    return "disabled";
  case 1:
    return "strict";
  case 2:
    return "filter";
  default:
    return "-";
  }
}

static uint32_t build_property_rows(BumpArena &arena,
                                    const ProcessProperties &p,
                                    const State &state, PropEntry *out,
                                    const uint32_t cap) {
  uint32_t n = 0;
  const auto add = [&](const int section, const char *label,
                       const String value) {
    if (n < cap) out[n++] = PropEntry{label, value, section};
  };

  const String dash = String::static_string("-");

  // Process
  add(ePropSection_Process, "Name", p.comm);
  add(ePropSection_Process, "PID", String::sprintf(arena, "%d", p.pid));
  if (p.parent_name.len > 0) {
    add(ePropSection_Process, "Parent",
        String::sprintf(arena, "%d (%s)", p.ppid, p.parent_name.data));
  } else {
    add(ePropSection_Process, "Parent", String::sprintf(arena, "%d", p.ppid));
  }
  {
    const SystemInfo &sys = state.system;
    const int64_t epoch =
        sys.boot_time_epoch_sec != 0 && sys.ticks_in_second != 0
            ? static_cast<int64_t>(sys.boot_time_epoch_sec +
                                   p.starttime / sys.ticks_in_second)
            : 0;
    char buf[64];
    format_start_time_full(epoch, buf, sizeof(buf));
    add(ePropSection_Process, "Started", String::copy_from(arena, buf));
  }
  add(ePropSection_Process, "TTY", p.tty);

  // User
  add(ePropSection_User, "User",
      String::sprintf(arena, "%s (%u)", p.username.data,
                      static_cast<unsigned>(p.uid)));
  add(ePropSection_User, "Group",
      String::sprintf(arena, "%s (%u)", p.groupname.data,
                      static_cast<unsigned>(p.gid)));
  if (p.euid != p.uid) {
    add(ePropSection_User, "Effective UID",
        String::sprintf(arena, "%u", static_cast<unsigned>(p.euid)));
  }
  if (p.egid != p.gid) {
    add(ePropSection_User, "Effective GID",
        String::sprintf(arena, "%u", static_cast<unsigned>(p.egid)));
  }
  if (p.suid != p.uid) {
    add(ePropSection_User, "Saved-set UID",
        String::sprintf(arena, "%u", static_cast<unsigned>(p.suid)));
  }
  if (p.sgid != p.gid) {
    add(ePropSection_User, "Saved-set GID",
        String::sprintf(arena, "%u", static_cast<unsigned>(p.sgid)));
  }
  add(ePropSection_User, "Groups", p.groups.len > 0 ? p.groups : dash);
  if (p.umask >= 0) {
    add(ePropSection_User, "Umask", String::sprintf(arena, "%04o", p.umask));
  }

  // Paths
  add(ePropSection_Paths, "Executable", p.exe_ok ? p.exe : dash);
  add(ePropSection_Paths, "Working Directory", p.cwd_ok ? p.cwd : dash);
  add(ePropSection_Paths, "Root", p.root_ok ? p.root : dash);
  add(ePropSection_Paths, "Command Line", p.cmdline.len > 0 ? p.cmdline : dash);

  // Placement
  add(ePropSection_Placement, "cgroup", p.cgroup.len > 0 ? p.cgroup : dash);

  // Security
  if (p.caps_ok) {
    add(ePropSection_Security, "Caps (effective)", p.cap_eff);
    add(ePropSection_Security, "Caps (permitted)", p.cap_prm);
    add(ePropSection_Security, "Caps (inheritable)", p.cap_inh);
    add(ePropSection_Security, "Caps (ambient)", p.cap_amb);
    add(ePropSection_Security, "Caps (bounding)", p.cap_bnd);
  }
  if (p.no_new_privs >= 0) {
    add(ePropSection_Security, "NoNewPrivs",
        String::static_string(p.no_new_privs ? "yes" : "no"));
  }
  if (p.seccomp >= 0) {
    add(ePropSection_Security, "Seccomp",
        String::static_string(seccomp_label(p.seccomp)));
  }
  if (p.security_label.len > 0) {
    add(ePropSection_Security, "Security Label", p.security_label);
  }

  return n;
}

static bool send_properties_request(Sync &sync, const Pid pid) {
  return on_demand_send_request(sync,
                                sync.on_demand_reader.properties_request_queue,
                                PropertiesRequest{pid});
}

void properties_viewer_request(PropertiesViewerState &state, Sync &sync,
                               const Pid pid, const char *comm,
                               const ImGuiID dock_id,
                               const ProcessWindowFlags extra_flags) {
  if (on_demand_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  PropertiesViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  on_demand_window_init(win->od, pid, comm, dock_id, extra_flags);
  win->props = {};
  win->menu_selection = {};

  if (!send_properties_request(sync, pid)) {
    on_demand_mark_request_dropped(win->od);
  }

  on_demand_sort_added(state.windows);
}

static void copy_props_into(BumpArena &arena, ProcessProperties &p) {
  p.comm = String::copy_from(arena, p.comm);
  p.parent_name = String::copy_from(arena, p.parent_name);
  p.exe = String::copy_from(arena, p.exe);
  p.cwd = String::copy_from(arena, p.cwd);
  p.root = String::copy_from(arena, p.root);
  p.cmdline = String::copy_from(arena, p.cmdline);
  p.username = String::copy_from(arena, p.username);
  p.groupname = String::copy_from(arena, p.groupname);
  p.tty = String::copy_from(arena, p.tty);
  p.groups = String::copy_from(arena, p.groups);
  p.cgroup = String::copy_from(arena, p.cgroup);
  p.cap_inh = String::copy_from(arena, p.cap_inh);
  p.cap_prm = String::copy_from(arena, p.cap_prm);
  p.cap_eff = String::copy_from(arena, p.cap_eff);
  p.cap_bnd = String::copy_from(arena, p.cap_bnd);
  p.cap_amb = String::copy_from(arena, p.cap_amb);
  p.security_label = String::copy_from(arena, p.security_label);
}

void properties_viewer_update(PropertiesViewerState &state, Sync &sync) {
  PropertiesResponse response;
  while (sync.on_demand_reader.properties_response_queue.pop(response)) {
    PropertiesViewerWindow *win = on_demand_find(state.windows, response.pid);
    const bool had_props = win && win->od.status == eOnDemandViewerStatus_Ready;
    if (win && on_demand_apply_response(win->od, response.error_code)) {
      if (had_props) ++state.updates_since_last_cleanup;
      win->props = response.props;
      copy_props_into(state.cur_arena, win->props);
    }
    response.owner_arena.destroy();
  }

  // Compact arena if wasted too much
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (PropertiesViewerWindow &win : state.windows) {
      if (win.od.status == eOnDemandViewerStatus_Ready) {
        copy_props_into(new_arena, win.props);
      }
    }

    state.cur_arena = new_arena;
    state.updates_since_last_cleanup = 0;
    old_arena.destroy();
  }
}

static constexpr ImGuiTableFlags PROP_TABLE_FLAGS =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
    ImGuiTableFlags_Borders | ImGuiTableFlags_HighlightHoveredColumn;

static String selection_text(BumpArena &arena, const String &value,
                             const UiTextSelection &selection) {
  const uint32_t end = std::min(selection.end, value.len);
  const uint32_t begin = std::min(selection.begin, end);
  if (begin == end) return value;
  return String::copy_from(arena, value.data + begin, end - begin);
}

static void draw_properties_content(FrameContext &ctx, ViewState &view_state,
                                    PropertiesViewerWindow &win,
                                    const PropEntry *entries,
                                    const uint32_t count) {
  // Shared Property column width keeps the per-section tables aligned
  ui_push_mono_font();
  float label_width = 0.0f;
  for (uint32_t i = 0; i < count; ++i) {
    label_width =
        std::max(label_width, ImGui::CalcTextSize(entries[i].label).x);
  }
  ui_pop_mono_font();

  for (int section = 0; section < IM_ARRAYSIZE(PROP_SECTIONS); ++section) {
    bool has_rows = false;
    for (uint32_t i = 0; i < count && !has_rows; ++i) {
      has_rows = entries[i].section == section;
    }
    if (!has_rows) continue;

    ImGui::SeparatorText(PROP_SECTIONS[section]);

    const String table_id =
        String::sprintf(ctx.frame_arena, "##Sec%d", section);
    if (!ImGui::BeginTable(table_id.data, 2, PROP_TABLE_FLAGS)) {
      continue;
    }
    ui_push_mono_font();
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed,
                            label_width);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    for (uint32_t i = 0; i < count; ++i) {
      const PropEntry &entry = entries[i];
      if (entry.section != section) continue;
      ImGui::PushID(static_cast<int>(i));

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);

      ImGui::TextUnformatted(entry.label);
      bool open_menu = ui_item_right_clicked();

      ImGui::TableSetColumnIndex(1);
      const UiTextSelection selection =
          ui_selectable_text("##value", entry.value);
      open_menu |= ui_item_right_clicked();

      if (selection.begin != selection.end && ui_copy_shortcut_pressed()) {
        clipboard_copy_cell(
            view_state.notifications,
            selection_text(ctx.frame_arena, entry.value, selection));
      }

      if (open_menu) {
        win.menu_selection = selection;
        ImGui::OpenPopup("##menu");
      }
      ui_hold_text_selection("##menu");
      if (ImGui::BeginPopup("##menu")) {
        const String cell =
            selection_text(ctx.frame_arena, entry.value, win.menu_selection);
        if (ImGui::MenuItemEx(copy_cell_menu_label(ctx.frame_arena, cell).data,
                              ICON_MD_CONTENT_COPY)) {
          clipboard_copy_cell(view_state.notifications, cell);
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ui_pop_mono_font();
    ImGui::EndTable();
  }
}

// Like on_demand_viewer_title but without a result count: the property set is
// the same for every process, so a number would be noise.
static String properties_window_title(BumpArena &arena,
                                      const OnDemandViewerStatus status,
                                      const char *process_name, const Pid pid) {
  const char *suffix = status == eOnDemandViewerStatus_Error ? " (Error)"
                       : status == eOnDemandViewerStatus_Loading
                           ? " (Loading...)"
                           : "";
  return String::sprintf(arena, "Properties%s - %s (%d)###Properties%d", suffix,
                         process_name, pid, pid);
}

void properties_viewer_draw(FrameContext &ctx, ViewState &view_state,
                            const State &state) {
  ZoneScoped;
  PropertiesViewerState &my_state = view_state.properties_viewer_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    PropertiesViewerWindow &win = my_state.windows.data()[last];

    PropEntry entries[48];
    uint32_t count = 0;
    if (win.od.status == eOnDemandViewerStatus_Ready) {
      count = build_property_rows(ctx.frame_arena, win.props, state, entries,
                                  IM_ARRAYSIZE(entries));
    }

    const String title = properties_window_title(
        ctx.frame_arena, win.od.status, win.od.process_name, win.od.pid);

    bool keep_open = true;
    if (on_demand_window_begin(view_state, win.od, title.data, keep_open)) {
      if (win.od.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.od.error_code);
      } else if (win.od.status == eOnDemandViewerStatus_Ready) {
        draw_properties_content(ctx, view_state, win, entries, count);
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
