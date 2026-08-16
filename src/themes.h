// Theme implementations adapted from:
// - Enemymouse theme from
//   https://gist.github.com/enemymouse/c8aa24e247a1d7b9fc33d45091cbb8f0
// - Nord (MIT, (c) Sven Greb) https://github.com/nordtheme/nord
// - OneNord blends Nord with Atom One Dark (MIT, (c) GitHub Inc.)
// - Everforest (MIT, (c) sainnhe) https://github.com/sainnhe/everforest
// The palettes below are color values; the full upstream license texts ship in
// the in-app "Licenses" dialog (scripts/gen_licenses.py ->
// src/views/licenses.h).

#pragma once

#include "imgui.h"

enum class Theme : uint8_t {
  Dark,
  Light,
  Classic,
  Enemymouse,
  Nord,
  Onenord,
  EverforestDark,
  EverforestLight,
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
  case Theme::EverforestDark:
    return "Everforest Dark";
  case Theme::EverforestLight:
    return "Everforest Light";
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

// Per-theme chart series palettes. ImPlot picks series colors from its active
// colormap (not ImGuiCol_PlotLines), so style_control.cpp registers these and
// selects one per theme; themes without an entry fall back to ImPlot's "Deep".
//
// Nord accents (Frost + Aurora), ordered so the leading series read clearly.
// The tail entries only come into play in the per-core CPU chart, where ImPlot
// cycles through the colormap (mod its length) once there are more cores than
// colors. 10 distinct hues matches ImPlot's default "Deep" map.
inline constexpr ImVec4 kNordColormap[] = {
    ImVec4(0.53f, 0.75f, 0.82f, 1.0f), // nord8  frost cyan
    ImVec4(0.64f, 0.75f, 0.55f, 1.0f), // nord14 green
    ImVec4(0.92f, 0.80f, 0.55f, 1.0f), // nord13 yellow
    ImVec4(0.71f, 0.56f, 0.68f, 1.0f), // nord15 purple
    ImVec4(0.75f, 0.38f, 0.42f, 1.0f), // nord11 red
    ImVec4(0.37f, 0.51f, 0.67f, 1.0f), // nord10 blue
    ImVec4(0.82f, 0.53f, 0.44f, 1.0f), // nord12 orange
    ImVec4(0.56f, 0.74f, 0.73f, 1.0f), // nord7  teal
    ImVec4(0.51f, 0.63f, 0.76f, 1.0f), // nord9  light blue
    ImVec4(0.85f, 0.87f, 0.91f, 1.0f), // nord4  light gray
};
// OneNord: the same structure with One Dark's more saturated syntax accents.
// Its palette tops out shorter than Nord's, so it wraps a touch sooner.
inline constexpr ImVec4 kOneNordColormap[] = {
    ImVec4(0.38f, 0.69f, 0.94f, 1.0f), // blue
    ImVec4(0.60f, 0.76f, 0.47f, 1.0f), // green
    ImVec4(0.90f, 0.75f, 0.48f, 1.0f), // yellow
    ImVec4(0.78f, 0.47f, 0.87f, 1.0f), // magenta
    ImVec4(0.88f, 0.42f, 0.46f, 1.0f), // red
    ImVec4(0.34f, 0.71f, 0.76f, 1.0f), // cyan
    ImVec4(0.82f, 0.60f, 0.40f, 1.0f), // orange
    ImVec4(0.67f, 0.70f, 0.75f, 1.0f), // gray
    ImVec4(0.29f, 0.55f, 0.72f, 1.0f), // deep blue
};
// Everforest dark accents (pastel, tuned for the dark background).
inline constexpr ImVec4 kEverforestDarkColormap[] = {
    ImVec4(0.498f, 0.733f, 0.702f, 1.0f), // blue   #7fbbb3
    ImVec4(0.655f, 0.753f, 0.502f, 1.0f), // green  #a7c080
    ImVec4(0.859f, 0.737f, 0.498f, 1.0f), // yellow #dbbc7f
    ImVec4(0.902f, 0.596f, 0.459f, 1.0f), // orange #e69875
    ImVec4(0.902f, 0.494f, 0.502f, 1.0f), // red    #e67e80
    ImVec4(0.839f, 0.600f, 0.714f, 1.0f), // purple #d699b6
    ImVec4(0.514f, 0.753f, 0.573f, 1.0f), // aqua   #83c092
};
// Everforest light accents (deeper, so series read on the cream background).
inline constexpr ImVec4 kEverforestLightColormap[] = {
    ImVec4(0.227f, 0.580f, 0.773f, 1.0f), // blue   #3a94c5
    ImVec4(0.553f, 0.631f, 0.004f, 1.0f), // green  #8da101
    ImVec4(0.875f, 0.627f, 0.000f, 1.0f), // yellow #dfa000
    ImVec4(0.961f, 0.490f, 0.149f, 1.0f), // orange #f57d26
    ImVec4(0.973f, 0.333f, 0.322f, 1.0f), // red    #f85552
    ImVec4(0.875f, 0.412f, 0.729f, 1.0f), // purple #df69ba
    ImVec4(0.208f, 0.655f, 0.486f, 1.0f), // aqua   #35a77c
};

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

    auto with_alpha = [](const ImVec4 c, const float a) {
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
    // Docked tab bars fill the strip behind the tabs with TitleBg /
    // TitleBgActive, so tab colors sit one shade above those to stay visible.
    colors[ImGuiCol_Tab] = nord2;
    colors[ImGuiCol_TabHovered] = with_alpha(nord9, 0.80f);
    colors[ImGuiCol_TabSelected] = nord10;
    colors[ImGuiCol_TabSelectedOverline] = nord8;
    colors[ImGuiCol_TabDimmed] = nord1;
    colors[ImGuiCol_TabDimmedSelected] = nord3;
    colors[ImGuiCol_DockingPreview] = with_alpha(nord8, 0.70f);
    // Chart series colors come from the ImPlot colormap (see
    // style_control.cpp), not ImGuiCol_PlotLines, so those are intentionally
    // left at defaults.
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

    auto wa = [](const ImVec4 c, const float a) {
      return ImVec4(c.x, c.y, c.z, a);
    };

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
    colors[ImGuiCol_Tab] = on3;
    colors[ImGuiCol_TabHovered] = wa(on_blue, 0.80f);
    colors[ImGuiCol_TabSelected] = on_blue2;
    colors[ImGuiCol_TabSelectedOverline] = on_cyan;
    colors[ImGuiCol_TabDimmed] = on2;
    colors[ImGuiCol_TabDimmedSelected] = on3;
    colors[ImGuiCol_DockingPreview] = wa(on_cyan, 0.70f);
    // Chart series colors come from the ImPlot colormap (see
    // style_control.cpp), not ImGuiCol_PlotLines, so those are intentionally
    // left at defaults.
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

  case Theme::EverforestDark: {
    // Everforest (sainnhe), dark medium. Soft, low-glare forest palette.
    constexpr ImVec4 bg0(0.176f, 0.208f, 0.231f, 1.00f);    // #2d353b
    constexpr ImVec4 bg1(0.204f, 0.247f, 0.267f, 1.00f);    // #343f44
    constexpr ImVec4 bg2(0.239f, 0.282f, 0.302f, 1.00f);    // #3d484d
    constexpr ImVec4 bg3(0.278f, 0.322f, 0.345f, 1.00f);    // #475258
    constexpr ImVec4 fg(0.827f, 0.776f, 0.667f, 1.00f);     // #d3c6aa
    constexpr ImVec4 grey(0.522f, 0.573f, 0.537f, 1.00f);   // #859289
    constexpr ImVec4 red(0.902f, 0.494f, 0.502f, 1.00f);    // #e67e80
    constexpr ImVec4 yellow(0.859f, 0.737f, 0.498f, 1.00f); // #dbbc7f
    constexpr ImVec4 green(0.655f, 0.753f, 0.502f, 1.00f);  // #a7c080
    constexpr ImVec4 aqua(0.514f, 0.753f, 0.573f, 1.00f);   // #83c092
    constexpr ImVec4 blue(0.498f, 0.733f, 0.702f, 1.00f);   // #7fbbb3

    auto wa = [](const ImVec4 c, const float a) {
      return ImVec4(c.x, c.y, c.z, a);
    };

    ImGui::StyleColorsDark(dst);
    colors[ImGuiCol_Text] = fg;
    colors[ImGuiCol_TextDisabled] = grey;
    colors[ImGuiCol_WindowBg] = bg0;
    colors[ImGuiCol_ChildBg] = wa(bg0, 0.00f);
    colors[ImGuiCol_PopupBg] = wa(bg1, 0.97f);
    colors[ImGuiCol_Border] = wa(bg3, 0.90f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = bg2;
    colors[ImGuiCol_FrameBgHovered] = bg3;
    colors[ImGuiCol_FrameBgActive] = wa(bg3, 0.70f);
    colors[ImGuiCol_TitleBg] = bg0;
    colors[ImGuiCol_TitleBgActive] = bg1;
    colors[ImGuiCol_TitleBgCollapsed] = wa(bg0, 0.75f);
    colors[ImGuiCol_MenuBarBg] = bg1;
    colors[ImGuiCol_ScrollbarBg] = bg0;
    colors[ImGuiCol_ScrollbarGrab] = bg3;
    colors[ImGuiCol_ScrollbarGrabHovered] = wa(green, 0.60f);
    colors[ImGuiCol_ScrollbarGrabActive] = green;
    colors[ImGuiCol_CheckMark] = green;
    colors[ImGuiCol_SliderGrab] = aqua;
    colors[ImGuiCol_SliderGrabActive] = green;
    colors[ImGuiCol_Button] = wa(green, 0.45f);
    colors[ImGuiCol_ButtonHovered] = wa(green, 0.65f);
    colors[ImGuiCol_ButtonActive] = wa(aqua, 0.85f);
    colors[ImGuiCol_Header] = wa(green, 0.35f);
    colors[ImGuiCol_HeaderHovered] = wa(green, 0.50f);
    colors[ImGuiCol_HeaderActive] = wa(aqua, 0.70f);
    colors[ImGuiCol_Separator] = wa(bg3, 0.80f);
    colors[ImGuiCol_SeparatorHovered] = wa(green, 0.78f);
    colors[ImGuiCol_SeparatorActive] = aqua;
    colors[ImGuiCol_ResizeGrip] = wa(green, 0.40f);
    colors[ImGuiCol_ResizeGripHovered] = wa(green, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = wa(aqua, 0.95f);
    colors[ImGuiCol_Tab] = bg2;
    colors[ImGuiCol_TabHovered] = wa(green, 0.70f);
    colors[ImGuiCol_TabSelected] = wa(green, 0.55f);
    colors[ImGuiCol_TabSelectedOverline] = aqua;
    colors[ImGuiCol_TabDimmed] = bg1;
    colors[ImGuiCol_TabDimmedSelected] = bg2;
    colors[ImGuiCol_DockingPreview] = wa(aqua, 0.70f);
    colors[ImGuiCol_TableHeaderBg] = bg1;
    colors[ImGuiCol_TableBorderStrong] = bg3;
    colors[ImGuiCol_TableBorderLight] = wa(bg3, 0.60f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = wa(bg1, 0.40f);
    colors[ImGuiCol_TextSelectedBg] = wa(green, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = wa(bg0, 0.73f);
    g_app_colors[eAppColor_NewProcessRow] = wa(green, 0.30f);
    g_app_colors[eAppColor_DeadProcessRow] = wa(red, 0.30f);
    g_app_colors[eAppColor_ErrorText] = red;
    g_app_colors[eAppColor_WarningText] = yellow;
    g_app_colors[eAppColor_InfoText] = blue;
    break;
  }

  case Theme::EverforestLight: {
    // Everforest light medium. Deeper accents than the dark variant so they
    // read on the cream background.
    constexpr ImVec4 bg0(0.992f, 0.965f, 0.890f, 1.00f);   // #fdf6e3
    constexpr ImVec4 bg1(0.957f, 0.941f, 0.851f, 1.00f);   // #f4f0d9
    constexpr ImVec4 bg2(0.937f, 0.922f, 0.831f, 1.00f);   // #efebd4
    constexpr ImVec4 bg3(0.902f, 0.886f, 0.800f, 1.00f);   // #e6e2cc
    constexpr ImVec4 fg(0.361f, 0.416f, 0.447f, 1.00f);    // #5c6a72
    constexpr ImVec4 grey(0.576f, 0.624f, 0.569f, 1.00f);  // #939f91
    constexpr ImVec4 red(0.973f, 0.333f, 0.322f, 1.00f);   // #f85552
    constexpr ImVec4 green(0.553f, 0.631f, 0.004f, 1.00f); // #8da101

    auto wa = [](const ImVec4 c, const float a) {
      return ImVec4(c.x, c.y, c.z, a);
    };

    ImGui::StyleColorsLight(dst);
    colors[ImGuiCol_Text] = fg;
    colors[ImGuiCol_TextDisabled] = grey;
    colors[ImGuiCol_WindowBg] = bg0;
    colors[ImGuiCol_ChildBg] = wa(bg0, 0.00f);
    colors[ImGuiCol_PopupBg] = wa(bg1, 0.98f);
    colors[ImGuiCol_Border] = wa(bg3, 0.90f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = bg2;
    colors[ImGuiCol_FrameBgHovered] = bg3;
    colors[ImGuiCol_FrameBgActive] = wa(bg3, 0.70f);
    colors[ImGuiCol_TitleBg] = bg2;
    colors[ImGuiCol_TitleBgActive] = bg3;
    colors[ImGuiCol_TitleBgCollapsed] = wa(bg2, 0.75f);
    colors[ImGuiCol_MenuBarBg] = bg1;
    colors[ImGuiCol_ScrollbarBg] = bg1;
    colors[ImGuiCol_ScrollbarGrab] = bg3;
    colors[ImGuiCol_ScrollbarGrabHovered] = wa(green, 0.60f);
    colors[ImGuiCol_ScrollbarGrabActive] = wa(green, 0.85f);
    colors[ImGuiCol_CheckMark] = green;
    colors[ImGuiCol_SliderGrab] = wa(green, 0.75f);
    colors[ImGuiCol_SliderGrabActive] = green;
    colors[ImGuiCol_Button] = wa(green, 0.35f);
    colors[ImGuiCol_ButtonHovered] = wa(green, 0.55f);
    colors[ImGuiCol_ButtonActive] = wa(green, 0.75f);
    colors[ImGuiCol_Header] = wa(green, 0.30f);
    colors[ImGuiCol_HeaderHovered] = wa(green, 0.45f);
    colors[ImGuiCol_HeaderActive] = wa(green, 0.65f);
    colors[ImGuiCol_Separator] = wa(bg3, 0.90f);
    colors[ImGuiCol_SeparatorHovered] = wa(green, 0.60f);
    colors[ImGuiCol_SeparatorActive] = green;
    colors[ImGuiCol_ResizeGrip] = wa(green, 0.40f);
    colors[ImGuiCol_ResizeGripHovered] = wa(green, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = wa(green, 0.75f);
    colors[ImGuiCol_Tab] = bg2;
    colors[ImGuiCol_TabHovered] = wa(green, 0.45f);
    colors[ImGuiCol_TabSelected] = bg0;
    colors[ImGuiCol_TabSelectedOverline] = green;
    colors[ImGuiCol_TabDimmed] = wa(bg1, 0.60f);
    colors[ImGuiCol_TabDimmedSelected] = bg3;
    colors[ImGuiCol_TabDimmedSelectedOverline] = wa(green, 0.55f);
    colors[ImGuiCol_DockingPreview] = wa(green, 0.70f);
    colors[ImGuiCol_TableHeaderBg] = bg2;
    colors[ImGuiCol_TableBorderStrong] = bg3;
    colors[ImGuiCol_TableBorderLight] = wa(bg3, 0.60f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = wa(bg2, 0.55f);
    colors[ImGuiCol_TextSelectedBg] = wa(green, 0.30f);
    colors[ImGuiCol_ModalWindowDimBg] = wa(grey, 0.40f);
    // Row tints from the palette; severity text keeps the validated
    // high-contrast light values (see set_app_colors_light).
    set_app_colors_light();
    g_app_colors[eAppColor_NewProcessRow] = wa(green, 0.30f);
    g_app_colors[eAppColor_DeadProcessRow] = wa(red, 0.30f);
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
