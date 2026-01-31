#include "views/menu_bar.h"

#include "views/process_host.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

static constexpr float PERIODS[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};
static const char *PERIOD_LABELS[] = {"Paused", "0.25s", "0.5s",
                                      "1s",     "2s",    "5s"};

static constexpr float ZOOM_SCALES[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
static const char *ZOOM_LABELS[] = {"75%", "100%", "125%", "150%", "200%"};
static constexpr int ZOOM_COUNT = 5;

static void draw_preferences_modal(PreferencesState &prefs) {
  if (prefs.show_preferences_modal) {
    ImGui::OpenPopup("Preferences");
  }

  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("Preferences", &prefs.show_preferences_modal,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Appearance");
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("Theme", theme_name(prefs.theme))) {
      for (int i = 0; i < static_cast<int>(Theme::COUNT); i++) {
        Theme t = static_cast<Theme>(i);
        bool is_selected = (prefs.theme == t);
        if (ImGui::Selectable(theme_name(t), is_selected)) {
          prefs.theme = t;
          apply_theme(prefs.theme);
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Display");
    ImGui::Separator();

    int zoom_idx = 1; // default to 100%
    for (int i = 0; i < ZOOM_COUNT; i++) {
      if (prefs.zoom_scale == ZOOM_SCALES[i]) {
        zoom_idx = i;
        break;
      }
    }

    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("Zoom", &zoom_idx, ZOOM_LABELS, ZOOM_COUNT)) {
      prefs.zoom_scale = ZOOM_SCALES[zoom_idx];
    }

    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##Font", "Path to .ttf file (empty = default)",
                             prefs.font_path, sizeof(prefs.font_path));
    ImGui::SameLine();
    if (ImGui::Button("Apply Font")) {
      prefs.font_needs_reload = true;
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Updates");
    ImGui::Separator();

    int current_idx = 2; // default to 0.5s
    for (int i = 0; i < 6; i++) {
      if (prefs.update_period == PERIODS[i]) {
        current_idx = i;
        break;
      }
    }

    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("Update Period", &current_idx, PERIOD_LABELS, 6)) {
      prefs.update_period = PERIODS[current_idx];
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Rendering");
    ImGui::Separator();

    ImGui::SetNextItemWidth(100);
    ImGui::SliderInt("Target FPS", &prefs.target_fps, 15, 60);

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

void menu_bar_draw(ViewState &view_state) {
  ZoneScoped;
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

      // System CPU Chart options
      if (ImGui::BeginMenu("System CPU")) {
        if (ImGui::MenuItem("Per-core", nullptr,
                            view_state.system_cpu_chart_state.show_per_core)) {
          view_state.system_cpu_chart_state.show_per_core =
              !view_state.system_cpu_chart_state.show_per_core;
        }
        if (ImGui::MenuItem("Stacked", nullptr,
                            view_state.system_cpu_chart_state.stacked,
                            view_state.system_cpu_chart_state.show_per_core)) {
          view_state.system_cpu_chart_state.stacked =
              !view_state.system_cpu_chart_state.stacked;
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();

      const bool has_focused_process =
          view_state.process_host_state.focused_pid > 0;
      if (ImGui::MenuItem("Restore Process Window Layout", nullptr, false,
                          has_focused_process)) {
        process_host_restore_layout(view_state,
                                    view_state.process_host_state.focused_pid);
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

    // Draw FPS on the right side if debug mode enabled (toggle with F3)
    if (view_state.preferences_state.show_debug_fps) {
      char fps_text[32];
      snprintf(fps_text, sizeof(fps_text), "%.1f FPS",
               static_cast<double>(ImGui::GetIO().Framerate));
      float text_width = ImGui::CalcTextSize(fps_text).x;
      float menu_bar_width = ImGui::GetWindowWidth();
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      ImGui::SameLine(menu_bar_width - text_width - spacing);
      ImGui::TextDisabled("%s", fps_text);
    }

    ImGui::EndMenuBar();
  }

  draw_preferences_modal(view_state.preferences_state);
}
