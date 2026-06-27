// Theme implementations adapted from:
// - Enemymouse theme from
// https://gist.github.com/enemymouse/c8aa24e247a1d7b9fc33d45091cbb8f0

#pragma once

#include "imgui.h"

enum class Theme : uint8_t {
  Dark,
  Light,
  Classic,
  Enemymouse,
  Nord,
  Onenord,
  COUNT
};

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
  case Theme::Onenord:
    return "OneNord";
  default:
    return "Unknown";
  }
}

enum AppColor : uint8_t {
  eAppColor_NewProcessRow,  // new-process row highlight (table bg overlay)
  eAppColor_DeadProcessRow, // dead-process row highlight (table bg overlay)
  eAppColor_ErrorText,      // error severity (notifications)
  eAppColor_WarningText,    // caution text (e.g. "requires root") + warnings
  eAppColor_InfoText,       // info severity (notifications)
  eAppColor_COUNT,
};

// App-specific colors, parallel to ImGuiStyle::Colors. Populated per-theme by
// apply_theme(); read from views via app_color_u32(). Not scaled by window
// opacity - these are foreground highlights, not panel backgrounds.
inline ImVec4 g_app_colors[eAppColor_COUNT];

inline ImU32 app_color_u32(const AppColor idx) {
  return ImGui::ColorConvertFloat4ToU32(g_app_colors[idx]);
}

// Shared highlight defaults: the dark set reads on Dark/Classic/Enemymouse, the
// light set on Light. Themes with their own palette (Nord/OneNord) override
// these in apply_theme().
inline void set_app_colors_dark() {
  g_app_colors[eAppColor_NewProcessRow] = ImVec4(0.24f, 0.75f, 0.24f, 0.22f);
  g_app_colors[eAppColor_DeadProcessRow] = ImVec4(0.80f, 0.27f, 0.27f, 0.22f);
  g_app_colors[eAppColor_ErrorText] = ImVec4(0.92f, 0.36f, 0.32f, 1.00f);
  g_app_colors[eAppColor_WarningText] = ImVec4(1.00f, 0.75f, 0.24f, 1.00f);
  g_app_colors[eAppColor_InfoText] = ImVec4(0.40f, 0.66f, 0.96f, 1.00f);
}
inline void set_app_colors_light() {
  g_app_colors[eAppColor_NewProcessRow] = ImVec4(0.16f, 0.63f, 0.16f, 0.28f);
  g_app_colors[eAppColor_DeadProcessRow] = ImVec4(0.78f, 0.22f, 0.22f, 0.28f);
  // Darkened so severity labels stay >=4.5:1 on the light window background.
  g_app_colors[eAppColor_ErrorText] = ImVec4(0.77f, 0.15f, 0.11f, 1.00f);
  g_app_colors[eAppColor_WarningText] = ImVec4(0.67f, 0.35f, 0.00f, 1.00f);
  g_app_colors[eAppColor_InfoText] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
}

inline void apply_theme(const Theme theme, ImGuiStyle *dst = nullptr) {
  ImGuiStyle *style = dst ? dst : &ImGui::GetStyle();
  ImVec4 *colors = style->Colors;

  switch (theme) {
  case Theme::Dark:
    ImGui::StyleColorsDark(dst);
    set_app_colors_dark();
    break;

  case Theme::Light:
    ImGui::StyleColorsLight(dst);
    set_app_colors_light();
    break;

  case Theme::Classic:
    ImGui::StyleColorsClassic(dst);
    set_app_colors_dark();
    break;

  case Theme::Enemymouse: {
    ImGui::StyleColorsDark(dst);
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
    set_app_colors_dark();
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
    colors[ImGuiCol_Text] = nord4;
    // nord3 (the standard "comment" tone) is only ~1.7:1 on nord0 - too faint
    // for grayed rows and disabled text. Use a lighter Snow-Storm-ward gray.
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.68f, 1.00f);
    colors[ImGuiCol_WindowBg] = nord0;
    colors[ImGuiCol_ChildBg] = with_alpha(nord0, 0.00f);
    colors[ImGuiCol_PopupBg] = with_alpha(nord1, 0.95f);
    colors[ImGuiCol_Border] = with_alpha(nord2, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = nord2;
    colors[ImGuiCol_FrameBgHovered] = nord3;
    colors[ImGuiCol_FrameBgActive] = with_alpha(nord3, 0.70f);
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
    g_app_colors[eAppColor_NewProcessRow] = with_alpha(nord14, 0.30f);
    g_app_colors[eAppColor_DeadProcessRow] = with_alpha(nord11, 0.30f);
    g_app_colors[eAppColor_ErrorText] = nord11;
    g_app_colors[eAppColor_WarningText] = nord13;
    g_app_colors[eAppColor_InfoText] = nord8;
    break;
  }

  case Theme::Onenord: {
    // OneNord: Nord structure + One Dark's more saturated accents
    // Polar Night (slightly darker than Nord for more contrast)
    constexpr ImVec4 on0(0.12f, 0.13f, 0.16f, 1.00f); // #1E2127
    constexpr ImVec4 on1(0.16f, 0.17f, 0.20f, 1.00f); // #282C34
    constexpr ImVec4 on2(0.18f, 0.19f, 0.23f, 1.00f); // #2C313A
    constexpr ImVec4 on3(0.24f, 0.27f, 0.32f, 1.00f); // #3E4451
    // Snow Storm (brighter text than Nord)
    constexpr ImVec4 on_fg(0.93f, 0.95f, 0.96f, 1.00f);  // #ECEFF4 (nord6)
    constexpr ImVec4 on_fg2(0.78f, 0.80f, 0.84f, 1.00f); // #C8CCD4
    // Frost (more saturated than Nord)
    constexpr ImVec4 on_cyan(0.34f, 0.71f, 0.76f, 1.00f);  // #56B6C2
    constexpr ImVec4 on_blue(0.38f, 0.69f, 0.94f, 1.00f);  // #61AFEF
    constexpr ImVec4 on_blue2(0.29f, 0.55f, 0.72f, 1.00f); // #4B8DB8
    // Aurora (more saturated than Nord)
    constexpr ImVec4 on_red(0.88f, 0.42f, 0.46f, 1.00f);    // #E06C75
    constexpr ImVec4 on_yellow(0.90f, 0.75f, 0.48f, 1.00f); // #E5C07B
    constexpr ImVec4 on_green(0.60f, 0.76f, 0.47f, 1.00f);  // #98C379

    auto wa = [](ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); };

    ImGui::StyleColorsDark(dst);
    colors[ImGuiCol_Text] = on_fg;
    colors[ImGuiCol_TextDisabled] = wa(on_fg2, 0.55f);
    colors[ImGuiCol_WindowBg] = on0;
    colors[ImGuiCol_ChildBg] = wa(on0, 0.00f);
    colors[ImGuiCol_PopupBg] = wa(on1, 0.97f);
    colors[ImGuiCol_Border] = wa(on3, 0.90f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = on2;
    colors[ImGuiCol_FrameBgHovered] = on3;
    colors[ImGuiCol_FrameBgActive] = wa(on3, 0.70f);
    colors[ImGuiCol_TitleBg] = on0;
    colors[ImGuiCol_TitleBgActive] = on1;
    colors[ImGuiCol_TitleBgCollapsed] = wa(on0, 0.75f);
    colors[ImGuiCol_MenuBarBg] = on1;
    colors[ImGuiCol_ScrollbarBg] = on0;
    colors[ImGuiCol_ScrollbarGrab] = on3;
    colors[ImGuiCol_ScrollbarGrabHovered] = on_blue2;
    colors[ImGuiCol_ScrollbarGrabActive] = on_blue;
    colors[ImGuiCol_CheckMark] = on_cyan;
    colors[ImGuiCol_SliderGrab] = on_blue;
    colors[ImGuiCol_SliderGrabActive] = on_cyan;
    colors[ImGuiCol_Button] = wa(on_blue2, 0.60f);
    colors[ImGuiCol_ButtonHovered] = wa(on_blue, 0.80f);
    colors[ImGuiCol_ButtonActive] = on_cyan;
    colors[ImGuiCol_Header] = wa(on_blue2, 0.45f);
    colors[ImGuiCol_HeaderHovered] = wa(on_blue, 0.60f);
    colors[ImGuiCol_HeaderActive] = wa(on_cyan, 0.80f);
    colors[ImGuiCol_Separator] = wa(on3, 0.80f);
    colors[ImGuiCol_SeparatorHovered] = wa(on_blue, 0.78f);
    colors[ImGuiCol_SeparatorActive] = on_cyan;
    colors[ImGuiCol_ResizeGrip] = wa(on_blue2, 0.40f);
    colors[ImGuiCol_ResizeGripHovered] = wa(on_blue, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = wa(on_cyan, 0.95f);
    colors[ImGuiCol_Tab] = on1;
    colors[ImGuiCol_TabHovered] = wa(on_blue, 0.80f);
    colors[ImGuiCol_TabSelected] = on_blue2;
    colors[ImGuiCol_TabSelectedOverline] = on_cyan;
    colors[ImGuiCol_TabDimmed] = on0;
    colors[ImGuiCol_TabDimmedSelected] = on2;
    colors[ImGuiCol_DockingPreview] = wa(on_cyan, 0.70f);
    colors[ImGuiCol_PlotLines] = on_cyan;
    colors[ImGuiCol_PlotLinesHovered] = on_red;
    colors[ImGuiCol_PlotHistogram] = on_green;
    colors[ImGuiCol_PlotHistogramHovered] = on_yellow;
    colors[ImGuiCol_TableHeaderBg] = on1;
    colors[ImGuiCol_TableBorderStrong] = on3;
    colors[ImGuiCol_TableBorderLight] = wa(on3, 0.60f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = wa(on1, 0.40f);
    colors[ImGuiCol_TextSelectedBg] = wa(on_blue2, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = wa(on0, 0.73f);
    g_app_colors[eAppColor_NewProcessRow] = wa(on_green, 0.30f);
    g_app_colors[eAppColor_DeadProcessRow] = wa(on_red, 0.30f);
    g_app_colors[eAppColor_ErrorText] = on_red;
    g_app_colors[eAppColor_WarningText] = on_yellow;
    g_app_colors[eAppColor_InfoText] = on_blue;
    break;
  }

  default:
    ImGui::StyleColorsLight(dst);
    set_app_colors_light();
    break;
  }
}

// Layout metrics shared by every theme. This is a dense professional tool, so
// spacing stays at ImGui's compact defaults; we only flatten the chrome:
// borderless panels and just-barely-rounded corners. Values are
// DPI-independent; callers scale them by monitor scale and zoom via
// ImGuiStyle::ScaleAllSizes().
inline void apply_geometry(ImGuiStyle &style) {
  // Flat, borderless panels - separation comes from dock splitters and tinted
  // headers, not outlines. A hairline is kept around popups so floating menus
  // stay distinct from the content behind them.
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;

  // Just a hint of rounding - enough to take the hard edge off without the
  // rounded "consumer app" look.
  constexpr float kRounding = 2.0f;
  style.WindowRounding = kRounding;
  style.ChildRounding = kRounding;
  style.FrameRounding = kRounding;
  style.PopupRounding = kRounding;
  style.GrabRounding = kRounding;
  style.TabRounding = kRounding;
  style.ScrollbarRounding = kRounding;

  // Slimmer scrollbars (default is 14)
  style.ScrollbarSize = 11.0f;
}
