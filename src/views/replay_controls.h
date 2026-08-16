#pragma once

#include <linux/limits.h>

struct ViewState;

struct ReplayViewState {
  int speed_index = 1;
  bool show_open_dialog = false;
  bool launch_request = false;
  bool reset_history_request = false;
  char open_path[PATH_MAX] = {};
  const char *active_path = nullptr;
};

// Flip playback pause/resume and wake the playback thread.
// Also toggle auto-follow.
void replay_toggle_pause(ViewState &view_state);

// Release exactly one more record without resuming. Only meaningful while
// paused; ignored once playback has reached the end of the recording.
void replay_request_step(ViewState &view_state);

// Auto-follow a record applied to a paused replay for a single frame
void replay_follow_record(ViewState &view_state);
void replay_follow_release(ViewState &view_state);

void replay_open_dialog_draw(ViewState &view_state);
void replay_overlay_draw(ViewState &view_state);
