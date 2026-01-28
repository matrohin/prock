#include "environ_viewer.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

const char *ENVIRON_COPY_HEADER = "Name\tValue\n";

static void copy_environ_row(const EnvironEntry &entry) {
  char buf[4400];
  snprintf(buf, sizeof(buf), "%s%s\t%s", ENVIRON_COPY_HEADER, entry.name,
           entry.value);
  ImGui::SetClipboardText(buf);
}

static void copy_path_segment(const char *start, const char *end) {
  size_t len = end - start;
  char buf[4096];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, start, len);
  buf[len] = '\0';
  ImGui::SetClipboardText(buf);
}

static void copy_all_environ(BumpArena &arena, const EnvironViewerWindow &win) {
  const size_t buf_size = 128 + win.entries.size * 4400;
  char *buf = arena.alloc_string(buf_size);
  char *ptr = buf;
  ptr += snprintf(ptr, buf_size, "%s", ENVIRON_COPY_HEADER);

  for (size_t i = 0; i < win.entries.size; ++i) {
    const EnvironEntry &entry = win.entries.data[i];
    ptr += snprintf(ptr, buf_size - (ptr - buf), "%s\t%s\n", entry.name,
                    entry.value);
  }
  ImGui::SetClipboardText(buf);
}

// Returns true if value looks like a PATH-style variable (multiple colon-separated paths)
static bool is_expandable_value(const char *value, size_t len) {
  if (len < 10) return false;  // Too short to benefit from expansion
  int colons = 0;
  for (size_t i = 0; i < len; ++i) {
    if (value[i] == ':') {
      ++colons;
      if (colons >= 2) return true;  // At least 3 segments
    }
  }
  return false;
}

static void sort_environ(EnvironViewerWindow &win) {
  if (win.entries.size == 0) return;

  const auto compare = [&](const EnvironEntry &a, const EnvironEntry &b) {
    switch (win.sorted_by) {
    case eEnvironViewerColumnId_Name:
      return strcmp(a.name, b.name) < 0;
    case eEnvironViewerColumnId_Value:
      return strcmp(a.value, b.value) < 0;
    default:
      return false;
    }
  };

  if (win.sorted_order == ImGuiSortDirection_Ascending) {
    std::stable_sort(win.entries.data, win.entries.data + win.entries.size,
                     compare);
  } else {
    std::stable_sort(win.entries.data, win.entries.data + win.entries.size,
                     [&](const EnvironEntry &a, const EnvironEntry &b) {
                       return compare(b, a);
                     });
  }
}

static void send_environ_request(Sync &sync, const int pid) {
  const EnvironRequest req = {pid};
  sync.on_demand_reader.environ_request_queue.push(req);
  sync.on_demand_reader.library_cv.notify_one();
}

void environ_viewer_request(EnvironViewerState &state, Sync &sync,
                            const int pid, const char *comm,
                            const ImGuiID dock_id) {
  // Create new window
  EnvironViewerWindow *win =
      state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->status = eEnvironViewerStatus_Loading;
  win->pid = pid;
  win->dock_id = dock_id;
  win->flags |= eProcessWindowFlags_RedockRequested;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->selected_index = -1;
  win->selected_child_index = -1;

  send_environ_request(sync, pid);

  common_views_sort_added(state.windows);
}

void environ_viewer_update(EnvironViewerState &state, Sync &sync) {
  // Process responses
  EnvironResponse response;
  while (sync.on_demand_reader.environ_response_queue.pop(response)) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
      EnvironViewerWindow &win = state.windows.data()[i];
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eEnvironViewerStatus_Ready;
          // Copy entries to our arena, including string data
          win.entries =
              Array<EnvironEntry>::create(state.cur_arena, response.entries.size);
          for (size_t j = 0; j < response.entries.size; ++j) {
            const EnvironEntry &src = response.entries.data[j];
            EnvironEntry &dst = win.entries.data[j];
            dst.name = state.cur_arena.alloc_string_copy(src.name, src.name_len);
            dst.name_len = src.name_len;
            dst.value = state.cur_arena.alloc_string_copy(src.value, src.value_len);
            dst.value_len = src.value_len;
          }
        } else {
          win.status = eEnvironViewerStatus_Error;
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
      EnvironViewerWindow &win = state.windows.data()[i];
      if (win.entries.size > 0) {
        Array<EnvironEntry> new_entries =
            Array<EnvironEntry>::create(new_arena, win.entries.size);
        for (size_t j = 0; j < win.entries.size; ++j) {
          const EnvironEntry &src = win.entries.data[j];
          EnvironEntry &dst = new_entries.data[j];
          dst.name = new_arena.alloc_string_copy(src.name, src.name_len);
          dst.name_len = src.name_len;
          dst.value = new_arena.alloc_string_copy(src.value, src.value_len);
          dst.value_len = src.value_len;
        }
        win.entries = new_entries;
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}

void environ_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  EnvironViewerState &my_state = view_state.environ_viewer_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    EnvironViewerWindow &win = my_state.windows.data()[last];
    char title[128];
    if (win.status == eEnvironViewerStatus_Error) {
      snprintf(title, sizeof(title), "Environment: %s (%d) - Error###Environ%d",
               win.process_name, win.pid, win.pid);
    } else if (win.status == eEnvironViewerStatus_Loading) {
      snprintf(title, sizeof(title),
               "Environment: %s (%d) - Loading...###Environ%d",
               win.process_name, win.pid, win.pid);
    } else {
      snprintf(title, sizeof(title),
               "Environment: %s (%d) - %zu variables###Environ%d",
               win.process_name, win.pid, win.entries.size, win.pid);
    }

    process_window_handle_docking_and_pos(view_state, win.dock_id,
                                          win.flags, title);

    bool should_be_opened = true;
    if (ImGui::Begin(title, &should_be_opened, COMMON_VIEW_FLAGS)) {
      process_window_check_close(win.flags, should_be_opened);

      // Content area - show previous data while loading, or error message
      if (win.status == eEnvironViewerStatus_Error) {
        draw_error_with_pkexec(win.error_message, win.error_code);
      } else if (win.entries.size > 0) {
        ImGuiTextFilter filter = draw_filter_input(
            "##EnvFilter", win.filter_text, sizeof(win.filter_text));
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
          win.status = eEnvironViewerStatus_Loading;
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
                                  [&]() { sort_environ(win); });

          for (size_t j = 0; j < win.entries.size; ++j) {
            const EnvironEntry &entry = win.entries.data[j];
            // Filter by name or value
            if (filter.IsActive() && !filter.PassFilter(entry.name) &&
                !filter.PassFilter(entry.value)) {
              continue;
            }
            const bool is_selected = (win.selected_index == static_cast<int>(j));
            const bool expandable =
                is_expandable_value(entry.value, entry.value_len);

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

              bool is_open = ImGui::TreeNodeEx(entry.name, flags);

              // Handle selection on click
              if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
              }

              // Context menu
              if (ImGui::BeginPopupContextItem()) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
                if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                  copy_environ_row(entry);
                }
                if (ImGui::MenuItem("Copy All")) {
                  copy_all_environ(ctx.frame_arena, win);
                }
                ImGui::EndPopup();
              }

              // Value column - show collapsed hint or nothing when expanded
              ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
              if (!is_open) {
                ImGui::TextUnformatted(entry.value);
                if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("%s", entry.value);
                }
              }

              // Render children when expanded
              if (is_open) {
                const char *seg_start = entry.value;
                const char *p = entry.value;
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

                    char seg_label[32];
                    snprintf(seg_label, sizeof(seg_label), "[%d]", seg_idx);
                    ImGui::TreeNodeEx(seg_label, leaf_flags);

                    if (ImGui::IsItemClicked()) {
                      win.selected_index = static_cast<int>(j);
                      win.selected_child_index = seg_idx;
                    }

                    // Context menu for child segment
                    if (ImGui::BeginPopupContextItem()) {
                      win.selected_index = static_cast<int>(j);
                      win.selected_child_index = seg_idx;
                      if (ImGui::MenuItem("Copy Path", "Ctrl+C")) {
                        copy_path_segment(seg_start, seg_end);
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
              if (ImGui::Selectable(entry.name, is_selected,
                                    ImGuiSelectableFlags_SpanAllColumns)) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
              }

              if (ImGui::BeginPopupContextItem()) {
                win.selected_index = static_cast<int>(j);
                win.selected_child_index = -1;
                if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                  copy_environ_row(entry);
                }
                if (ImGui::MenuItem("Copy All")) {
                  copy_all_environ(ctx.frame_arena, win);
                }
                ImGui::EndPopup();
              }

              // Value
              ImGui::TableSetColumnIndex(eEnvironViewerColumnId_Value);
              ImGui::TextUnformatted(entry.value);
              if (ImGui::IsItemHovered() && entry.value_len > 50) {
                ImGui::SetTooltip("%s", entry.value);
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
            const char *seg_start = entry.value;
            const char *p = entry.value;
            int seg_idx = 0;
            while (*p || seg_start != p) {
              if (*p == ':' || *p == '\0') {
                if (seg_idx == win.selected_child_index) {
                  copy_path_segment(seg_start, p);
                  break;
                }
                if (*p == '\0') break;
                seg_start = p + 1;
                ++seg_idx;
              }
              ++p;
            }
          } else {
            copy_environ_row(entry);
          }
        }
      }
    }
    ImGui::End();

    if (should_be_opened) {
      ++last;
    } else {
      my_state.wasted_bytes += win.entries.size * sizeof(EnvironEntry);
    }
  }
  my_state.windows.shrink_to(last);
}
