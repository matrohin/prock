#include "app.h"
#include "imgui.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
#include "imgui_te_utils.h"

extern void ui_tests_brief_table_register(ImGuiTestEngine *engine);

int main() {
  AppParams params = {};
  App *app = app_create(params);
  if (!app) {
    return 1;
  }

  ImGuiTestEngine *engine = ImGuiTestEngine_CreateContext();
  ImGuiTestEngineIO &test_io = ImGuiTestEngine_GetIO(engine);
  test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
  test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  ImGui::GetIO().ConfigDebugIsDebuggerPresent = ImOsIsDebuggerPresent();

  ui_tests_brief_table_register(engine);

  ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
  ImGuiTestEngine_InstallDefaultCrashHandler();

  while (!app_should_close(app)) {
    FrameMark;

    glfwPollEvents();
    FrameContext frame_ctx = {};
    FrameMarkStart(MAIN_FRAME);
    app_update(app, frame_ctx);

    app_draw(app, frame_ctx);
    ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
    app_render(app);
    app_end_frame(frame_ctx);

    ImGuiTestEngine_PreSwap(engine);
    app_swap_buffers(app);
    ImGuiTestEngine_PostSwap(engine);
    FrameMarkEnd(MAIN_FRAME);
  }

  ImGuiTestEngine_Stop(engine);
  app_destroy(app);
  ImGuiTestEngine_DestroyContext(engine);
}