#include "actions/elevate.h"
#include "app.h"
#include "base/base.h"
#include "base/channel.h"
#include "constants.h"
#include "paths.h"
#include "readers/process_stat.h"
#include "state/state.h"
#include "sync.h"
#include "views/icons.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "implot.h"
#include "misc/freetype/imgui_freetype.h"

#include <GLES2/gl2.h>
#include <GLFW/glfw3.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// Home directory of the user who launched an elevated session (sudo or pkexec)
static const char *invoking_user_home() {
  uid_t uid = 0;
  if (!invoking_user_uid(uid)) return nullptr;
  const passwd *pw = getpwuid(uid);
  return pw ? pw->pw_dir : nullptr;
}

constexpr const char *USAGE =
    "Usage: prock [--replay <file.prck>] [--display-env NAME=VALUE]\n";

int main(int argc, char **argv) {
  const char *replay_path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
      replay_path = argv[++i];
    } else if (strcmp(argv[i], "--display-env") == 0 && i + 1 < argc) {
      if (!apply_display_env(argv[++i])) {
        fprintf(stderr, "Cannot apply --display-env %s\n%s", argv[i], USAGE);
        return 1;
      }
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("%s", USAGE);
      return 0;
    } else {
      fprintf(stderr, "Unknown argument: %s\n%s", argv[i], USAGE);
      return 1;
    }
  }

  AppParams params = {};
  params.replay_path = replay_path;

  // Set up config path in $HOME/.config/prock/, or in the home of whoever
  // launched an elevated session
  static char ini_path[PATH_MAX] = {};
  const char *invoker_home = geteuid() == 0 ? invoking_user_home() : nullptr;
  params.is_config_borrowed = invoker_home != nullptr;
  const char *home = invoker_home ? invoker_home : getenv("HOME");
  if (home) {
    const int n = snprintf(ini_path, sizeof(ini_path),
                           "%s/.config/prock/settings.ini", home);
    if (n > 0 && static_cast<size_t>(n) < sizeof(ini_path)) {
      params.config_path = ini_path;
      if (!params.is_config_borrowed) paths_ensure_parent_dir(ini_path);
    }
  }

  App *app = app_create(params);
  if (!app) {
    return 1;
  }

  while (!app_should_close(app)) {
    FrameMark;

    FrameContext frame_ctx = app_start_frame(app);
    FrameMarkStart(MAIN_FRAME);
    auto frame_start = SteadyClock::now();
    app_update(app, frame_ctx);
    app_draw(app, frame_ctx);
    app_render(app);
    app_end_frame(frame_ctx);

    app_swap_buffers(app);
    FrameMarkEnd(MAIN_FRAME);

    std::this_thread::sleep_until(frame_start +
                                  style_control_target_framerate());
  }

  app_destroy(app);

  return 0;
}
