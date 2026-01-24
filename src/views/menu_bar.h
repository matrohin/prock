#pragma once

struct PreferencesState {
  bool dark_mode = false;
  bool show_preferences_modal = false;
  float update_period = 0.5f;  // seconds, 0 = paused
  int target_fps = 60;
  float zoom_scale = 1.0f;  // UI zoom: 0.75 to 2.0
  char font_path[512] = {};  // Custom TTF font path, empty = default
  bool font_needs_reload = false;  // Signal to reload font atlas
};

struct ViewState;

void menu_bar_draw(ViewState &view_state);
