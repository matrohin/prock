#include "ui/ui_test_replay.h"

#include "app.h"
#include "common/fake_recording.h"
#include "common/test_helpers.h"
#include "playback/player.h"

#include "imgui_te_context.h"

#include <cstdio>
#include <cstdlib>
#include <linux/limits.h>
#include <unistd.h>

App *g_ui_test_app = nullptr;

constexpr int64_t MS = 1000 * 1000;
constexpr uint64_t TICKS_IN_SECOND = 100;
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t BOOT_TIME_EPOCH_SEC = 1700000000;
constexpr uint32_t NUM_CORES = 4;

// Frames a single record may take to travel from the playback thread.
constexpr int STEP_FRAME_BUDGET = 200;

static char g_recording_path[PATH_MAX];

static void add_process(FakeRecording &rec, const Pid pid, const Pid ppid,
                        const char *comm, const uint64_t rss_pages) {
  ProcessStat stat = make_process_stat(rec.arena, pid, ppid, comm);
  stat.cmdline = comm;
  stat.num_threads = 1;
  stat.statm_resident = rss_pages;
  stat.vsize = rss_pages * PAGE_SIZE * 4;
  rec.add(stat);
}

static void build_records(FakeRecording &rec) {
  add_process(rec, UI_TEST_PID_ROOT, 0, "init", 500);
  add_process(rec, 100, UI_TEST_PID_ROOT, "systemd-journald", 2000);
  add_process(rec, 200, UI_TEST_PID_ROOT, "sshd", 900);
  add_process(rec, UI_TEST_PID_SHELL, 200, "bash", 700);
  add_process(rec, UI_TEST_PID_DYING, UI_TEST_PID_SHELL, UI_TEST_NAME_DYING,
              1500);
  add_process(rec, UI_TEST_PID_BROWSER, UI_TEST_PID_ROOT, "firefox", 60000);
  rec.record(0);

  rec.advance(5, 2, 4096, 1024);
  rec.record(500 * MS);

  rec.advance(5, 2, 4096, 1024);
  rec.remove(UI_TEST_PID_DYING);
  add_process(rec, UI_TEST_PID_BORN, 300, "cc1plus", 12000);
  rec.record(1000 * MS);

  rec.advance(5, 2, 4096, 1024);
  rec.record(1500 * MS);

  rec.advance(25, 10, 20480, 5120);
  rec.record(4000 * MS);
}

static Sync &app_sync() { return g_ui_test_app->sync; }

const char *ui_test_replay_build() {
  const char *tmp_dir = getenv("TMPDIR");
  if (!tmp_dir || tmp_dir[0] != '/') tmp_dir = "/tmp";
  snprintf(g_recording_path, sizeof(g_recording_path),
           "%s/prock_ui_tests_XXXXXX.prck", tmp_dir);

  const int fd = mkstemps(g_recording_path, 5);
  if (fd < 0) {
    perror("mkstemps");
    g_recording_path[0] = '\0';
    return nullptr;
  }
  close(fd);

  FakeRecording rec = FakeRecording::create(
      {TICKS_IN_SECOND, PAGE_SIZE, BOOT_TIME_EPOCH_SEC}, NUM_CORES);
  build_records(rec);
  const bool ok = rec.write(g_recording_path);
  rec.destroy();

  if (!ok) {
    fprintf(stderr, "Failed to write %s\n", g_recording_path);
    ui_test_replay_cleanup();
    return nullptr;
  }
  return g_recording_path;
}

void ui_test_replay_cleanup() {
  if (g_recording_path[0] == '\0') return;
  unlink(g_recording_path);
  g_recording_path[0] = '\0';
}

void ui_test_replay_step(ImGuiTestContext *ctx) {
  const uint64_t before = g_ui_test_app->state.update_count;
  ctx->ItemClick(UI_TEST_REPLAY_STEP);

  for (int i = 0; i < STEP_FRAME_BUDGET; ++i) {
    if (g_ui_test_app->state.update_count != before) {
      ctx->Yield(UI_TEST_SETTLE_FRAMES);
      return;
    }
    ctx->Yield();
  }
  IM_CHECK_SILENT(g_ui_test_app->state.update_count != before);
}

// Not the Restart button: it resumes
void ui_test_replay_restart(ImGuiTestContext *ctx) {
  app_sync().playback.restart.store(true);
  // Same history reset the Restart button performs
  g_ui_test_app->view_state.replay_state.reset_history_request = true;
  playback_notify(app_sync());

  for (int i = 0; i < STEP_FRAME_BUDGET; ++i) {
    ctx->Yield();
    if (!app_sync().playback.restart.load()) return;
  }
  IM_CHECK_SILENT(!app_sync().playback.restart.load());
}

void ui_test_replay_seek(ImGuiTestContext *ctx, const int record_index) {
  IM_CHECK_SILENT(record_index >= 0 && record_index < UI_TEST_RECORD_COUNT);
  ui_test_replay_restart(ctx);
  for (int i = 0; i <= record_index; ++i) {
    ui_test_replay_step(ctx);
  }
}
