#pragma once

#include "base/base.h"

struct App;
struct ImGuiTestContext;

//   record 0 (t=0ms)    : the baseline set below
//   record 1 (t=500ms)  : same set, counters advanced
//   record 2 (t=1000ms) : UI_TEST_PID_BORN appears, UI_TEST_PID_DYING is gone
//   record 3 (t=1500ms) : same set, both still inside their 2s display windows
//   record 4 (t=4000ms) : both windows lapsed
constexpr int UI_TEST_RECORD_COUNT = 5;
constexpr int UI_TEST_RECORD_LIFECYCLE = 2;
constexpr int UI_TEST_RECORD_SETTLED = 4;

constexpr Pid UI_TEST_PID_ROOT = 1;    // parent of most of the tree
constexpr Pid UI_TEST_PID_BORN = 600;  // absent until UI_TEST_RECORD_LIFECYCLE
constexpr Pid UI_TEST_PID_DYING = 400; // present until UI_TEST_RECORD_LIFECYCLE

constexpr const char *UI_TEST_REPLAY_PLAY = "//##ReplayOverlay/Play";
constexpr const char *UI_TEST_REPLAY_PAUSE = "//##ReplayOverlay/Pause";
constexpr const char *UI_TEST_REPLAY_STEP = "//##ReplayOverlay/Step";

extern App *g_ui_test_app;

const char *ui_test_replay_build();
void ui_test_replay_cleanup();
void ui_test_replay_step(ImGuiTestContext *ctx);
void ui_test_replay_restart(ImGuiTestContext *ctx);
void ui_test_replay_seek(ImGuiTestContext *ctx, int record_index);
