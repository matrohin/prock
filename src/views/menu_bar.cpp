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
    if (view_state.focused_view == eFocusedView_BriefTable &&
        ImGui::BeginMenu("View")) {

      ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
      if (ImGui::MenuItem("Tree View", nullptr,
                          view_state.brief_table_state.tree_mode)) {
        view_state.brief_table_state.tree_mode =
            !view_state.brief_table_state.tree_mode;
      }
      ImGui::PopItemFlag();

      ImGui::EndMenu();
    } else if (view_state.focused_view == eFocusedView_SystemCpuChart &&
               ImGui::BeginMenu("View")) {
      ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
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
      ImGui::PopItemFlag();
      ImGui::Separator();
      if (ImGui::MenuItem("Auto-Fit Once")) {
        view_state.system_cpu_chart_state.auto_fit = true;
      }
      ImGui::EndMenu();
    } else if (view_state.focused_view == eFocusedView_SystemMemChart &&
               ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Auto-Fit Once")) {
        view_state.system_mem_chart_state.auto_fit = true;
      }
      ImGui::EndMenu();
    } else if (view_state.focused_view == eFocusedView_SystemIoChart &&
               ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Auto-Fit Once")) {
        view_state.system_io_chart_state.auto_fit = true;
      }
      ImGui::EndMenu();
    } else if (view_state.focused_view == eFocusedView_CpuChart &&
               ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Auto-Fit Once")) {
        view_state.cpu_chart_state.auto_fit = true;
      }
      ImGui::EndMenu();
    } else if (view_state.focused_view == eFocusedView_MemChart &&
               ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Auto-Fit Once")) {
        view_state.mem_chart_state.auto_fit = true;
      }
      ImGui::EndMenu();
    } else if (view_state.focused_view == eFocusedView_IoChart &&
               ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Auto-Fit Once")) {
        view_state.io_chart_state.auto_fit = true;
      }
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
