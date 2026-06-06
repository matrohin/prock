#define IMGUI_IMPL_OPENGL_ES2

#include "imgui.cpp"
#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"

// FreeType rasterizer (replaces the embedded stb_truetype). Enabled via the
// IMGUI_ENABLE_FREETYPE compile definition on this target, which also makes
// ImGui select the FreeType font loader by default (see imgui_draw.cpp).
#include "misc/freetype/imgui_freetype.cpp"

#include "implot.cpp"

// We only use PlotLine/PlotShaded (+ the getter "G" variants). Compiling out
// the other plotters (Scatter/Bars/PieChart/Heatmap/Histogram/etc.) roughly
// halves implot_items.cpp build time. See the PROCK_IMPLOT_MINIMAL guards in
// that file.
#define PROCK_IMPLOT_MINIMAL
#include "implot_items.cpp"

#include "backends/imgui_impl_glfw.cpp"
#include "backends/imgui_impl_opengl3.cpp"
