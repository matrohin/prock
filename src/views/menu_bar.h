#pragma once

#include "base/containers.h"
#include "readers/font_list_reader.h"
#include "themes.h"

struct PreferencesState {
  float update_period = 0.5f; // seconds, 0 = paused
  int target_fps = 60;
  int zoom_scale_pct = 100;
  int window_opacity_pct = 100;
  char font_path[512] = {};      // Custom UI TTF path, empty = default
  char mono_font_path[512] = {}; // Custom monospace TTF path, empty = default
  char dump_dir[512] = {};       // Core dump folder, empty = ~/prock-dumps
  Theme theme = Theme::Light;
  bool show_preferences_modal = false;
  bool show_about_modal = false;
  bool show_licenses_modal = false;
  bool font_needs_reload = false; // Signal to reload font atlas
  bool show_debug_fps = false;    // Debug-only FPS overlay; flip in code
  bool show_menu_on_alt = false;
  bool cpu_per_core =
      false; // CPU % as per-core (can exceed 100%) vs normalized
  bool auto_follow = true;
  bool y_auto_fit = true; // continuously auto-fit chart Y axes to data
  bool font_list_requested = false;
  bool font_list_received = false;
  bool prev_show_preferences = false;
  BumpArena font_list_arena;
  Array<FontEntry> font_list;
  // Shared by the font picker popups; reset when a picker opens.
  char font_filter[128] = {};
};

struct ViewState;

void menu_bar_update(ViewState &view_state);
void menu_bar_draw(ViewState &view_state);
