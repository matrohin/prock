#include "environ_viewer.h"

#include "views/common.h"
#include "views/icons.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "labels.h"
#include "on_demand_common.h"
#include "tracy/Tracy.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

const char *ENVIRON_COPY_HEADER = "Name\tValue\n";

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_ENVIRON = 5;

static void copy_environ_row(BumpArena &arena, const EnvironEntry &entry) {
  const String str = String::sprintf(arena, "%s%s\t%s", ENVIRON_COPY_HEADER,
                                     entry.name.data, entry.value.data);
  ImGui::SetClipboardText(str.data);
}

static void copy_path_segment(BumpArena &arena, const char *start,
                              const char *end) {
  const String str = String::copy_from(arena, start, end - start);
  ImGui::SetClipboardText(str.data);
}

static void copy_all_environ(BumpArena &arena, const EnvironViewerWindow &win) {
  copy_all_to_clipboard(arena, win.entries.data, win.entries.size, 4400,
                        ENVIRON_COPY_HEADER,
                        [](char *ptr, size_t rem, const EnvironEntry &entry) {
                          return snprintf(ptr, rem, "%s\t%s\n", entry.name.data,
                                          entry.value.data);
                        });
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
  sort_bidirectional(win.entries.data, win.entries.size, win.sorted_order,
                     [&](const EnvironEntry &a, const EnvironEntry &b) {
                       switch (win.sorted_by) {
                       case eEnvironViewerColumnId_Name:
                         return strcmp(a.name.data, b.name.data) < 0;
                       case eEnvironViewerColumnId_Value:
                         return strcmp(a.value.data, b.value.data) < 0;
                       default:
                         return false;
                       }
                     });
}

static void send_environ_request(Sync &sync, const Pid pid) {
  const EnvironRequest req = {pid};
  sync.on_demand_reader.environ_request_queue.push(req);
  sync.on_demand_reader.request_read_cv.notify_one();
}

void environ_viewer_request(EnvironViewerState &state, Sync &sync,
                            const Pid pid, const char *comm,
                            const ImGuiID dock_id,
                            const ProcessWindowFlags extra_flags) {
  if (process_window_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  EnvironViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  win->status = eOnDemandViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested | extra_flags;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_index = -1;
  win->selected_child_index = -1;

  send_environ_request(sync, pid);

  common_views_sort_added(state.windows);
}

void environ_viewer_update(EnvironViewerState &state, Sync &sync) {
  EnvironResponse response;
  while (sync.on_demand_reader.environ_response_queue.pop(response)) {
    for (EnvironViewerWindow &win : state.windows) {
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eOnDemandViewerStatus_Ready;
          win.entries =
              Array<EnvironEntry>::copy_from(state.cur_arena, response.entries);
          for (uint32_t j = 0; j < win.entries.size; ++j) {
            EnvironEntry &dst = win.entries.data[j];
            dst.name = String::copy_from(state.cur_arena, dst.name);
            dst.value = String::copy_from(state.cur_arena, dst.value);
          }
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
  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_ENVIRON) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (EnvironViewerWindow &win : state.windows) {
      if (win.entries.size > 0) {
        Array<EnvironEntry> new_entries =
            Array<EnvironEntry>::copy_from(state.cur_arena, win.entries);
        for (uint32_t j = 0; j < win.entries.size; ++j) {
          EnvironEntry &dst = new_entries.data[j];
          dst.name = String::copy_from(new_arena, dst.name);
          dst.value = String::copy_from(new_arena, dst.value);
        }
        win.entries = new_entries;
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
    const String title = on_demand_viewer_title(
        ctx.frame_arena, win.status, "Environment", "env variables",
        win.entries.size, win.process_name, win.pid);

    process_window_handle_docking_and_pos(view_state, win.dock_id, win.flags,
                                          title.data);

    bool should_be_opened = true;
    const ImGuiWindowFlags win_flags = process_window_flags(win.flags);
    if (ImGui::Begin(title.data, &should_be_opened, win_flags)) {
      process_window_check_close(win.flags, should_be_opened);

      // Content area - show previous data while loading, or error message
      if (win.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.error_code);
      } else if (win.entries.size > 0) {
        ImGuiTextFilter filter = draw_filter_input(
            "##EnvFilter", win.filter_text, sizeof(win.filter_text));
        ImGui::SameLine();
        if (draw_refresh_button()) {
          win.status = eOnDemandViewerStatus_Loading;
          send_environ_request(*view_state.sync, win.pid);
        }
        if (ImGui::BeginTable("Environment", eEnvironViewerColumnId_Count,
                              COMMON_TABLE_FLAGS)) {
          ImGui::TableSetupScrollFreeze(0, 1);
          ImGui::TableSetupColumn("Name",
                                  ImGuiTableColumnFlags_DefaultSort |
                                      ImGuiTableColumnFlags_NoHide,
                                  0.0f, eEnvironViewerColumnId_Name);
          ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_None, 0.0f,
                                  eEnvironViewerColumnId_Value);
          ImGui::TableHeadersRow();

          handle_table_sort_specs(win.sorted_by, win.sorted_order,
                                  [&] { sort_environ(win); });

          for (uint32_t j = 0; j < win.entries.size; ++j) {
            const EnvironEntry &entry = win.entries.data[j];
            // Filter by name or value
            if (filter.IsActive() && !filter.PassFilter(entry.name.data) &&
                !filter.PassFilter(entry.value.data)) {
              continue;
            }
            const bool is_selected = win.selected_index == static_cast<int>(j);
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
              if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
              }

              // Context menu
              if (ImGui::BeginPopupContextItem()) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
                if (ImGui::MenuItemEx("Copy", ICON_MD_CONTENT_COPY, "Ctrl+C")) {
                  copy_environ_row(ctx.frame_arena, entry);
                }
                if (ImGui::MenuItem("Copy All")) {
                  copy_all_environ(ctx.frame_arena, win);
                }
                ImGui::EndPopup();
              }

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

                    if (ImGui::IsItemClicked()) {
                      win.selected_index = static_cast<int>(j);
                      win.selected_child_index = seg_idx;
                    }

                    // Context menu for child segment
                    if (ImGui::BeginPopupContextItem()) {
                      win.selected_index = static_cast<int>(j);
                      win.selected_child_index = seg_idx;
                      if (ImGui::MenuItemEx("Copy Path", ICON_MD_CONTENT_COPY,
                                            "Ctrl+C")) {
                        copy_path_segment(ctx.frame_arena, seg_start, seg_end);
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
                                    ImGuiSelectableFlags_SpanAllColumns)) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
              }

              if (ImGui::BeginPopupContextItem()) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
                if (ImGui::MenuItemEx("Copy", ICON_MD_CONTENT_COPY, "Ctrl+C")) {
                  copy_environ_row(ctx.frame_arena, entry);
                }
                if (ImGui::MenuItem("Copy All")) {
                  copy_all_environ(ctx.frame_arena, win);
                }
                ImGui::EndPopup();
              }

              // Value
              ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
              ImGui::TextUnformatted(entry.value.data);
              if (ImGui::IsItemHovered() && entry.value.len > 50) {
                ImGui::SetTooltip("%s", entry.value.data);
              }
            }

            ImGui::PopID();
          }

          ImGui::EndTable();
        }

        // Ctrl+C to copy selected row or child segment
        if (win.selected_index >= 0 &&
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
          const EnvironEntry &entry = win.entries.data[win.selected_index];
          if (win.selected_child_index >= 0) {
            // Copy specific path segment
            const char *seg_start = entry.value.data;
            const char *p = entry.value.data;
            int seg_idx = 0;
            while (*p || seg_start != p) {
              if (*p == ':' || *p == '\0') {
                if (seg_idx == win.selected_child_index) {
                  copy_path_segment(ctx.frame_arena, seg_start, p);
                  break;
                }
                if (*p == '\0') break;
                seg_start = p + 1;
                ++seg_idx;
              }
              ++p;
            }
          } else {
            copy_environ_row(ctx.frame_arena, entry);
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
