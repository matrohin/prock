#include "views/menu_bar.h"

#include "views/common.h"
#include "views/process_host.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

#include <cstring>
#include <iterator>

#ifndef PROCK_VERSION
#define PROCK_VERSION "unknown"
#endif

static constexpr float PERIODS[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};
static const char *PERIOD_LABELS[] = {"Paused", "0.25s", "0.5s",
                                      "1s",     "2s",    "5s"};

static constexpr float ZOOM_SCALES[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
static const char *ZOOM_LABELS[] = {"75%", "100%", "125%", "150%", "200%"};
static constexpr int ZOOM_COUNT = std::size(ZOOM_LABELS);
static const char *PREFERENCES_TITLE = "Preferences";
static const char *ABOUT_TITLE = "About Prock";
static constexpr float FONT_LIST_WIDTH = 400.0f;

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

    ImGui::SeparatorText("Appearance");

    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("Theme", theme_name(prefs.theme))) {
      for (int i = 0; i < static_cast<int>(Theme::COUNT); i++) {
        const Theme t = static_cast<Theme>(i);
        const bool is_selected = prefs.theme == t;
        if (ImGui::Selectable(theme_name(t), is_selected)) {
          prefs.theme = t;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    int zoom_idx = 1; // default to 100%
    for (int i = 0; i < ZOOM_COUNT; i++) {
      if (prefs.zoom_scale == ZOOM_SCALES[i]) {
        zoom_idx = i;
        break;
      }
    }

    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Zoom", &zoom_idx, ZOOM_LABELS, ZOOM_COUNT)) {
      prefs.zoom_scale = ZOOM_SCALES[zoom_idx];
    }

    ImGui::SetNextItemWidth(120);
    int opacity_pct = static_cast<int>(prefs.window_opacity * 100.0f + 0.5f);
    if (ImGui::SliderInt("Opacity", &opacity_pct, 0, 100, "%d%%")) {
      prefs.window_opacity = static_cast<float>(opacity_pct) / 100.0f;
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Font");

    const float list_height = 8.0f * ImGui::GetTextLineHeightWithSpacing();
    if (prefs.font_list.size > 0) {
      ImGui::SetNextItemWidth(FONT_LIST_WIDTH);
      const ImGuiTextFilter filter = draw_filter_input(
          "##FontFilter", prefs.font_filter, sizeof(prefs.font_filter));
      bool found_in_list = (prefs.font_path[0] == '\0');
      if (ImGui::BeginListBox("##FontList",
                              ImVec2(FONT_LIST_WIDTH, list_height))) {
        const bool default_selected = (prefs.font_path[0] == '\0');
        if (ImGui::Selectable("Default (built-in)", default_selected)) {
          prefs.font_path[0] = '\0';
          prefs.font_needs_reload = true;
        }
        if (default_selected) {
          ImGui::SetItemDefaultFocus();
          if (prefs.font_scroll_to_selected) {
            ImGui::SetScrollHereY(0.5f);
            prefs.font_scroll_to_selected = false;
          }
        }
        for (uint32_t i = 0; i < prefs.font_list.size; i++) {
          const FontEntry &entry = prefs.font_list.data[i];
          const bool selected = strcmp(prefs.font_path, entry.path) == 0;
          if (selected) found_in_list = true;
          if (!filter.PassFilter(entry.name)) {
            continue;
          }
          if (ImGui::Selectable(entry.name, selected)) {
            snprintf(prefs.font_path, sizeof(prefs.font_path), "%s",
                     entry.path);
            prefs.font_needs_reload = true;
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", entry.path);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
            if (prefs.font_scroll_to_selected) {
              ImGui::SetScrollHereY(0.5f);
              prefs.font_scroll_to_selected = false;
            }
          }
        }
        ImGui::EndListBox();
      }
      if (!found_in_list) {
        ImGui::TextDisabled("Active: %s", prefs.font_path);
      }
    } else if (!prefs.font_list_received) {
      // Reserve the loaded list's footprint so the modal opens at its final
      // size and stays centered while the font list loads.
      ImGui::BeginDisabled();
      ImGui::SetNextItemWidth(FONT_LIST_WIDTH);
      draw_filter_input("##FontFilter", prefs.font_filter,
                        sizeof(prefs.font_filter));
      if (ImGui::BeginListBox("##FontList",
                              ImVec2(FONT_LIST_WIDTH, list_height))) {
        ImGui::TextDisabled("Loading fonts...");
        ImGui::EndListBox();
      }
      ImGui::EndDisabled();
    } else {
      ImGui::SetNextItemWidth(300);
      ImGui::InputTextWithHint("##Font", "Path to .ttf file (empty = default)",
                               prefs.font_path, sizeof(prefs.font_path));
      ImGui::SameLine();
      if (ImGui::Button("Apply Font")) {
        prefs.font_needs_reload = true;
      }
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

    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Update Period", &current_idx, PERIOD_LABELS, 6)) {
      prefs.update_period = PERIODS[current_idx];
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::SeparatorText("Rendering");

    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("FPS Limit", &prefs.target_fps, 15, 144);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(120, 0))) {
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

    ImGui::Spacing();

    ImGui::TextLinkOpenURL("Source code", "https://github.com/matrohin/prock");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float button_w = 120.0f;
    if (ImGui::Button("Close", ImVec2(button_w, 0.0f))) {
      ImGui::CloseCurrentPopup();
      prefs.show_about_modal = false;
    }

    ImGui::EndPopup();
  }
}

void menu_bar_update(ViewState &view_state) {
  PreferencesState &prefs = view_state.preferences_state;
  Sync &sync = *view_state.sync;

  const bool opening =
      prefs.show_preferences_modal && !prefs.prev_show_preferences;
  const bool closing =
      !prefs.show_preferences_modal && prefs.prev_show_preferences;
  prefs.prev_show_preferences = prefs.show_preferences_modal;

  if (opening) {
    prefs.font_scroll_to_selected = true;
  }
  if (closing) {
    prefs.font_list_requested = false;
    prefs.font_list_received = false;
    prefs.font_filter[0] = '\0';
    prefs.font_list_arena.destroy();
    prefs.font_list = {};
  }

  if (prefs.show_preferences_modal && !prefs.font_list_requested) {
    sync.on_demand_reader.font_list_request_queue.push({});
    sync.on_demand_reader.request_read_cv.notify_one();
    prefs.font_list_requested = true;
  }

  FontListResponse response;
  if (sync.on_demand_reader.font_list_response_queue.pop(response)) {
    if (prefs.font_list_requested) {
      prefs.font_list_arena.destroy();
      prefs.font_list_arena = BumpArena::create();
      prefs.font_list =
          Array<FontEntry>::copy_from(prefs.font_list_arena, response.fonts);
      prefs.font_list_received = true;
      prefs.font_scroll_to_selected = true;
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
  draw_about_modal(view_state.preferences_state);
}
