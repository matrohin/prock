#include "smaps_viewer.h"

#include "views/common.h"
#include "views/icons.h"
#include "views/shortcut.h"
#include "views/table_item.h"
#include "views/ui.h"
#include "views/view_state.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <cstring>

static constexpr uint32_t CLEANUP_AFTER_N_UPDATES_SMAPS = 5;

static const char *segment_label(const SmapsSegment &seg) {
  return seg.path.data && seg.path.len > 0 ? seg.path.data : "[anon]";
}

static void format_kb(char *buf, const int size, const ulong kb) {
  format_memory_bytes(kb * 1024.0, buf, size);
}

// Aggregated row for grouped mode (by mapping name).
struct SmapsGroup {
  const char *name;
  uint32_t count;
  ulong size_kb, rss_kb, pss_kb, private_kb, swap_kb;
};

const char *SMAPS_COPY_HEADER = "Address\tPerms\tSize(kB)\tRSS(kB)\tPSS(kB)"
                                "\tPrivate(kB)\tSwap(kB)\tMapping\n";

const char *SMAPS_GROUP_COPY_HEADER =
    "Segs\tSize(kB)\tRSS(kB)\tPSS(kB)\tPrivate(kB)\tSwap(kB)\tMapping\n";

static String smaps_cell_text(BumpArena &arena, const SmapsSegment &seg,
                              const int column) {
  switch (column) {
  case eSmapsViewerColumnId_Address:
    return String::sprintf(arena, "%lx-%lx", seg.start_addr, seg.end_addr);
  case eSmapsViewerColumnId_Perms:
    return String::static_string(seg.perms);
  case eSmapsViewerColumnId_Size:
    return String::sprintf(arena, "%lu", seg.size_kb);
  case eSmapsViewerColumnId_Rss:
    return String::sprintf(arena, "%lu", seg.rss_kb);
  case eSmapsViewerColumnId_Pss:
    return String::sprintf(arena, "%lu", seg.pss_kb);
  case eSmapsViewerColumnId_Private:
    return String::sprintf(arena, "%lu",
                           seg.private_clean_kb + seg.private_dirty_kb);
  case eSmapsViewerColumnId_Swap:
    return String::sprintf(arena, "%lu", seg.swap_kb);
  case eSmapsViewerColumnId_Mapping:
    return String::static_string(segment_label(seg));
  default:
    return String::static_string("");
  }
}

// Grouped table column indices (0..6: Segs, Size, RSS, PSS, Private, Swap,
// Mapping), not SmapsViewerColumnId values.
static String smaps_group_cell_text(BumpArena &arena, const SmapsGroup &g,
                                    const int column) {
  switch (column) {
  case 0:
    return String::sprintf(arena, "%u", g.count);
  case 1:
    return String::sprintf(arena, "%lu", g.size_kb);
  case 2:
    return String::sprintf(arena, "%lu", g.rss_kb);
  case 3:
    return String::sprintf(arena, "%lu", g.pss_kb);
  case 4:
    return String::sprintf(arena, "%lu", g.private_kb);
  case 5:
    return String::sprintf(arena, "%lu", g.swap_kb);
  case 6:
    return String::static_string(g.name);
  default:
    return String::static_string("");
  }
}

static void copy_smaps_row(Notifications &notifications, BumpArena &frame_arena,
                           const SmapsSegment &seg) {
  const String buf = String::sprintf(
      frame_arena, "%s%lx-%lx\t%s\t%lu\t%lu\t%lu\t%lu\t%lu\t%s",
      SMAPS_COPY_HEADER, seg.start_addr, seg.end_addr, seg.perms, seg.size_kb,
      seg.rss_kb, seg.pss_kb, seg.private_clean_kb + seg.private_dirty_kb,
      seg.swap_kb, segment_label(seg));
  clipboard_copy_row(notifications, buf.data);
}

static void copy_all_smaps(Notifications &notifications, BumpArena &arena,
                           const SmapsViewerWindow &win) {
  copy_all_to_clipboard(
      notifications, arena, win.segments.data, win.segments.size, 160,
      SMAPS_COPY_HEADER,
      [](char *ptr, const size_t rem, const SmapsSegment &seg) {
        return snprintf(ptr, rem, "%lx-%lx\t%s\t%lu\t%lu\t%lu\t%lu\t%lu\t%s\n",
                        seg.start_addr, seg.end_addr, seg.perms, seg.size_kb,
                        seg.rss_kb, seg.pss_kb,
                        seg.private_clean_kb + seg.private_dirty_kb,
                        seg.swap_kb, segment_label(seg));
      });
}

static void copy_smaps_group(Notifications &notifications,
                             BumpArena &frame_arena, const SmapsGroup &g) {
  const String buf = String::sprintf(
      frame_arena, "%s%u\t%lu\t%lu\t%lu\t%lu\t%lu\t%s", SMAPS_GROUP_COPY_HEADER,
      g.count, g.size_kb, g.rss_kb, g.pss_kb, g.private_kb, g.swap_kb, g.name);
  clipboard_copy_row(notifications, buf.data);
}

static void copy_all_smaps_groups(Notifications &notifications,
                                  BumpArena &arena, const SmapsGroup *groups,
                                  const uint32_t count) {
  copy_all_to_clipboard(
      notifications, arena, groups, count, 96, SMAPS_GROUP_COPY_HEADER,
      [](char *ptr, const size_t rem, const SmapsGroup &g) {
        return snprintf(ptr, rem, "%u\t%lu\t%lu\t%lu\t%lu\t%lu\t%s\n", g.count,
                        g.size_kb, g.rss_kb, g.pss_kb, g.private_kb, g.swap_kb,
                        g.name);
      });
}

static void sort_segments(SmapsViewerWindow &win) {
  sort_bidirectional(win.segments.data, win.segments.size, win.od.sorted_order,
                     [&](const SmapsSegment &a, const SmapsSegment &b) {
                       switch (win.od.sorted_by) {
                       case eSmapsViewerColumnId_Address:
                         return a.start_addr < b.start_addr;
                       case eSmapsViewerColumnId_Perms:
                         return strncmp(a.perms, b.perms, sizeof(a.perms)) < 0;
                       case eSmapsViewerColumnId_Size:
                         return a.size_kb < b.size_kb;
                       case eSmapsViewerColumnId_Rss:
                         return a.rss_kb < b.rss_kb;
                       case eSmapsViewerColumnId_Pss:
                         return a.pss_kb < b.pss_kb;
                       case eSmapsViewerColumnId_Private:
                         return a.private_clean_kb + a.private_dirty_kb <
                                b.private_clean_kb + b.private_dirty_kb;
                       case eSmapsViewerColumnId_Swap:
                         return a.swap_kb < b.swap_kb;
                       case eSmapsViewerColumnId_Mapping:
                         return strcmp(segment_label(a), segment_label(b)) < 0;
                       default:
                         return false;
                       }
                     });
}

static Array<SmapsSegment> copy_segments(BumpArena &arena,
                                         const Array<SmapsSegment> &src) {
  Array<SmapsSegment> dst = Array<SmapsSegment>::copy_from(arena, src);
  for (SmapsSegment &seg : dst) {
    if (seg.path.data) {
      seg.path = String::copy_from(arena, seg.path);
    }
  }
  return dst;
}

static bool send_smaps_request(Sync &sync, const Pid pid) {
  return on_demand_send_request(sync, sync.on_demand_reader.smaps_request_queue,
                                SmapsRequest{pid});
}

void smaps_viewer_request(SmapsViewerState &state, Sync &sync, const Pid pid,
                          const char *comm, const ImGuiID dock_id,
                          const ProcessWindowFlags extra_flags) {
  if (on_demand_focus(state.windows, pid)) {
    return;
  }

  ++state.updates_since_last_cleanup;
  SmapsViewerWindow *win = state.windows.emplace_back(state.cur_arena);
  on_demand_window_init(win->od, pid, comm, dock_id, extra_flags);

  if (!send_smaps_request(sync, pid)) {
    on_demand_mark_request_dropped(win->od);
  }

  on_demand_sort_added(state.windows);
}

void smaps_viewer_update(SmapsViewerState &state, Sync &sync) {
  SmapsResponse response;
  while (sync.on_demand_reader.smaps_response_queue.pop(response)) {
    SmapsViewerWindow *win = on_demand_find(state.windows, response.pid);
    if (win) {
      if (win->segments.size > 0) {
        // Old segments will be abandoned in the arena — count as wasted.
        ++state.updates_since_last_cleanup;
      }
      win->refresh_pending = false;
      if (on_demand_apply_response(win->od, response.error_code)) {
        win->segments = copy_segments(state.cur_arena, response.segments);
        sort_segments(*win);
      }
    }
    response.owner_arena.destroy();
  }

  if (state.updates_since_last_cleanup > CLEANUP_AFTER_N_UPDATES_SMAPS) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (SmapsViewerWindow &win : state.windows) {
      win.segments = copy_segments(new_arena, win.segments);
    }

    state.cur_arena = new_arena;
    state.updates_since_last_cleanup = 0;
    old_arena.destroy();
  }
}

static String smaps_filter_str(FrameContext &ctx, const SmapsSegment &seg) {
  return String::sprintf(ctx.frame_arena, "%s %s", segment_label(seg),
                         seg.perms);
}

void smaps_viewer_draw(FrameContext &ctx, ViewState &view_state) {
  ZoneScoped;
  SmapsViewerState &my_state = view_state.smaps_viewer_state;
  uint32_t last = 0;

  for (uint32_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    SmapsViewerWindow &win = my_state.windows.data()[last];

    bool keep_open = true;
    if (on_demand_window_begin(view_state, win.od, "Memory Maps",
                               win.segments.size, ctx.frame_arena, keep_open)) {
      if (win.od.status == eOnDemandViewerStatus_Error) {
        draw_error_with_pkexec(win.od.error_code);
      } else if (win.od.status == eOnDemandViewerStatus_Ready) {
        ImGuiTextFilter filter;
        bool refresh = false;
        if (on_demand_toolbar_begin(win.od, filter, "##SmapsFilter", 1)) {
          ImGui::TableNextColumn();
          if (ImGui::Checkbox("Group", &win.grouped)) {
            win.od.selected_index = -1;
          }
          refresh = on_demand_toolbar_end(win.od, win.refresh_pending);
        }
        if (refresh) {
          win.refresh_pending =
              send_smaps_request(*view_state.sync, win.od.pid);
        }

        // Pre-pass: compute totals over filtered segments (used by both modes)
        ulong total_pss = 0, total_rss = 0, total_private = 0;
        ulong total_swap = 0, total_size = 0;
        uint32_t visible_count = 0;
        for (const SmapsSegment &seg : win.segments) {
          const String filter_str = smaps_filter_str(ctx, seg);
          if (!filter.PassFilter(filter_str.data)) continue;
          total_size += seg.size_kb;
          total_rss += seg.rss_kb;
          total_pss += seg.pss_kb;
          total_private += seg.private_clean_kb + seg.private_dirty_kb;
          total_swap += seg.swap_kb;
          ++visible_count;
        }

        // Summary bar
        if (visible_count > 0 || win.segments.size == 0) {
          const struct {
            const char *label;
            ulong value;
          } totals[] = {{"PSS:", total_pss},
                        {"RSS:", total_rss},
                        {"Private:", total_private},
                        {"Swap:", total_swap},
                        {"Size:", total_size}};
          const float scale = ui_scale();
          char val[32];
          for (uint32_t t = 0; t < IM_ARRAYSIZE(totals); ++t) {
            if (t > 0) ImGui::SameLine(0, 16 * scale);
            ImGui::TextDisabled("%s", totals[t].label);
            ImGui::SameLine(0, 4 * scale);
            format_kb(val, sizeof(val), totals[t].value);
            ImGui::Text("%s", val);
          }
        }

        if (win.segments.size == 0) {
          ImGui::TextDisabled("No mappings");
        } else if (win.grouped) {
          // ---- Grouped mode ----
          // Build groups in the frame arena (aggregated by mapping name)
          GrowingArray<SmapsGroup> groups = {};
          for (const SmapsSegment &seg : win.segments) {
            const String filter_str = smaps_filter_str(ctx, seg);
            if (!filter.PassFilter(filter_str.data)) continue;

            const char *name = segment_label(seg);
            SmapsGroup *found = nullptr;
            for (SmapsGroup &g : groups) {
              if (strcmp(g.name, name) == 0) {
                found = &g;
                break;
              }
            }
            if (!found) {
              found = groups.emplace_back(ctx.frame_arena);
              found->name = name;
              found->count = 0;
            }
            found->count++;
            found->size_kb += seg.size_kb;
            found->rss_kb += seg.rss_kb;
            found->pss_kb += seg.pss_kb;
            found->private_kb += seg.private_clean_kb + seg.private_dirty_kb;
            found->swap_kb += seg.swap_kb;
          }

          // Sort groups
          const auto group_lt = [&](const SmapsGroup &a, const SmapsGroup &b) {
            switch (win.od.sorted_by) {
            case eSmapsViewerColumnId_SegmentCount:
              return a.count < b.count;
            case eSmapsViewerColumnId_Size:
              return a.size_kb < b.size_kb;
            case eSmapsViewerColumnId_Rss:
              return a.rss_kb < b.rss_kb;
            case eSmapsViewerColumnId_Pss:
              return a.pss_kb < b.pss_kb;
            case eSmapsViewerColumnId_Private:
              return a.private_kb < b.private_kb;
            case eSmapsViewerColumnId_Swap:
              return a.swap_kb < b.swap_kb;
            case eSmapsViewerColumnId_Mapping:
              return strcmp(a.name, b.name) < 0;
            default:
              return a.pss_kb < b.pss_kb;
            }
          };
          if (win.od.sorted_order == ImGuiSortDirection_Ascending) {
            std::stable_sort(groups.begin(), groups.end(), group_lt);
          } else {
            std::stable_sort(groups.begin(), groups.end(),
                             [&](const SmapsGroup &a, const SmapsGroup &b) {
                               return group_lt(b, a);
                             });
          }

          constexpr int kGroupedCols = 7;
          if (ImGui::BeginTable("MemMapsGrouped", kGroupedCols,
                                COMMON_TABLE_FLAGS)) {
            ui_push_mono_font();
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Segs",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_SegmentCount);
            ImGui::TableSetupColumn("Size",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Size);
            ImGui::TableSetupColumn("RSS",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Rss);
            ImGui::TableSetupColumn("PSS",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Pss);
            ImGui::TableSetupColumn("Private",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Private);
            ImGui::TableSetupColumn("Swap",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Swap);
            ImGui::TableSetupColumn("Mapping", ImGuiTableColumnFlags_NoHide,
                                    0.0f, eSmapsViewerColumnId_Mapping);
            ImGui::TableHeadersRow();

            handle_table_sort_specs(win.od.sorted_by, win.od.sorted_order, [&] {
              if (win.od.sorted_order == ImGuiSortDirection_Ascending) {
                std::stable_sort(groups.begin(), groups.end(), group_lt);
              } else {
                std::stable_sort(groups.begin(), groups.end(),
                                 [&](const SmapsGroup &a, const SmapsGroup &b) {
                                   return group_lt(b, a);
                                 });
              }
            });

            for (uint32_t j = 0; j < groups.size(); ++j) {
              const SmapsGroup &g = groups.data()[j];
              const bool is_selected =
                  win.od.selected_index == static_cast<int>(j);
              ImGui::PushID(static_cast<int>(j));
              ImGui::TableNextRow();

              // Segs
              ImGui::TableSetColumnIndex(0);
              const String seg_count =
                  String::sprintf(ctx.frame_arena, "%u", g.count);
              if (ImGui::Selectable(seg_count.data, is_selected,
                                    ImGuiSelectableFlags_SpanAllColumns) ||
                  ImGui::IsItemFocused()) {
                win.od.selected_index = static_cast<int>(j);
              }

              if (ui_context_menu(is_selected, win.od.context_menu_column,
                                  kGroupedCols)) {
                win.od.selected_index = static_cast<int>(j);
                const String cell = smaps_group_cell_text(
                    ctx.frame_arena, g, win.od.context_menu_column);
                if (ImGui::MenuItemEx(
                        copy_cell_menu_label(ctx.frame_arena, cell).data,
                        ICON_MD_CONTENT_COPY)) {
                  clipboard_copy_cell(view_state.notifications, cell);
                }
                if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
                  copy_smaps_group(view_state.notifications, ctx.frame_arena,
                                   g);
                }
                if (ImGui::MenuItem("Copy All")) {
                  copy_all_smaps_groups(view_state.notifications,
                                        ctx.frame_arena, groups.data(),
                                        groups.size());
                }
                ImGui::EndPopup();
              }

              // Size
              ImGui::TableSetColumnIndex(1);
              table_item_draw_memory(g.size_kb * 1024.0);

              // RSS
              ImGui::TableSetColumnIndex(2);
              table_item_draw_memory(g.rss_kb * 1024.0);

              // PSS
              ImGui::TableSetColumnIndex(3);
              table_item_draw_memory(g.pss_kb * 1024.0);

              // Private
              ImGui::TableSetColumnIndex(4);
              table_item_draw_memory(g.private_kb * 1024.0);

              // Swap
              ImGui::TableSetColumnIndex(5);
              table_item_draw_memory(g.swap_kb * 1024.0);

              // Mapping
              ImGui::TableSetColumnIndex(6);
              ImGui::TextUnformatted(g.name);

              ImGui::PopID();
            }

            ui_pop_mono_font();
            ImGui::EndTable();

            if (shortcut_copy_row(win.od.selected_index, groups.size())) {
              copy_smaps_group(view_state.notifications, ctx.frame_arena,
                               groups.data()[win.od.selected_index]);
            }
          }
        } else {
          // ---- Flat mode ----
          constexpr int kFlatCols = eSmapsViewerColumnId_SegmentCount;
          if (ImGui::BeginTable("MemMaps", kFlatCols, COMMON_TABLE_FLAGS)) {
            ui_push_mono_font();
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Address",
                                    ImGuiTableColumnFlags_WidthFixed |
                                        ImGuiTableColumnFlags_NoHide,
                                    0.0f, eSmapsViewerColumnId_Address);
            ImGui::TableSetupColumn("Perms", ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Perms);
            ImGui::TableSetupColumn("Size",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Size);
            ImGui::TableSetupColumn("RSS",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Rss);
            ImGui::TableSetupColumn("PSS",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Pss);
            ImGui::TableSetupColumn("Private",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Private);
            ImGui::TableSetupColumn("Swap",
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                        ImGuiTableColumnFlags_WidthFixed,
                                    0.0f, eSmapsViewerColumnId_Swap);
            ImGui::TableSetupColumn("Mapping", ImGuiTableColumnFlags_None, 0.0f,
                                    eSmapsViewerColumnId_Mapping);
            ImGui::TableHeadersRow();

            handle_table_sort_specs(win.od.sorted_by, win.od.sorted_order,
                                    [&] { sort_segments(win); });

            for (uint32_t j = 0; j < win.segments.size; ++j) {
              const SmapsSegment &seg = win.segments.data[j];

              const String filter_str = smaps_filter_str(ctx, seg);
              if (!filter.PassFilter(filter_str.data)) continue;

              const bool is_selected =
                  win.od.selected_index == static_cast<int>(j);
              ImGui::PushID(static_cast<int>(j));
              ImGui::TableNextRow();

              // Address
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Address);
              const String addr_buf = String::sprintf(
                  ctx.frame_arena, "%lx-%lx", seg.start_addr, seg.end_addr);
              if (ImGui::Selectable(addr_buf.data, is_selected,
                                    ImGuiSelectableFlags_SpanAllColumns) ||
                  ImGui::IsItemFocused()) {
                win.od.selected_index = static_cast<int>(j);
              }

              if (ui_context_menu(is_selected, win.od.context_menu_column,
                                  kFlatCols)) {
                win.od.selected_index = static_cast<int>(j);
                const String cell = smaps_cell_text(ctx.frame_arena, seg,
                                                    win.od.context_menu_column);
                if (ImGui::MenuItemEx(
                        copy_cell_menu_label(ctx.frame_arena, cell).data,
                        ICON_MD_CONTENT_COPY)) {
                  clipboard_copy_cell(view_state.notifications, cell);
                }
                if (ImGui::MenuItem("Copy Row", "Ctrl+C")) {
                  copy_smaps_row(view_state.notifications, ctx.frame_arena,
                                 seg);
                }
                if (ImGui::MenuItem("Copy All")) {
                  copy_all_smaps(view_state.notifications, ctx.frame_arena,
                                 win);
                }
                ImGui::EndPopup();
              }

              // Perms
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Perms);
              ImGui::TextUnformatted(seg.perms);

              // Size
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Size);
              table_item_draw_memory(seg.size_kb * 1024.0);

              // RSS
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Rss);
              table_item_draw_memory(seg.rss_kb * 1024.0);

              // PSS
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Pss);
              table_item_draw_memory(seg.pss_kb * 1024.0);

              // Private
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Private);
              table_item_draw_memory(
                  (seg.private_clean_kb + seg.private_dirty_kb) * 1024.0);

              // Swap
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Swap);
              table_item_draw_memory(seg.swap_kb * 1024.0);

              // Mapping
              ImGui::TableSetColumnIndex(eSmapsViewerColumnId_Mapping);
              ImGui::TextUnformatted(segment_label(seg));

              ImGui::PopID();
            }

            ui_pop_mono_font();
            ImGui::EndTable();

            if (shortcut_copy_row(win.od.selected_index, win.segments.size)) {
              copy_smaps_row(view_state.notifications, ctx.frame_arena,
                             win.segments.data[win.od.selected_index]);
            }
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
