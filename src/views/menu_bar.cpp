#include "views/menu_bar.h"

#include "views/process_host.h"
#include "views/view_state.h"

#include "imgui.h"
#include "tracy/Tracy.hpp"

static void apply_theme(const bool dark_mode) {
  if (dark_mode) {
    ImGui::StyleColorsDark();
  } else {
    ImGui::StyleColorsLight();
  }
}

static constexpr float PERIODS[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f};
static const char *LABELS[] = {"Paused", "0.25s", "0.5s", "1s", "2s", "5s"};

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

    if (ImGui::RadioButton("Light Mode", !prefs.dark_mode)) {
      prefs.dark_mode = false;
      apply_theme(prefs.dark_mode);
    }
    if (ImGui::RadioButton("Dark Mode", prefs.dark_mode)) {
      prefs.dark_mode = true;
      apply_theme(prefs.dark_mode);
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
    if (ImGui::Combo("Update Period", &current_idx, LABELS, 6)) {
      prefs.update_period = PERIODS[current_idx];
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Rendering");
    ImGui::Separator();

    ImGui::SetNextItemWidth(100);
    ImGui::SliderInt("Target FPS", &prefs.target_fps, 15, 144);

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
    ImGui::EndMenuBar();
  }

  draw_preferences_modal(view_state.preferences_state);
}
