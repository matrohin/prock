// Theme implementations adapted from:
// - Enemymouse theme from
// https://gist.github.com/enemymouse/c8aa24e247a1d7b9fc33d45091cbb8f0

#pragma once

#include "imgui.h"

enum class Theme : uint8_t { Dark, Light, Classic, Enemymouse, Nord, COUNT };

inline const char *theme_name(const Theme theme) {
  switch (theme) {
  case Theme::Dark:
    return "Dark";
  case Theme::Light:
    return "Light";
  case Theme::Classic:
    return "Classic";
  case Theme::Enemymouse:
    return "Enemymouse";
  case Theme::Nord:
    return "Nord";
  default:
    return "Unknown";
  }
}

inline void apply_theme(const Theme theme, ImGuiStyle *dst = nullptr) {
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

  case Theme::Nord: {
    // Nord color palette
    // Polar Night (dark backgrounds)
    constexpr ImVec4 nord0(0.18f, 0.20f, 0.25f, 1.00f); // #2E3440
    constexpr ImVec4 nord1(0.23f, 0.26f, 0.32f, 1.00f); // #3B4252
    constexpr ImVec4 nord2(0.26f, 0.30f, 0.37f, 1.00f); // #434C5E
    constexpr ImVec4 nord3(0.30f, 0.34f, 0.42f, 1.00f); // #4C566A
    // Snow Storm (light text)
    constexpr ImVec4 nord4(0.85f, 0.87f, 0.91f, 1.00f); // #D8DEE9
    // Frost (blue accents)
    constexpr ImVec4 nord8(0.53f, 0.75f, 0.82f, 1.00f);  // #88C0D0
    constexpr ImVec4 nord9(0.51f, 0.63f, 0.76f, 1.00f);  // #81A1C1
    constexpr ImVec4 nord10(0.37f, 0.51f, 0.67f, 1.00f); // #5E81AC
    // Aurora (accent colors)
    constexpr ImVec4 nord11(0.75f, 0.38f, 0.42f, 1.00f); // #BF616A red
    constexpr ImVec4 nord13(0.92f, 0.80f, 0.55f, 1.00f); // #EBCB8B yellow
    constexpr ImVec4 nord14(0.64f, 0.75f, 0.55f, 1.00f); // #A3BE8C green

    auto with_alpha = [](ImVec4 c, float a) {
      return ImVec4(c.x, c.y, c.z, a);
    };

    ImGui::StyleColorsDark(dst);
    style->WindowRounding = 4.0f;
    style->ChildRounding = 4.0f;
    style->FrameRounding = 4.0f;
    style->GrabRounding = 4.0f;
    style->PopupRounding = 4.0f;
    style->ScrollbarRounding = 4.0f;
    style->TabRounding = 4.0f;

    colors[ImGuiCol_Text] = nord4;
    colors[ImGuiCol_TextDisabled] = nord3;
    colors[ImGuiCol_WindowBg] = nord0;
    colors[ImGuiCol_ChildBg] = with_alpha(nord0, 0.00f);
    colors[ImGuiCol_PopupBg] = with_alpha(nord1, 0.95f);
    colors[ImGuiCol_Border] = with_alpha(nord2, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = nord1;
    colors[ImGuiCol_FrameBgHovered] = nord2;
    colors[ImGuiCol_FrameBgActive] = nord3;
    colors[ImGuiCol_TitleBg] = nord0;
    colors[ImGuiCol_TitleBgActive] = nord1;
    colors[ImGuiCol_TitleBgCollapsed] = with_alpha(nord0, 0.75f);
    colors[ImGuiCol_MenuBarBg] = nord1;
    colors[ImGuiCol_ScrollbarBg] = nord0;
    colors[ImGuiCol_ScrollbarGrab] = nord2;
    colors[ImGuiCol_ScrollbarGrabHovered] = nord3;
    colors[ImGuiCol_ScrollbarGrabActive] = nord9;
    colors[ImGuiCol_CheckMark] = nord8;
    colors[ImGuiCol_SliderGrab] = nord9;
    colors[ImGuiCol_SliderGrabActive] = nord8;
    colors[ImGuiCol_Button] = with_alpha(nord10, 0.60f);
    colors[ImGuiCol_ButtonHovered] = with_alpha(nord9, 0.80f);
    colors[ImGuiCol_ButtonActive] = nord8;
    colors[ImGuiCol_Header] = with_alpha(nord10, 0.45f);
    colors[ImGuiCol_HeaderHovered] = with_alpha(nord9, 0.60f);
    colors[ImGuiCol_HeaderActive] = with_alpha(nord8, 0.80f);
    colors[ImGuiCol_Separator] = with_alpha(nord2, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = with_alpha(nord9, 0.78f);
    colors[ImGuiCol_SeparatorActive] = nord8;
    colors[ImGuiCol_ResizeGrip] = with_alpha(nord10, 0.40f);
    colors[ImGuiCol_ResizeGripHovered] = with_alpha(nord9, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = with_alpha(nord8, 0.95f);
    colors[ImGuiCol_Tab] = nord1;
    colors[ImGuiCol_TabHovered] = with_alpha(nord9, 0.80f);
    colors[ImGuiCol_TabSelected] = nord10;
    colors[ImGuiCol_TabSelectedOverline] = nord8;
    colors[ImGuiCol_TabDimmed] = nord0;
    colors[ImGuiCol_TabDimmedSelected] = nord2;
    colors[ImGuiCol_DockingPreview] = with_alpha(nord8, 0.70f);
    colors[ImGuiCol_PlotLines] = nord8;
    colors[ImGuiCol_PlotLinesHovered] = nord11;
    colors[ImGuiCol_PlotHistogram] = nord14;
    colors[ImGuiCol_PlotHistogramHovered] = nord13;
    colors[ImGuiCol_TableHeaderBg] = nord1;
    colors[ImGuiCol_TableBorderStrong] = nord2;
    colors[ImGuiCol_TableBorderLight] = with_alpha(nord2, 0.50f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = with_alpha(nord1, 0.30f);
    colors[ImGuiCol_TextSelectedBg] = with_alpha(nord10, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = with_alpha(nord0, 0.73f);
    break;
  }

  default:
    ImGui::StyleColorsLight(dst);
    break;
  }
}
