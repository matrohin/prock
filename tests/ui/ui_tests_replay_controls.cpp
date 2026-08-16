#include "app.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "implot.h"
#include "implot_internal.h"
#include "ui/ui_test_replay.h"

constexpr const char *SYSTEM_CPU_PLOT = "//System CPU Usage/##SystemCPU";

static double plot_x_max(ImGuiTestContext *context, const char *plot_path) {
  const ImPlotPlot *plot =
      ImPlot::GetCurrentContext()->Plots.GetByKey(context->GetID(plot_path));
  IM_CHECK_SILENT_RETV(plot != nullptr, 0.0);
  return plot->Axes[ImAxis_X1].Range.Max;
}

void ui_tests_replay_controls_register(ImGuiTestEngine *engine) {
  ImGuiTest *t = nullptr;

  t = IM_REGISTER_TEST(engine, "replay_controls",
                       "Stepping pulls the charts to the new record");
  t->TestFunc = [](ImGuiTestContext *context) {
    ui_test_replay_seek(context, UI_TEST_RECORD_SETTLED - 1);
    const PreferencesState &prefs = g_ui_test_app->view_state.preferences_state;

    context->ItemClick(UI_TEST_REPLAY_PLAY);
    IM_CHECK(prefs.auto_follow);
    context->ItemClick(UI_TEST_REPLAY_PAUSE);
    IM_CHECK(!prefs.auto_follow);

    const double before = plot_x_max(context, SYSTEM_CPU_PLOT);
    ui_test_replay_step(context);

    IM_CHECK_GT(plot_x_max(context, SYSTEM_CPU_PLOT), before + 2.0);
    IM_CHECK(!prefs.auto_follow);
  };
}
