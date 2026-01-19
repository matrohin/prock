#pragma once

struct PreferencesState {
  bool dark_mode = false;
  bool show_preferences_modal = false;
};

struct ViewState;

void menu_bar_draw(ViewState &view_state);
