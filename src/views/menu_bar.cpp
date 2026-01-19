#include "views/menu_bar.h"

#include "views/view_state.h"

#include "imgui.h"

static void apply_theme(bool dark_mode) {
  if (dark_mode) {
    ImGui::StyleColorsDark();
  } else {
    ImGui::StyleColorsLight();
  }
}

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
  if (ImGui::BeginMenuBar()) {
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
