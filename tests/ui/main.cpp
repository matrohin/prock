#include "app.h"
#include "ui/ui_test_replay.h"
#include "views/replay_controls.h"

#include "imgui.h"
#include "imgui_te_engine.h"
#include "imgui_te_exporters.h"
#include "imgui_te_ui.h"
#include "imgui_te_utils.h"

#include <cstdio>
#include <cstring>

extern void ui_tests_brief_table_register(ImGuiTestEngine *engine);
extern void ui_tests_replay_controls_register(ImGuiTestEngine *engine);

constexpr const char *USAGE = "Usage: prock_ui_tests [--gui]\n";

int main(int argc, char **argv) {
  bool gui = false;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--gui") == 0) {
      gui = true;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("%s", USAGE);
      return 0;
    } else {
      fprintf(stderr, "Unknown argument: %s\n%s", argv[i], USAGE);
      return 1;
    }
  }

  const char *recording_path = ui_test_replay_build();
  if (!recording_path) {
    return 1;
  }

  AppParams params = {};
  params.hidden_window = !gui;
  params.replay_path = recording_path;
  params.replay_start_paused = true;
  App *app = app_create(params);
  if (!app) {
    ui_test_replay_cleanup();
    return 1;
  }
  g_ui_test_app = app;

  ImGuiTestEngine *engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO &test_io = ImGuiTestEngine_GetIO(engine);
  test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
  test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  test_io.ConfigLogToTTY = !gui;
  ImGui::GetIO().ConfigDebugIsDebuggerPresent = ImOsIsDebuggerPresent();

  ui_tests_brief_table_register(engine);
  ui_tests_replay_controls_register(engine);

  ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
  ImGuiTestEngine_InstallDefaultCrashHandler();

  // Get the baseline record on screen; tests take it from there.
  replay_request_step(app->view_state);

  bool tests_queued = false;

  while (!app_should_close(app)) {
    FrameMark;

    glfwPollEvents();
    FrameContext frame_ctx = {};
    FrameMarkStart(MAIN_FRAME);
    app_update(app, frame_ctx);

    app_draw(app, frame_ctx);
    if (gui) {
      ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
    }
    app_render(app);
    app_end_frame(frame_ctx);

    ImGuiTestEngine_PreSwap(engine);
    app_swap_buffers(app);
    ImGuiTestEngine_PostSwap(engine);
    FrameMarkEnd(MAIN_FRAME);

    if (gui) continue;
    if (!tests_queued) {
      // The tests act on the process table, so they can only start once the
      // playback thread has delivered the record requested above.
      if (app->state.update_count > 0) {
        ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, "all",
                                   ImGuiTestRunFlags_RunFromCommandLine);
        tests_queued = true;
      }
    } else if (ImGuiTestEngine_IsTestQueueEmpty(engine)) {
      break;
    }
  }

  ImGuiTestEngine_Stop(engine);

  ImGuiTestEngineResultSummary summary = {};
  ImGuiTestEngine_GetResultSummary(engine, &summary);
  ImGuiTestEngine_PrintResultSummary(engine);

  app_destroy(app);
  ImGuiTestEngine_DestroyContext(engine);
  ui_test_replay_cleanup();

  return summary.CountSuccess == summary.CountTested ? 0 : 1;
}
