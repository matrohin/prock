#pragma once

#include "themes.h"

struct PreferencesState {
  float update_period = 0.5f;  // seconds, 0 = paused
  int target_fps = 60;
  float zoom_scale = 1.0f;  // UI zoom: 0.75 to 2.0
  char font_path[512] = {};  // Custom TTF font path, empty = default
  Theme theme = Theme::Light;
  bool show_preferences_modal = false;
  bool font_needs_reload = false;  // Signal to reload font atlas
  bool show_debug_fps = false;  // Toggle with F3
  bool cpu_per_core = false;  // CPU % as per-core (can exceed 100%) vs normalized
};

struct ViewState;

void menu_bar_draw(ViewState &view_state);
