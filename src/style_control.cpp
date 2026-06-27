#include "style_control.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#include "inter_font.h"
#include "material_symbols_font.h"
#pragma GCC diagnostic pop

#include "constants.h"
#include "imgui.h"
#include "implot.h"
#include "misc/freetype/imgui_freetype.h"
#include "themes.h"
#include "views/icons.h"

static Theme g_applied_theme = Theme::COUNT;
static float g_monitor_scale = 1.0f;
static int g_applied_opacity = 0;
static int g_applied_zoom_scale = 0;
static ImGuiStyle g_base_style;

static std::chrono::microseconds g_target_rate;

static void apply_window_opacity(ImGuiStyle &style, const float opacity) {
  if (opacity >= 1.0f) {
    return;
  }
  static constexpr ImGuiCol bg_colors[] = {
      ImGuiCol_WindowBg,         ImGuiCol_ChildBg,
      ImGuiCol_MenuBarBg,        ImGuiCol_PopupBg,
      ImGuiCol_TitleBg,          ImGuiCol_TitleBgActive,
      ImGuiCol_TitleBgCollapsed, ImGuiCol_ScrollbarBg,
      ImGuiCol_TableHeaderBg,    ImGuiCol_TableRowBg,
      ImGuiCol_TableRowBgAlt,    ImGuiCol_Tab,
      ImGuiCol_TabHovered,       ImGuiCol_TabSelected,
      ImGuiCol_TabDimmed,        ImGuiCol_TabDimmedSelected,
      ImGuiCol_FrameBg,          ImGuiCol_FrameBgHovered,
      ImGuiCol_FrameBgActive,    ImGuiCol_Button,
      ImGuiCol_ButtonHovered,    ImGuiCol_ButtonActive,
  };
  for (const ImGuiCol col : bg_colors) {
    style.Colors[col].w *= opacity;
  }
}

static void apply_plot_opacity(const float opacity) {
  ImPlotStyle &plot_style = ImPlot::GetStyle();
  plot_style.Colors[ImPlotCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  if (opacity >= 1.0f) {
    plot_style.Colors[ImPlotCol_PlotBg] = IMPLOT_AUTO_COL;
    return;
  }
  ImVec4 plot = g_base_style.Colors[ImGuiCol_WindowBg];
  plot.w *= opacity;
  plot_style.Colors[ImPlotCol_PlotBg] = plot;
}

void style_control_rebuild(const int zoom_pct, const int opacity_pct) {
  if (g_applied_zoom_scale == zoom_pct && g_applied_opacity == opacity_pct)
    return;

  ImGuiStyle &live = ImGui::GetStyle();
  ImGuiIO &io = ImGui::GetIO();

  const float zoom = zoom_pct / 100.0f;
  const float opacity = opacity_pct / 100.0f;

  live = g_base_style;
  live.ScaleAllSizes(g_monitor_scale * zoom);
  live.WindowRounding = 0.0f;
  apply_window_opacity(live, opacity);
  apply_plot_opacity(opacity);

  io.FontGlobalScale = zoom;
  g_applied_zoom_scale = zoom_pct;
  g_applied_opacity = opacity_pct;
}

// Chart colormap registry indices. The palette data lives in themes.h, but
// registering it returns an ImPlot index and needs the live ImPlot context
// (created by main() at runtime), so these are filled in by
// register_chart_colormaps(), not eagerly.
static ImPlotColormap g_nord_colormap;
static ImPlotColormap g_onenord_colormap;

static void register_chart_colormaps() {
  g_nord_colormap =
      ImPlot::AddColormap("Nord", kNordColormap, IM_ARRAYSIZE(kNordColormap));
  g_onenord_colormap = ImPlot::AddColormap("OneNord", kOneNordColormap,
                                           IM_ARRAYSIZE(kOneNordColormap));
}

static void apply_chart_colormap(const Theme theme) {
  ImPlotColormap cmap = ImPlotColormap_Deep;
  if (theme == Theme::Nord)
    cmap = g_nord_colormap;
  else if (theme == Theme::Onenord)
    cmap = g_onenord_colormap;

  ImPlot::GetStyle().Colormap = cmap;
  // Series colors are cached per item on first draw; drop the cache so any open
  // plots re-pick from the new colormap after a live theme switch.
  ImPlot::BustColorCache();
}

void style_control_select_theme(const Theme theme) {
  if (theme == g_applied_theme) return;

  apply_theme(theme, &g_base_style);
  apply_geometry(g_base_style);
  apply_chart_colormap(theme);

  g_applied_theme = theme;
  // The live style is only refreshed from g_base_style by
  // style_control_rebuild, which short-circuits on unchanged zoom/opacity.
  // Invalidate that gate so the next rebuild copies the new theme colors
  // through even when scale is unchanged.
  g_applied_zoom_scale = -1;
}

void style_control_init(const Theme theme, const float monitor_scale,
                        const int target_fps) {
  register_chart_colormaps();
  style_control_select_theme(theme);
  style_control_set_target_fps(target_fps);
  g_base_style.AntiAliasedLines = true;
  g_base_style.AntiAliasedLinesUseTex = true;
  g_base_style.AntiAliasedFill = true;

  g_monitor_scale = monitor_scale;

  ImPlotStyle &implot_style = ImPlot::GetStyle();
  implot_style.UseLocalTime = true;
  implot_style.UseISO8601 = true;
}

// Merge the embedded Material Symbols glyphs onto the last-added base font so
// context menus can prefix items with icons. GlyphMinAdvanceX makes every icon
// occupy a fixed gutter, which keeps menu labels aligned (see MenuItemEx).
static void merge_icon_font(ImGuiIO &io, const float scale) {
  ImFontConfig cfg;
  cfg.MergeMode = true;
  cfg.FontDataOwnedByAtlas = false;
  cfg.GlyphMinAdvanceX = BASE_FONT_SIZE * scale;
  // Merged fonts inherit the base font's baseline, but Material Symbols fill
  // the whole em above it, so they ride ~1px high. Nudge them down; ImGui snaps
  // and rescales this offset per bake, so it stays centered across zoom levels.
  cfg.GlyphOffset.y = BASE_FONT_SIZE * scale / 13.0f;
  static constexpr ImWchar range[] = {ICON_MIN_MD, ICON_MAX_MD, 0};
  io.Fonts->AddFontFromMemoryTTF(
      const_cast<unsigned char *>(material_symbols_ttf),
      material_symbols_ttf_size, BASE_FONT_SIZE * scale, &cfg, range);
}

// The bundled default UI font: Inter, embedded compressed (see inter_font.h).
static ImFont *add_inter(ImGuiIO &io, const float size,
                         const ImFontConfig *cfg) {
  return io.Fonts->AddFontFromMemoryCompressedBase85TTF(
      inter_compressed_data_base85, size, cfg);
}

void style_control_load_fonts(const char *font_path) {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  // FreeType light hinting: snap glyphs to the pixel grid vertically only (like
  // ClearType), keeping horizontal spacing intact. Crisper small UI text than
  // the unhinted stb_truetype default, without the chunkiness of full hinting.
  io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

  static constexpr ImWchar icon_exclude[] = {ICON_MIN_MD, ICON_MAX_MD, 0};
  ImFontConfig cfg;
  cfg.GlyphExcludeRanges = icon_exclude;

  const float size = BASE_FONT_SIZE * g_monitor_scale;
  if (font_path && font_path[0] != '\0') {
    if (!io.Fonts->AddFontFromFileTTF(font_path, size, &cfg)) {
      add_inter(io, size, &cfg);
    }
  } else {
    add_inter(io, size, &cfg);
  }
  merge_icon_font(io, g_monitor_scale);
}

void style_control_set_target_fps(const int fps) {
  g_target_rate = std::chrono::microseconds(1'000'000 / fps);
}

std::chrono::microseconds style_control_target_framerate() {
  return g_target_rate;
}
