#include "environ_viewer.h"

#include "views/common.h"
#include "views/icons.h"
#include "views/shortcut.h"
#include "views/ui.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "labels.h"
#include "on_demand_common.h"
#include "tracy/Tracy.hpp"

#include <cstring>

const char *ENVIRON_COPY_HEADER = "Name\tValue\n";

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_ENVIRON = 5;

static void copy_environ_row(Notifications &notifications, BumpArena &arena,
                             const EnvironEntry &entry) {
  const String str = String::sprintf(arena, "%s%s\t%s", ENVIRON_COPY_HEADER,
                                     entry.name.data, entry.value.data);
  clipboard_copy_row(notifications, str.data);
}

static void copy_path_segment(Notifications &notifications, BumpArena &arena,
                              const char *start, const char *end) {
  clipboard_copy_cell(notifications,
                      String::copy_from(arena, start, end - start));
}

static void copy_all_environ(Notifications &notifications, BumpArena &arena,
                             const EnvironViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.entries.data, win.entries.size, 4400,
      ENVIRON_COPY_HEADER,
      [](char *ptr, const size_t rem, const EnvironEntry &entry) {
        return snprintf(ptr, rem, "%s\t%s\n", entry.name.data,
                        entry.value.data);
      });
}

// Context menu shared by expandable and plain rows; the last submitted item
// must be the row's spanning Selectable/TreeNode.
static void environ_context_menu(FrameContext &ctx,
                                 Notifications &notifications,
                                 EnvironViewerWindow &win, const int index,
                                 const EnvironEntry &entry) {
  if (ui_context_menu(
          win.od.selected_index == index && win.selected_child_index < 0,
          win.od.context_menu_column, eEnvironViewerColumnId_Count)) {
    win.od.selected_index = index;
    win.selected_child_index = -1;
    const String cell =
        win.od.context_menu_column == eEnvironViewerColumnId_Value ? entry.value
                                                                   : entry.name;
    if (ImGui::MenuItemEx(copy_cell_menu_label(ctx.frame_arena, cell).data,
                          ICON_MD_CONTENT_COPY)) {
      clipboard_copy_cell(notifications, cell);
    }
    if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
      copy_environ_row(notifications, ctx.frame_arena, entry);
    }
    if (ImGui::MenuItem("Copy All")) {
      copy_all_environ(notifications, ctx.frame_arena, win);
    }
    ImGui::EndPopup();
  }
}

// Returns true if value looks like a PATH-style variable (multiple
// colon-separated paths)
static bool is_expandable_value(const String &value) {
  if (value.len < 10) return false; // Too short to benefit from expansion
  int colons = 0;
  for (uint32_t i = 0; i < value.len; ++i) {
    if (value.data[i] == ':') {
      ++colons;
      if (colons >= 2) return true; // At least 3 segments
    }
  }
  return false;
}

static void sort_environ(EnvironViewerWindow &win) {
  sort_bidirectional(win.entries.data, win.entries.size, win.od.sorted_order,
                     [&](const EnvironEntry &a, const EnvironEntry &b) {
                       switch (win.od.sorted_by) {
                       case eEnvironViewerColumnId_Name:
                         return strcmp(a.name.data, b.name.data) < 0;
                       case eEnvironViewerColumnId_Value:
                         return strcmp(a.value.data, b.value.data) < 0;
                       default:
                         return false;
                       }
                     });
}

static Array<EnvironEntry> copy_entries(BumpArena &arena,
                                        const Array<EnvironEntry> &src) {
  Array<EnvironEntry> dst = Array<EnvironEntry>::copy_from(arena, src);
  for (EnvironEntry &entry : dst) {
    entry.name = String::copy_from(arena, entry.name);
    entry.value = String::copy_from(arena, entry.value);
  }
  return dst;
}

static bool send_environ_request(Sync &sync, const Pid pid) {
  return on_demand_send_request(
      sync, sync.on_demand_reader.environ_request_queue, EnvironRequest{pid});
}

void environ_viewer_request(EnvironViewerState &state, Sync &sync,
                            const Pid pid, const char *comm,
                            const ImGuiID dock_id,
                            const ProcessWindowFlags extra_flags) {
  if (on_demand_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  EnvironViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  on_demand_window_init(win->od, pid, comm, dock_id, extra_flags);
  win->selected_child_index = -1;

  if (!send_environ_request(sync, pid)) {
    on_demand_mark_request_dropped(win->od);
  }

  on_demand_sort_added(state.windows);
}

void environ_viewer_update(EnvironViewerState &state, Sync &sync) {
  EnvironResponse response;
  while (sync.on_demand_reader.environ_response_queue.pop(response)) {
    EnvironViewerWindow *win = on_demand_find(state.windows, response.pid);
    if (win && on_demand_apply_response(win->od, response.error_code)) {
      win->entries = copy_entries(state.cur_arena, response.entries);
      sort_environ(*win);
    }
    response.owner_arena.destroy();
  }

  // Compact arena if wasted too much
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_ENVIRON) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (EnvironViewerWindow &win : state.windows) {
      if (win.entries.size > 0) {
        win.entries = copy_entries(new_arena, win.entries);
      }
    }

    state.cur_arena = new_arena;
    state.updates_since_last_cleanup = 0;
    old_arena.destroy();
  }
}

void environ_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  EnvironViewerState &my_state = view_state.environ_viewer_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    EnvironViewerWindow &win = my_state.windows.data()[last];

    bool keep_open = true;
    if (on_demand_window_begin(view_state, win.od, "Environment",
                               win.entries.size, ctx.frame_arena, keep_open)) {
      // Content area - show previous data while loading, or error message
      if (win.od.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.od.error_code);
      } else if (win.entries.size > 0) {
        ImGuiTextFilter filter;
        bool refresh = false;
        if (on_demand_toolbar_begin(win.od, filter, "##EnvFilter")) {
          refresh = on_demand_toolbar_end(win.od);
        }
        if (refresh) {
          on_demand_refresh_status(
              win.od, send_environ_request(*view_state.sync, win.od.pid));
        }

        if (ImGui::BeginTable("Environment", eEnvironViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
          ui_push_mono_font();
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Name",
                                  ImGuiTableColumnFlags_DefaultSort |
                                      ImGuiTableColumnFlags_NoHide,
                                  0.0f, eEnvironViewerColumnId_Name);
          ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_None, 0.0f,
                                  eEnvironViewerColumnId_Value);
          ImGui::TableHeadersRow();

          handle_table_sort_specs(win.od.sorted_by, win.od.sorted_order,
                                  [&] { sort_environ(win); });

          for (uint32_t j = 0; j < win.entries.size; ++j) {
            const EnvironEntry &entry = win.entries.data[j];
            // Filter by name or value
            if (filter.IsActive() && !filter.PassFilter(entry.name.data) &&
                !filter.PassFilter(entry.value.data)) {
              continue;
            }
            const bool is_selected =
                win.od.selected_index == static_cast<int>(j);
            const bool expandable = is_expandable_value(entry.value);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Name);

            ImGui::PushID(static_cast<int>(j));

            if (expandable) {
              // Use TreeNode for expandable PATH-like values
              const bool parent_selected =
                  is_selected && win.selected_child_index < 0;
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
                                         ImGuiTreeNodeFlags_AllowOverlap;
              if (parent_selected) flags |= ImGuiTreeNodeFlags_Selected;

              const bool is_open = ImGui::TreeNodeEx(entry.name.data, flags);

              // Handle selection on click
              if ((ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ||
                  ImGui::IsItemFocused()) {
                win.od.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
              }

              environ_context_menu(ctx, view_state.notifications, win,
                                   static_cast<int>(j), entry);

              // Value column - show collapsed hint or nothing when expanded
              ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
              if (!is_open) {
                ImGui::TextUnformatted(entry.value.data);
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("%s", entry.value.data);
                }
              }

              // Render children when expanded
              if (is_open) {
                const char *seg_start = entry.value.data;
                const char *p = entry.value.data;
                int seg_idx = 0;
                while (*p || seg_start != p) {
                  if (*p == ':' || *p == '\0') {
                    const bool child_selected =
                        is_selected && win.selected_child_index == seg_idx;
                    const char *seg_end = p;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Name);

                    // Leaf node with selection support
                    ImGui::PushID(seg_idx);
                    ImGuiTreeNodeFlags leaf_flags =
                        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet |
                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                        ImGuiTreeNodeFlags_SpanAllColumns;
                    if (child_selected)
                      leaf_flags |= ImGuiTreeNodeFlags_Selected;

                    const String seg_label =
                        String::sprintf(ctx.frame_arena, "[%d]", seg_idx);
                    ImGui::TreeNodeEx(seg_label.data, leaf_flags);

                    if (ImGui::IsItemClicked() || ImGui::IsItemFocused()) {
                      win.od.selected_index = static_cast<int>(j);
                      win.selected_child_index = seg_idx;
                    }

                    // Context menu for child segment
                    if (ui_context_menu(child_selected)) {
                      win.od.selected_index = static_cast<int>(j);
                      win.selected_child_index = seg_idx;
                      const String path = String::copy_from(
                          ctx.frame_arena, seg_start, seg_end - seg_start);
                      if (ImGui::MenuItemEx(
                              copy_cell_menu_label(ctx.frame_arena, path).data,
                              ICON_MD_CONTENT_COPY, "Ctrl+C")) {
                        clipboard_copy_cell(view_state.notifications, path);
                      }
                      ImGui::EndPopup();
                    }

                    ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
                    if (seg_end > seg_start) {
                      ImGui::TextUnformatted(seg_start, seg_end);
                    } else {
                      ImGui::TextDisabled("(empty)");
                    }
                    ImGui::PopID();

                    if (*p == '\0') break;
                    seg_start = p + 1;
                    ++seg_idx;
                  }
                  ++p;
                }
                ImGui::TreePop();
              }
            } else {
              // Non-expandable: use regular selectable
              if (ImGui::Selectable(entry.name.data, is_selected,
                                    ImGuiSelectableFlags_SpanAllColumns) ||
                  ImGui::IsItemFocused()) {
                win.od.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
              }

              environ_context_menu(ctx, view_state.notifications, win,
                                   static_cast<int>(j), entry);

              // Value
              ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
              ImGui::TextUnformatted(entry.value.data);
              if (ImGui::IsItemHovered() && entry.value.len > 50) {
                ImGui::SetTooltip("%s", entry.value.data);
              }
            }

            ImGui::PopID();
          }

          ui_pop_mono_font();
          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row or child segment
        if (shortcut_copy_row(win.od.selected_index, win.entries.size)) {
          const EnvironEntry &entry = win.entries.data[win.od.selected_index];
          if (win.selected_child_index >= 0) {
            // Copy specific path segment
            const char *seg_start = entry.value.data;
            const char *p = entry.value.data;
            int seg_idx = 0;
            while (*p || seg_start != p) {
              if (*p == ':' || *p == '\0') {
                if (seg_idx == win.selected_child_index) {
                  copy_path_segment(view_state.notifications, ctx.frame_arena,
                                    seg_start, p);
                  break;
                }
                if (*p == '\0') break;
                seg_start = p + 1;
                ++seg_idx;
              }
              ++p;
            }
          } else {
            copy_environ_row(view_state.notifications, ctx.frame_arena, entry);
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
