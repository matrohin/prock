#include "views/command_palette.h"

#include "constants.h"
#include "style_control.h"
#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>

struct Command {
  const char *label;     // shown in the palette
  ImGuiKeyChord binding; // fixed shortcut (0 = palette-only)
  void (*execute)(ViewState &);
};

static void cmd_open_palette(ViewState &vs) {
  vs.command_state.show_palette = true;
}
static void cmd_toggle_auto_follow(ViewState &vs) {
  vs.preferences_state.auto_follow = !vs.preferences_state.auto_follow;
}
static void cmd_toggle_auto_fit_y(ViewState &vs) {
  vs.preferences_state.y_auto_fit = !vs.preferences_state.y_auto_fit;
}
static void cmd_open_preferences(ViewState &vs) {
  vs.preferences_state.show_preferences_modal = true;
}
static void cmd_toggle_per_core_cpu(ViewState &vs) {
  vs.preferences_state.cpu_per_core = !vs.preferences_state.cpu_per_core;
  vs.system_cpu_chart_state.y_axis_fitted = 0;
}
static void cmd_toggle_stacked(ViewState &vs) {
  vs.system_cpu_chart_state.stacked = !vs.system_cpu_chart_state.stacked;
  vs.system_cpu_chart_state.y_axis_fitted = 0;
}
static void cmd_toggle_menu_on_alt(ViewState &vs) {
  vs.preferences_state.show_menu_on_alt =
      !vs.preferences_state.show_menu_on_alt;
}
static void apply_zoom(ViewState &vs, const int delta_pct) {
  PreferencesState &prefs = vs.preferences_state;
  prefs.zoom_scale_pct =
      std::clamp(prefs.zoom_scale_pct + delta_pct, ZOOM_MIN_PCT, ZOOM_MAX_PCT);
  style_control_rebuild(prefs.zoom_scale_pct, prefs.window_opacity_pct);
}
static void cmd_zoom_in(ViewState &vs) { apply_zoom(vs, ZOOM_STEP_PCT); }
static void cmd_zoom_out(ViewState &vs) { apply_zoom(vs, -ZOOM_STEP_PCT); }
static void cmd_focus_process_filter(ViewState &vs) {
  vs.brief_table_state.focus_filter_requested = true;
}
static void cmd_show_licenses(ViewState &vs) {
  vs.preferences_state.show_licenses_modal = true;
}
static void cmd_show_about(ViewState &vs) {
  vs.preferences_state.show_about_modal = true;
}

// Entries are listed in CommandId order. A 0 chord means the command has no
// global shortcut and is reachable only through the palette (or an existing
// menu item), which avoids colliding with the table's own key handling.
static const Command g_commands[eCommand_Count] = {
    {"Filter processes in the table",
     ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F, cmd_focus_process_filter},
    {"Toggle auto-follow", ImGuiKey_Space, cmd_toggle_auto_follow},
    {"Toggle auto-fit Y axis", ImGuiMod_Shift | ImGuiKey_Space,
     cmd_toggle_auto_fit_y},
    {"Zoom in", ImGuiMod_Ctrl | ImGuiKey_Equal, cmd_zoom_in},
    {"Zoom out", ImGuiMod_Ctrl | ImGuiKey_Minus, cmd_zoom_out},
    {"Toggle per-core CPU view", 0, cmd_toggle_per_core_cpu},
    {"Toggle stacked CPU chart", 0, cmd_toggle_stacked},
    {"Toggle menu bar on Alt", 0, cmd_toggle_menu_on_alt},
    {"Open Preferences...", 0, cmd_open_preferences},
    {"Open command palette", ImGuiMod_Ctrl | ImGuiKey_P, cmd_open_palette},
    {"Show third-party licenses", 0, cmd_show_licenses},
    {"About Prock", 0, cmd_show_about},
};

void command_dispatch(ViewState &vs) {
  for (uint32_t i = 0; i < eCommand_Count; ++i) {
    const ImGuiKeyChord chord = g_commands[i].binding;
    if (chord != 0 && ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal)) {
      g_commands[i].execute(vs);
    }
  }

  // Extra zoom triggers a single chord per command can't express:
  const ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl && io.MouseWheel != 0.0f) {
    apply_zoom(vs, io.MouseWheel > 0.0f ? ZOOM_STEP_PCT : -ZOOM_STEP_PCT);
  }
}

static const char *PALETTE_TITLE = "Command Palette";

void command_palette_draw(ViewState &vs) {
  CommandState &cs = vs.command_state;

  if (cs.show_palette) {
    cs.show_palette = false;
    // Don't stack the palette onto another popup; the two fight over the popup
    // stack each frame and flicker. (Zoom etc. still work over popups since
    // they don't open one.)
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
      ImGui::OpenPopup(PALETTE_TITLE);
    }
  }

  const ImVec2 vp = ImGui::GetMainViewport()->Size;
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  const ImVec2 size(vp.x * 0.5f, vp.y * 0.6f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);

  if (!ImGui::BeginPopupModal(
          PALETTE_TITLE, nullptr,
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs)) {
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    cs.filter[0] = '\0';
    cs.selected = 0;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }

  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SetNextItemWidth(-FLT_MIN);
  const ImGuiTextFilter filter =
      draw_filter_input("##cmdfilter", cs.filter, sizeof(cs.filter),
                        "Type to search commands...");

  int matches[eCommand_Count];
  int match_count = 0;
  for (uint32_t i = 0; i < eCommand_Count; ++i) {
    if (filter.IsActive() && !filter.PassFilter(g_commands[i].label)) {
      continue;
    }
    matches[match_count++] = static_cast<int>(i);
  }

  cs.selected =
      std::clamp(cs.selected, 0, match_count > 0 ? match_count - 1 : 0);

  int run = -1;
  bool nav_moved = false;
  if (match_count > 0) {
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                      (ctrl && ImGui::IsKeyPressed(ImGuiKey_J));
    const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                    (ctrl && ImGui::IsKeyPressed(ImGuiKey_K));
    if (down) {
      cs.selected = (cs.selected + 1) % match_count;
      nav_moved = true;
    }
    if (up) {
      cs.selected = (cs.selected + match_count - 1) % match_count;
      nav_moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
      run = matches[cs.selected];
    }
  }

  // Rows span the full width edge-to-edge; the padding lives *inside* each
  // selectable (zero window padding so the highlight reaches the border).
  const float pad_x = ImGui::GetFontSize() * 0.5f;
  const float row_h = ImGui::GetFrameHeight();
  const float text_off = (row_h - ImGui::GetTextLineHeight()) * 0.5f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool list_open = ImGui::BeginChild("##cmdlist", ImVec2(0.0f, 0.0f),
                                           ImGuiChildFlags_Borders);
  ImGui::PopStyleVar();
  if (list_open) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 label_col = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 key_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    for (int m = 0; m < match_count; ++m) {
      const int idx = matches[m];
      const Command &cmd = g_commands[idx];
      const bool is_selected = (m == cs.selected);

      ImGui::PushID(idx);
      const ImVec2 row_min = ImGui::GetCursorScreenPos();
      const float row_w = ImGui::GetContentRegionAvail().x;
      if (ImGui::Selectable("##row", is_selected,
                            ImGuiSelectableFlags_SpanAvailWidth,
                            ImVec2(0.0f, row_h))) {
        run = idx; // a click is a deliberate pick: select + run
      }
      if (is_selected && nav_moved) {
        ImGui::SetScrollHereY();
      }

      const float text_y = row_min.y + text_off;
      dl->AddText(ImVec2(row_min.x + pad_x, text_y), label_col, cmd.label);
      if (cmd.binding != 0) {
        const char *key = ImGui::GetKeyChordName(cmd.binding);
        const float key_w = ImGui::CalcTextSize(key).x;
        dl->AddText(ImVec2(row_min.x + row_w - pad_x - key_w, text_y), key_col,
                    key);
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  if (run >= 0) {
    ImGui::CloseCurrentPopup();
    cs.filter[0] = '\0';
    cs.selected = 0;
    g_commands[run].execute(vs);
  }

  ImGui::EndPopup();
}
