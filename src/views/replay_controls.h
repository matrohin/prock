#pragma once

#include <linux/limits.h>

struct ViewState;
struct Sync;

struct ReplayViewState {
  int speed_index = 1;
  bool show_open_dialog = false;
  bool launch_request = false;
  bool reset_history_request = false;
  char open_path[PATH_MAX] = {};
  const char *active_path = nullptr;
};

// Flip playback pause/resume and wake the playback thread. Shared by the
// overlay button and the Space shortcut.
void replay_toggle_pause(Sync &sync);

void replay_open_dialog_draw(ViewState &view_state);
void replay_overlay_draw(ViewState &view_state);
