#include "views/menu_bar.h"

#include "constants.h"
#include "style_control.h"
#include "views/common.h"
#include "views/licenses.h"
#include "views/on_demand_common.h"
#include "views/process_host.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <cstring>
#include <iterator>

#ifndef PROCK_VERSION
#define PROCK_VERSION "unknown"
#endif

static constexpr float PERIODS[] = {0.0f, 0.25f, 0.5f,
                                    1.0f, 2.0f,  UPDATE_PERIOD_MAX_SEC};
static const char *PERIOD_LABELS[] = {"Paused", "0.25s", "0.5s",
                                      "1s",     "2s",    "5s"};
static const char *PREFERENCES_TITLE = "Preferences";
static const char *ABOUT_TITLE = "About Prock";
static const char *LICENSES_TITLE = "Third-Party Licenses";
static const char *THEME_EDITOR_TITLE = "Theme Editor";
static constexpr float UI_ELEMENT_WIDTH = 220.0f;
static constexpr float FONT_POPUP_WIDTH = 300.0f;
static constexpr float SETTING_LABEL_WIDTH = 130.0f;
static constexpr float CLOSE_BUTTON_WIDTH = 120.0f;

// Left-hand label column shared by all Preferences rows; the widget that
// follows starts at a fixed x so rows line up across sections.
static void setting_label(const char *label, const char *tooltip = nullptr) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (tooltip != nullptr) {
    ImGui::SetItemTooltip("%s", tooltip);
  }
  ImGui::SameLine(SETTING_LABEL_WIDTH * ui_scale());
}

static bool input_int(const char *title, int &value, const int min,
                      const int max) {
  setting_label(title);
  ImGui::PushID(title);
  ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * ui_scale());
  ImGui::InputInt("##value", &value, 1, 10);
  const bool edited = ImGui::IsItemDeactivatedAfterEdit();
  ImGui::PopID();
  if (edited) {
    value = std::clamp(value, min, max);
    return true;
  }
  return false;
}

static void draw_font_picker(PreferencesState &prefs, const char *label,
                             const char *tooltip, const char *default_label,
                             char *path, const size_t path_size) {
  ImGui::PushID(label);
  setting_label(label, tooltip);

  if (prefs.font_list.size == 0) {
    if (!prefs.font_list_received) {
      ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * ui_scale());
      ImGui::BeginDisabled();
      if (ImGui::BeginCombo("##font", "Loading fonts...")) {
        ImGui::EndCombo();
      }
      ImGui::EndDisabled();
    } else {
      ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * ui_scale());
      ImGui::InputTextWithHint("##font", "Path to .ttf file (empty = default)",
                               path, path_size);
      ImGui::SameLine();
      if (ImGui::Button("Apply")) {
        prefs.font_needs_reload = true;
      }
    }
    ImGui::PopID();
    return;
  }

  const char *preview = default_label;
  if (path[0] != '\0') {
    preview = path;
    for (uint32_t i = 0; i < prefs.font_list.size; i++) {
      if (strcmp(path, prefs.font_list.data[i].path) == 0) {
        preview = prefs.font_list.data[i].name;
        break;
      }
    }
  }

  ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * ui_scale());
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(FONT_POPUP_WIDTH * ui_scale(), 0.0f), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginCombo("##font", preview)) {
    const bool appearing = ImGui::IsWindowAppearing();
    if (appearing) {
      prefs.font_filter[0] = '\0';
      ImGui::SetKeyboardFocusHere();
    }
    ImGuiTextFilter filter;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui_filter_input(filter, "##FontFilter", prefs.font_filter,
                    sizeof(prefs.font_filter));

    const float list_height = 8.25f * ImGui::GetTextLineHeightWithSpacing();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
    const bool list_open = ImGui::BeginChild(
        "##FontList", ImVec2(0.0f, list_height),
        ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (list_open) {
      if (ImGui::Selectable(default_label, path[0] == '\0')) {
        path[0] = '\0';
        prefs.font_needs_reload = true;
        ImGui::CloseCurrentPopup();
      }
      for (uint32_t i = 0; i < prefs.font_list.size; i++) {
        const FontEntry &entry = prefs.font_list.data[i];
        const bool selected = strcmp(path, entry.path) == 0;
        if (!filter.PassFilter(entry.name)) {
          continue;
        }
        if (ImGui::Selectable(entry.name, selected)) {
          snprintf(path, path_size, "%s", entry.path);
          prefs.font_needs_reload = true;
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", entry.path);
        }
        if (selected && appearing) {
          ImGui::SetScrollHereY(0.5f);
        }
      }
    }
    ImGui::EndChild();
    ImGui::EndCombo();
  } else if (path[0] != '\0' && ImGui::IsItemHovered()) {
    // The narrow combo can clip a long font name; show it in full.
    ImGui::SetTooltip("%s\n%s", preview, path);
  }
  ImGui::PopID();
}

static void draw_theme_editor_modal(PreferencesState &prefs) {
  if (prefs.show_theme_editor_modal) {
    ImGui::OpenPopup(THEME_EDITOR_TITLE);
  }

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(600.0f * ui_scale(), 500.0f * ui_scale()), ImGuiCond_Appearing);

  if (ImGui::BeginPopupModal(THEME_EDITOR_TITLE, &prefs.show_theme_editor_modal)) {
    if (popup_close_on_escape()) {
      prefs.show_theme_editor_modal = false;
    }

    static ImGuiTextFilter filter;
    
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_MD_SEARCH);
    ImGui::SameLine();
    filter.Draw("##ColorFilter", 200.0f * ui_scale());
    
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 200.0f * ui_scale());
    ImGui::SetNextItemWidth(200.0f * ui_scale());
    if (ImGui::BeginCombo("##LoadPreset", "Load Preset...")) {
      for (int i = 0; i < static_cast<int>(Theme::Custom); i++) {
        const Theme t = static_cast<Theme>(i);
        if (ImGui::Selectable(theme_name(t))) {
          ImGuiStyle temp_style;
          apply_theme(t, &temp_style);
          for (int j = 0; j < ImGuiCol_COUNT; ++j) {
            g_custom_colors[j] = temp_style.Colors[j];
          }
          for (int j = 0; j < eAppColor_COUNT; ++j) {
            g_custom_app_colors[j] = g_app_colors[j];
          }
          style_control_force_update();
          style_control_rebuild(prefs.zoom_scale_pct, prefs.window_opacity_pct);
        }
      }
      ImGui::EndCombo();
    }
    
    ImGui::Spacing();

    if (ImGui::BeginTabBar("ThemeEditorTabs")) {
      if (ImGui::BeginTabItem("ImGui Colors")) {
        ImGui::BeginChild("ImGuiColorsScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y));
        for (int i = 0; i < ImGuiCol_COUNT; ++i) {
          const char* color_name = ImGui::GetStyleColorName(i);
          if (!filter.PassFilter(color_name)) continue;
          
          ImGui::PushID(i);
          if (ImGui::ColorEdit4(color_name, (float*)&g_custom_colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            if (prefs.theme == Theme::Custom) {
              style_control_force_update();
              style_control_rebuild(prefs.zoom_scale_pct, prefs.window_opacity_pct);
            }
          }
          ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("App Colors")) {
        ImGui::BeginChild("AppColorsScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y));
        static const char* app_color_names[eAppColor_COUNT] = {
          "NewProcessRow",
          "DeadProcessRow",
          "ErrorText",
          "WarningText",
          "InfoText"
        };
        for (int i = 0; i < eAppColor_COUNT; ++i) {
          if (!filter.PassFilter(app_color_names[i])) continue;
          
          ImGui::PushID(i);
          if (ImGui::ColorEdit4(app_color_names[i], (float*)&g_custom_app_colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            if (prefs.theme == Theme::Custom) {
              style_control_force_update();
              style_control_rebuild(prefs.zoom_scale_pct, prefs.window_opacity_pct);
            }
          }
          ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    if (ImGui::Button("Close", ImVec2(CLOSE_BUTTON_WIDTH * ui_scale(), 0.0f))) {
      ImGui::CloseCurrentPopup();
      prefs.show_theme_editor_modal = false;
    }

    ImGui::EndPopup();
  }
}

static void draw_preferences_modal(PreferencesState &prefs) {
  if (prefs.show_preferences_modal) {
    ImGui::OpenPopup(PREFERENCES_TITLE);
  }

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal(PREFERENCES_TITLE, &prefs.show_preferences_modal,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    // Esc closes the preferences modal
    if (popup_close_on_escape()) {
      prefs.show_preferences_modal = false;
    }

    if (g_borrowed_config) {
      ImGui::TextDisabled(ICON_MD_SHIELD
                          " Elevated session: changes apply now, not saved");
      ImGui::Spacing();
    }

    ImGui::SeparatorText("Appearance");

    const float scale = ui_scale();

    bool changed_style = false;

    setting_label("Theme");
    ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * scale);
    if (ImGui::BeginCombo("##Theme", theme_name(prefs.theme))) {
      for (int i = 0; i < static_cast<int>(Theme::COUNT); i++) {
        const Theme t = static_cast<Theme>(i);
        const bool is_selected = prefs.theme == t;
        if (ImGui::Selectable(theme_name(t), is_selected)) {
          prefs.theme = t;
          changed_style = true;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Edit Colors")) {
      prefs.theme = Theme::Custom;
      changed_style = true;
      prefs.show_theme_editor_modal = true;
      prefs.show_preferences_modal = false;
    }

    changed_style |=
        input_int("Zoom", prefs.zoom_scale_pct, ZOOM_MIN_PCT, ZOOM_MAX_PCT);
    changed_style |= input_int("Opacity", prefs.window_opacity_pct, 0, 100);

    if (changed_style) {
      style_control_select_theme(prefs.theme);
      style_control_rebuild(prefs.zoom_scale_pct, prefs.window_opacity_pct);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Rendering");

    if (input_int("FPS Limit", prefs.target_fps, TARGET_FPS_MIN,
                  TARGET_FPS_MAX)) {
      style_control_set_target_fps(prefs.target_fps);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Updates");

    int current_idx = 2; // default to 0.5s
    for (int i = 0; i < 6; i++) {
      if (prefs.update_period == PERIODS[i]) {
        current_idx = i;
        break;
      }
    }

    setting_label("Update Period");
    ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * scale);
    if (ImGui::Combo("##UpdatePeriod", &current_idx, PERIOD_LABELS, 6)) {
      prefs.update_period = PERIODS[current_idx];
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Fonts");

    draw_font_picker(
        prefs, "UI Font", "Used for menus, toolbars, charts, and dialogs",
        "Default (Inter)", prefs.font_path, sizeof(prefs.font_path));
    draw_font_picker(prefs, "Monospace Font",
                     "Used for data tables, keeping digits and paths aligned",
                     "Default (JetBrains Mono)", prefs.mono_font_path,
                     sizeof(prefs.mono_font_path));

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Dumps");
    setting_label("Folder");
    ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * scale);
    ImGui::InputTextWithHint("##DumpDir", "Folder for core dumps",
                             prefs.dump_dir, sizeof(prefs.dump_dir));

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Recordings");
    setting_label("Folder");
    ImGui::SetNextItemWidth(UI_ELEMENT_WIDTH * scale);
    ImGui::InputTextWithHint(
        "##RecordingsDir", "Folder for playback recordings",
        prefs.recordings_dir, sizeof(prefs.recordings_dir));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(UI_ELEMENT_WIDTH * scale, 0))) {
      ImGui::CloseCurrentPopup();
      prefs.show_preferences_modal = false;
    }

    ImGui::EndPopup();
  }
}

static void open_url(const char *url) {
  ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
  if (pio.Platform_OpenInShellFn != nullptr) {
    pio.Platform_OpenInShellFn(ImGui::GetCurrentContext(), url);
  }
}

static void about_centered_text(const char *text) {
  const float item_w = ImGui::CalcTextSize(text).x;
  const float avail = ImGui::GetContentRegionAvail().x;
  if (item_w < avail) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - item_w) * 0.5f);
  }
  ImGui::TextUnformatted(text);
}

static void draw_licenses_modal(PreferencesState &prefs) {
  if (prefs.show_licenses_modal) {
    ImGui::OpenPopup(LICENSES_TITLE);
  }

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(640.0f * ui_scale(), 480.0f * ui_scale()),
                           ImGuiCond_Appearing);

  if (ImGui::BeginPopupModal(LICENSES_TITLE, &prefs.show_licenses_modal)) {
    if (popup_close_on_escape()) {
      prefs.show_licenses_modal = false;
    }

    ImGui::TextWrapped(
        "Prock bundles the third-party libraries and fonts listed below. "
        "Expand an entry to read its full license.");
    ImGui::Spacing();

    const float footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("licenses_scroll", ImVec2(0.0f, -footer),
                          ImGuiChildFlags_Borders)) {
      for (const ThirdPartyLicense &lic : THIRD_PARTY_LICENSES) {
        if (ImGui::CollapsingHeader(lic.name)) {
          ImGui::TextUnformatted(lic.text);
        }
      }
    }
    ImGui::EndChild();

    if (ImGui::Button("Close", ImVec2(CLOSE_BUTTON_WIDTH * ui_scale(), 0.0f))) {
      ImGui::CloseCurrentPopup();
      prefs.show_licenses_modal = false;
    }

    ImGui::EndPopup();
  }
}

static void draw_about_modal(PreferencesState &prefs) {
  if (prefs.show_about_modal) {
    ImGui::OpenPopup(ABOUT_TITLE);
  }

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal(ABOUT_TITLE, &prefs.show_about_modal,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (popup_close_on_escape()) {
      prefs.show_about_modal = false;
    }

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
    about_centered_text("Prock");
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    about_centered_text(PROCK_VERSION);
    ImGui::PopStyleColor();

    ImGui::Spacing();

    ImGui::Text("A process explorer and system monitor for Linux");
    ImGui::Text("Created by Dmitrii Matrokhin");
    ImGui::SameLine();
    ImGui::TextDisabled("<matrokhin.d@gmail.com>");

    ImGui::Spacing();

    ImGui::TextLinkOpenURL("Source code", "https://github.com/matrohin/prock");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(CLOSE_BUTTON_WIDTH * ui_scale(), 0.0f))) {
      ImGui::CloseCurrentPopup();
      prefs.show_about_modal = false;
    }

    ImGui::EndPopup();
  }
}

void menu_bar_update(ViewState &view_state) {
  PreferencesState &prefs = view_state.preferences_state;
  Sync &sync = *view_state.sync;

  const bool closing =
      !prefs.show_preferences_modal && prefs.prev_show_preferences;
  prefs.prev_show_preferences = prefs.show_preferences_modal;

  if (closing) {
    prefs.font_list_requested = false;
    prefs.font_list_received = false;
    prefs.font_list_arena.destroy();
    prefs.font_list = {};
  }

  if (prefs.show_preferences_modal && !prefs.font_list_requested) {
    prefs.font_list_requested = on_demand_send_request(
        sync, sync.on_demand_reader.font_list_request_queue, FontListRequest{});
  }

  FontListResponse response;
  if (sync.on_demand_reader.font_list_response_queue.pop(response)) {
    if (prefs.font_list_requested) {
      prefs.font_list_arena.destroy();
      prefs.font_list_arena = BumpArena::create();
      prefs.font_list =
          Array<FontEntry>::copy_from(prefs.font_list_arena, response.fonts);
      prefs.font_list_received = true;
    }
    response.owner_arena.destroy();
  }
}

void menu_bar_draw(ViewState &view_state) {
  ZoneScoped;
  if ((!view_state.preferences_state.show_menu_on_alt ||
       ImGui::IsKeyDown(ImGuiKey_LeftAlt)) &&
      ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

      if (ImGui::MenuItem("Per-core CPU %", nullptr,
                          view_state.preferences_state.cpu_per_core)) {
        view_state.preferences_state.cpu_per_core =
            !view_state.preferences_state.cpu_per_core;
        view_state.system_cpu_chart_state.y_axis_fitted = 0;
      }

      if (ImGui::MenuItem("Stacked", nullptr,
                          view_state.system_cpu_chart_state.stacked,
                          view_state.preferences_state.cpu_per_core)) {
        view_state.system_cpu_chart_state.stacked =
            !view_state.system_cpu_chart_state.stacked;
        view_state.system_cpu_chart_state.y_axis_fitted = 0;
      }

      if (ImGui::MenuItem("Show menu bar on Alt", nullptr,
                          view_state.preferences_state.show_menu_on_alt)) {
        view_state.preferences_state.show_menu_on_alt =
            !view_state.preferences_state.show_menu_on_alt;
      }

      if (view_state.process_host_state.focused_pid > 0) {
        ImGui::Separator();
        if (ImGui::MenuItem("Restore Process Window Layout")) {
          process_host_restore_layout(
              view_state, view_state.process_host_state.focused_pid);
        }
      }

      ImGui::PopItemFlag();
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Command Palette...", "Ctrl+P")) {
        view_state.command_state.show_palette = true;
      }
      char rec_label[64];
      ImGui::BeginDisabled(view_state.sync->replay_mode);
      if (ImGui::MenuItem(
              recorder_toggle_label(view_state, rec_label, sizeof(rec_label)),
              "Ctrl+Shift+R")) {
        view_state.recorder.toggle_request = true;
      }
      ImGui::EndDisabled();
      if (ImGui::MenuItem("Replay a recording...")) {
        view_state.replay_state.show_open_dialog = true;
      }
      if (ImGui::MenuItem("Preferences...")) {
        view_state.preferences_state.show_preferences_modal = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Report a Bug")) {
        open_url("https://github.com/matrohin/prock/issues");
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Third-Party Licenses...")) {
        view_state.preferences_state.show_licenses_modal = true;
      }
      if (ImGui::MenuItem("About Prock...")) {
        view_state.preferences_state.show_about_modal = true;
      }
      ImGui::EndMenu();
    }

    // Draw FPS on the right side if debug mode enabled (toggle with F3)
    if (view_state.preferences_state.show_debug_fps) {
      char fps_text[32];
      snprintf(fps_text, sizeof(fps_text), "%.1f FPS",
               static_cast<double>(ImGui::GetIO().Framerate));
      const float text_width = ImGui::CalcTextSize(fps_text).x;
      const float menu_bar_width = ImGui::GetWindowWidth();
      const float spacing = ImGui::GetStyle().ItemSpacing.x;
      ImGui::SameLine(menu_bar_width - text_width - spacing);
      ImGui::TextDisabled("%s", fps_text);
    }

    ImGui::EndMenuBar();
  }

  draw_preferences_modal(view_state.preferences_state);
  draw_theme_editor_modal(view_state.preferences_state);
  draw_about_modal(view_state.preferences_state);
  draw_licenses_modal(view_state.preferences_state);
}
