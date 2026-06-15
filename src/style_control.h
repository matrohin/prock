#pragma once

#include "themes.h"

#include <chrono>

void style_control_rebuild(int zoom_pct, int opacity_pct);

void style_control_select_theme(Theme theme);

void style_control_init(Theme theme, float monitor_scale, int target_fps);

void style_control_load_fonts(const char *font_path);

void style_control_set_target_fps(int fps);
std::chrono::microseconds style_control_target_framerate();
