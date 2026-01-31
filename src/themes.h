// Theme implementations adapted from:
// - Enemymouse theme from https://gist.github.com/enemymouse/c8aa24e247a1d7b9fc33d45091cbb8f0

#pragma once

#include "imgui.h"

enum class Theme {
  Dark,
  Light,
  Classic,
  Enemymouse,
  COUNT
};

inline const char *theme_name(Theme theme) {
  switch (theme) {
    case Theme::Dark: return "Dark";
    case Theme::Light: return "Light";
    case Theme::Classic: return "Classic";
    case Theme::Enemymouse: return "Enemymouse";
    default: return "Unknown";
  }
}

inline void apply_theme(Theme theme, ImGuiStyle *dst = nullptr) {
  ImGuiStyle *style = dst ? dst : &ImGui::GetStyle();
  ImVec4 *colors = style->Colors;

  switch (theme) {
    case Theme::Dark:
      ImGui::StyleColorsDark(dst);
      break;

    case Theme::Light:
      ImGui::StyleColorsLight(dst);
      break;

    case Theme::Classic:
      ImGui::StyleColorsClassic(dst);
      break;

    case Theme::Enemymouse: {
      ImGui::StyleColorsDark(dst);
      style->ChildRounding = 3.0f;
      style->WindowRounding = 3.0f;
      style->GrabRounding = 1.0f;
      style->GrabMinSize = 20.0f;
      style->FrameRounding = 3.0f;

      colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.40f, 0.41f, 1.00f);
      colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
      colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
      colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.13f, 0.13f, 0.90f);
      colors[ImGuiCol_Border] = ImVec4(0.00f, 1.00f, 1.00f, 0.65f);
      colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
      colors[ImGuiCol_FrameBg] = ImVec4(0.44f, 0.80f, 0.80f, 0.18f);
      colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.80f, 0.80f, 0.27f);
      colors[ImGuiCol_FrameBgActive] = ImVec4(0.44f, 0.81f, 0.86f, 0.66f);
      colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.18f, 0.21f, 0.73f);
      colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
      colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.27f);
      colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
      colors[ImGuiCol_ScrollbarBg] = ImVec4(0.22f, 0.29f, 0.30f, 0.71f);
      colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.44f);
      colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
      colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 1.00f, 0.68f);
      colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.36f);
      colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.76f);
      colors[ImGuiCol_Button] = ImVec4(0.00f, 0.65f, 0.65f, 0.46f);
      colors[ImGuiCol_ButtonHovered] = ImVec4(0.01f, 1.00f, 1.00f, 0.43f);
      colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.62f);
      colors[ImGuiCol_Header] = ImVec4(0.00f, 1.00f, 1.00f, 0.33f);
      colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.42f);
      colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
      colors[ImGuiCol_Separator] = ImVec4(0.00f, 0.50f, 0.50f, 0.33f);
      colors[ImGuiCol_SeparatorHovered] = ImVec4(0.00f, 0.50f, 0.50f, 0.47f);
      colors[ImGuiCol_SeparatorActive] = ImVec4(0.00f, 0.70f, 0.70f, 1.00f);
      colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
      colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
      colors[ImGuiCol_ResizeGripActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.40f, 0.40f, 0.46f);
      colors[ImGuiCol_TabHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.43f);
      colors[ImGuiCol_TabSelected] = ImVec4(0.00f, 0.65f, 0.65f, 0.60f);
      colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.00f, 1.00f, 1.00f, 0.70f);
      colors[ImGuiCol_TabDimmed] = ImVec4(0.00f, 0.20f, 0.20f, 0.46f);
      colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.00f, 0.40f, 0.40f, 0.60f);
      colors[ImGuiCol_DockingPreview] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
      colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
      colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.30f, 0.30f, 0.60f);
      colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.70f, 0.70f, 0.50f);
      colors[ImGuiCol_TableBorderLight] = ImVec4(0.00f, 0.50f, 0.50f, 0.33f);
      colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
      colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.20f, 0.20f, 0.20f);
      colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 1.00f, 0.22f);
      colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.10f, 0.09f, 0.51f);
      break;
    }

    default:
      ImGui::StyleColorsLight(dst);
      break;
  }
}
