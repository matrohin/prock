#pragma once

#include "sources/font_list_reader.h"
#include "themes.h"

struct PreferencesState {
  float update_period = 0.5f; // seconds, 0 = paused
  int target_fps = 60;
  int zoom_scale_pct = 100;
  int window_opacity_pct = 100;
  char font_path[512] = {};    // Custom TTF font path, empty = default
  Theme theme = Theme::Light;
  bool show_preferences_modal = false;
  bool show_about_modal = false;
  bool show_licenses_modal = false;
  bool font_needs_reload = false; // Signal to reload font atlas
  bool show_debug_fps = false;    // Toggle with F3
  bool show_menu_on_alt = false;
  bool cpu_per_core =
      false; // CPU % as per-core (can exceed 100%) vs normalized
  bool auto_follow = true;
  bool font_list_requested = false;
  bool font_list_received = false;
  bool font_scroll_to_selected = false;
  bool prev_show_preferences = false;
  BumpArena font_list_arena;
  Array<FontEntry> font_list;
  char font_filter[128] = {};
};

struct ViewState;

void menu_bar_update(ViewState &view_state);
void menu_bar_draw(ViewState &view_state);
