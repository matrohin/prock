#include "app.h"

#include "actions/elevate.h"
#include "views/entry.h"
#include "views/vim_nav.h"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <implot.h>
#include <misc/freetype/imgui_freetype.h>

#include <GLES2/gl2.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/stat.h>

// UNITY BUILD:
#include "actions/direct.cpp"
#include "actions/dump_writer.cpp"
#include "actions/elevate.cpp"
#include "actions/on_demand_actions.cpp"
#include "base/base.cpp"
#include "paths.cpp"
#include "playback/player.cpp"
#include "playback/recorder.cpp"
#include "readers/environ_reader.cpp"
#include "readers/font_list_reader.cpp"
#include "readers/library_reader.cpp"
#include "readers/on_demand_reader.cpp"
#include "readers/open_files_reader.cpp"
#include "readers/port_scan_reader.cpp"
#include "readers/proc_util.cpp"
#include "readers/process_stat.cpp"
#include "readers/properties_reader.cpp"
#include "readers/smaps_reader.cpp"
#include "readers/sock_diag.cpp"
#include "readers/socket_reader.cpp"
#include "readers/username.cpp"
#include "state/serialize.cpp"
#include "state/state.cpp"
#include "style_control.cpp"
#include "tracy/Tracy.hpp"
#include "views/brief_table.cpp"
#include "views/brief_table_logic.cpp"
#include "views/command_palette.cpp"
#include "views/cpu_chart.cpp"
#include "views/entry.cpp"
#include "views/environ_viewer.cpp"
#include "views/io_chart.cpp"
#include "views/library_viewer.cpp"
#include "views/mem_chart.cpp"
#include "views/menu_bar.cpp"
#include "views/notifications.cpp"
#include "views/on_demand_common.cpp"
#include "views/open_files_viewer.cpp"
#include "views/ports_viewer.cpp"
#include "views/process_host.cpp"
#include "views/process_window_flags.cpp"
#include "views/properties_viewer.cpp"
#include "views/replay_controls.cpp"
#include "views/smaps_viewer.cpp"
#include "views/socket_format.cpp"
#include "views/socket_viewer.cpp"
#include "views/system_cpu_chart.cpp"
#include "views/system_io_chart.cpp"
#include "views/system_mem_chart.cpp"
#include "views/system_net_chart.cpp"
#include "views/threads_viewer.cpp"
#include "views/vim_nav.cpp"

constexpr off_t SETTINGS_MAX_BYTES = 1 << 20;

static const char *DEFAULT_INI = R"(
[Window][prock]
Pos=0,0
Size=1280,692
Collapsed=0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Process Table]
Pos=0,298
Size=1280,502
Collapsed=0
DockId=0x00000002,0

[Window][System CPU Usage]
Pos=642,19
Size=638,296
Collapsed=0
DockId=0x00000004,0

[Window][System Memory Usage]
Pos=0,19
Size=640,296
Collapsed=0
DockId=0x00000003,0

[Window][System I/O]
Pos=0,19
Size=640,296
Collapsed=0
DockId=0x00000003,1

[Window][System Network]
Pos=0,19
Size=640,296
Collapsed=0
DockId=0x00000003,2

[Window][###ProcessTable]
Pos=0,317
Size=1280,375
Collapsed=0
DockId=0x00000002,0

[Window][Ports]
Pos=0,317
Size=1280,375
Collapsed=0
DockId=0x00000002,1

[Docking][Data]
DockSpace     ID=0xF352448A Window=0xEA9D8568 Pos=0,19 Size=1280,673 Split=Y
  DockNode    ID=0x00000001 Parent=0xF352448A SizeRef=1280,296 Split=X Selected=0x8286D95C
    DockNode  ID=0x00000003 Parent=0x00000001 SizeRef=640,397 Selected=0x8286D95C
    DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=638,397 Selected=0x49AB4810
  DockNode    ID=0x00000002 Parent=0xF352448A SizeRef=1280,502 CentralNode=1 Selected=0x67CD0030

[ViewSettings][SystemCpuChart]
Stacked=0
)";

static void glfw_error_callback(const int error, const char *description) {
  fprintf(stderr, "GLFW Error: %x: %s\n", error, description);
}

// See https://github.com/ocornut/imgui/issues/1206
// Sometimes imgui needs second frame update to handle some UI without delays.
// Reproducible example: context menus
static int g_needs_updates = 1;

// Window resizes as framebuffer-size events that do not enqueue any ImGui event
static bool g_framebuffer_resized = false;

static void maintaining_second_update(GLFWwindow * /*window*/, int /*button*/,
                                      int /*action*/, int /*mods*/) {
  g_needs_updates = 2;
}

// Keyboard nav applies a move request one frame after scoring it, and held
// keys autorepeat as GLFW_REPEAT events that enqueue no ImGui event — both
// need follow-up frames the wait-for-events loop would not produce otherwise.
static void key_maintaining_second_update(GLFWwindow * /*window*/, int /*key*/,
                                          int /*scancode*/, int /*action*/,
                                          int /*mods*/) {
  g_needs_updates = 2;
}

static void framebuffer_size_callback(GLFWwindow * /*window*/, int /*width*/,
                                      int /*height*/) {
  g_framebuffer_resized = true;
}

static void load_borrowed_settings(const char *path) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  struct stat st;
  if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
      st.st_size > SETTINGS_MAX_BYTES) {
    if (fd >= 0) close(fd);
    ImGui::LoadIniSettingsFromMemory(DEFAULT_INI);
    return;
  }

  BumpArena arena = BumpArena::create();
  const size_t size = static_cast<size_t>(st.st_size);
  char *buf = arena.alloc_string(size);
  size_t len = 0;
  while (len < size) {
    const ssize_t r = read(fd, buf + len, size - len);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) break;
    len += static_cast<size_t>(r);
  }
  close(fd);

  if (len > 0) {
    ImGui::LoadIniSettingsFromMemory(buf, len);
  } else {
    ImGui::LoadIniSettingsFromMemory(DEFAULT_INI);
  }
  arena.destroy();
}

static void *view_settings_read_open(ImGuiContext *,
                                     ImGuiSettingsHandler *handler,
                                     const char *name) {
  if (strcmp(name, "SystemCpuChart") == 0 || strcmp(name, "Preferences") == 0 ||
      strcmp(name, "ProcessTable") == 0) {
    return handler->UserData;
  }
  return nullptr;
}

static void set_path_setting(char *dst, const size_t size, const char *path) {
  if (g_borrowed_config) return;
  const size_t len = strlen(path);
  if (len < size) memcpy(dst, path, len + 1);
}

static void view_settings_read_line(ImGuiContext *, ImGuiSettingsHandler *,
                                    void *entry, const char *line) {
  ViewState *view_state = static_cast<ViewState *>(entry);
  if (!view_state) return;

  int val = 0;
  float fval = 0.0f;
  if (sscanf(line, "CpuPerCore=%d", &val) == 1) {
    view_state->preferences_state.cpu_per_core = val != 0;
  } else if (sscanf(line, "Stacked=%d", &val) == 1) {
    view_state->system_cpu_chart_state.stacked = val != 0;
  } else if (sscanf(line, "ShowMenuOnAlt=%d", &val) == 1) {
    view_state->preferences_state.show_menu_on_alt = val != 0;
  } else if (sscanf(line, "Theme=%d", &val) == 1) {
    if (val >= 0 && val < static_cast<int>(Theme::COUNT)) {
      view_state->preferences_state.theme = static_cast<Theme>(val);
    }
  } else if (sscanf(line, "UpdatePeriod=%f", &fval) == 1) {
    if (fval >= 0.0f && fval <= UPDATE_PERIOD_MAX_SEC) {
      view_state->preferences_state.update_period = fval;
    }
  } else if (sscanf(line, "TargetFPS=%d", &val) == 1) {
    view_state->preferences_state.target_fps =
        std::clamp(val, TARGET_FPS_MIN, TARGET_FPS_MAX);
  } else if (sscanf(line, "TreeMode=%d", &val) == 1) {
    view_state->brief_table_state.tree_mode = val != 0;
  } else if (sscanf(line, "ZoomScalePct=%d", &val) == 1) {
    view_state->preferences_state.zoom_scale_pct =
        std::clamp(val, ZOOM_MIN_PCT, ZOOM_MAX_PCT);
  } else if (sscanf(line, "WindowOpacityPct=%d", &val) == 1) {
    view_state->preferences_state.window_opacity_pct = std::clamp(val, 0, 100);
  } else if (strncmp(line, "FontPath=", 9) == 0) {
    set_path_setting(view_state->preferences_state.font_path,
                     sizeof(view_state->preferences_state.font_path), line + 9);
  } else if (strncmp(line, "MonoFontPath=", 13) == 0) {
    set_path_setting(view_state->preferences_state.mono_font_path,
                     sizeof(view_state->preferences_state.mono_font_path),
                     line + 13);
  } else if (strncmp(line, "DumpDir=", 8) == 0) {
    set_path_setting(view_state->preferences_state.dump_dir,
                     sizeof(view_state->preferences_state.dump_dir), line + 8);
  } else if (strncmp(line, "RecordingsDir=", 14) == 0) {
    set_path_setting(view_state->preferences_state.recordings_dir,
                     sizeof(view_state->preferences_state.recordings_dir),
                     line + 14);
  } else if (strncmp(line, "CustomColor_", 12) == 0) {
    int idx;
    float r, g, b, a;
    if (sscanf(line, "CustomColor_%d=%f,%f,%f,%f", &idx, &r, &g, &b, &a) == 5) {
      if (idx >= 0 && idx < ImGuiCol_COUNT) {
        g_custom_colors[idx] = ImVec4(r, g, b, a);
        g_custom_theme_inited = true;
      }
    }
  } else if (strncmp(line, "CustomAppColor_", 15) == 0) {
    int idx;
    float r, g, b, a;
    if (sscanf(line, "CustomAppColor_%d=%f,%f,%f,%f", &idx, &r, &g, &b, &a) == 5) {
      if (idx >= 0 && idx < eAppColor_COUNT) {
        g_custom_app_colors[idx] = ImVec4(r, g, b, a);
        g_custom_theme_inited = true;
      }
    }
  }
}

static void view_settings_write_all(ImGuiContext * /*ctx*/,
                                    ImGuiSettingsHandler *handler,
                                    ImGuiTextBuffer *buf) {
  ViewState *view_state = static_cast<ViewState *>(handler->UserData);
  if (!view_state) return;

  buf->appendf("[%s][SystemCpuChart]\n", handler->TypeName);
  buf->appendf("Stacked=%d\n",
               static_cast<int>(view_state->system_cpu_chart_state.stacked));
  buf->append("\n");

  buf->appendf("[%s][Preferences]\n", handler->TypeName);
  buf->appendf("CpuPerCore=%d\n",
               static_cast<int>(view_state->preferences_state.cpu_per_core));
  buf->appendf(
      "ShowMenuOnAlt=%d\n",
      static_cast<int>(view_state->preferences_state.show_menu_on_alt));
  buf->appendf("Theme=%d\n",
               static_cast<int>(view_state->preferences_state.theme));
  buf->appendf("UpdatePeriod=%.2f\n",
               view_state->preferences_state.update_period);
  buf->appendf("TargetFPS=%d\n", view_state->preferences_state.target_fps);
  buf->appendf("ZoomScalePct=%d\n",
               view_state->preferences_state.zoom_scale_pct);
  buf->appendf("WindowOpacityPct=%d\n",
               view_state->preferences_state.window_opacity_pct);
  if (view_state->preferences_state.font_path[0] != '\0') {
    buf->appendf("FontPath=%s\n", view_state->preferences_state.font_path);
  }
  if (view_state->preferences_state.mono_font_path[0] != '\0') {
    buf->appendf("MonoFontPath=%s\n",
                 view_state->preferences_state.mono_font_path);
  }
  if (view_state->preferences_state.dump_dir[0] != '\0') {
    buf->appendf("DumpDir=%s\n", view_state->preferences_state.dump_dir);
  }
  if (view_state->preferences_state.recordings_dir[0] != '\0') {
    buf->appendf("RecordingsDir=%s\n",
                 view_state->preferences_state.recordings_dir);
  }

  for (int i = 0; i < ImGuiCol_COUNT; ++i) {
    buf->appendf("CustomColor_%d=%.2f,%.2f,%.2f,%.2f\n", i,
                 g_custom_colors[i].x, g_custom_colors[i].y,
                 g_custom_colors[i].z, g_custom_colors[i].w);
  }
  for (int i = 0; i < eAppColor_COUNT; ++i) {
    buf->appendf("CustomAppColor_%d=%.2f,%.2f,%.2f,%.2f\n", i,
                 g_custom_app_colors[i].x, g_custom_app_colors[i].y,
                 g_custom_app_colors[i].z, g_custom_app_colors[i].w);
  }
  buf->append("\n");

  buf->appendf("[%s][ProcessTable]\n", handler->TypeName);
  buf->appendf("TreeMode=%d\n",
               static_cast<int>(view_state->brief_table_state.tree_mode));
  buf->append("\n");
}

static bool state_init(State &state) {
  const long ticks = sysconf(_SC_CLK_TCK);
  const long page_size = sysconf(_SC_PAGESIZE);
  if (ticks <= 0 || page_size <= 0) {
    return false;
  }
  state.system.ticks_in_second = ticks;
  state.system.mem_page_size = page_size;

  // Boot time (epoch seconds) is constant; read it once from /proc/stat
  // "btime".
  state.system.boot_time_epoch_sec = 0;
  if (FILE *stat_file = fopen("/proc/stat", "r")) {
    char line[256];
    while (fgets(line, sizeof(line), stat_file)) {
      unsigned long long btime;
      if (sscanf(line, "btime %llu", &btime) == 1) {
        state.system.boot_time_epoch_sec = btime;
        break;
      }
    }
    fclose(stat_file);
  }
  return true;
}

App *app_create(const AppParams &params) {
  SystemInfo replay_system = {};
  if (params.replay_path &&
      !playback_validate(params.replay_path, &replay_system)) {
    fprintf(stderr, "Cannot replay %s: not a valid .prck recording\n",
            params.replay_path);
    return nullptr;
  }

  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    return nullptr;
  }

  // GL ES 2.0 + GLSL 100 (WebGL 1.0)
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

  // Request an alpha-capable framebuffer so the window background can be made
  // translucent (Option B transparency) while text stays opaque.
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

  if (params.hidden_window) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  }

  // Create window with graphics context
  float main_scale =
      ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
  GLFWwindow *window = glfwCreateWindow(static_cast<int>(1280 * main_scale),
                                        static_cast<int>(800 * main_scale),
                                        "Prock", nullptr, nullptr);
  if (window == nullptr) {
    return nullptr;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(0);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigInputTextCursorBlink = false;

  // TODO: >800KB allocated which is more than 256KB (slab size),
  // it would be better to allocate more (X + 256KB) for persistent storage
  // right away and don't think about it afterwards (to not get one more alloc).
  App *app = create_with_arena<App>();
  app->window = window;
  ViewState &view_state = app->view_state;
  view_state.persistent_arena = &app->arena;
  view_state.string_interner = InternTable::create(&app->arena);

  // Register custom settings handler for view options
  ImGuiSettingsHandler handler = {};
  handler.TypeName = "ViewSettings";
  handler.TypeHash = ImHashStr(handler.TypeName);
  handler.ReadOpenFn = view_settings_read_open;
  handler.ReadLineFn = view_settings_read_line;
  handler.WriteAllFn = view_settings_write_all;
  handler.UserData = &app->view_state;
  ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

  g_borrowed_config = params.is_config_borrowed;
  if (!params.is_config_borrowed) {
    io.IniFilename = params.config_path;
    if (!io.IniFilename || access(io.IniFilename, F_OK) != 0) {
      ImGui::LoadIniSettingsFromMemory(DEFAULT_INI);
    } else {
      ImGui::LoadIniSettingsFromDisk(io.IniFilename);
    }
  } else {
    load_borrowed_settings(params.config_path);
    io.IniFilename = nullptr;
  }

  // Where we write follows our own $HOME, not the borrowed settings: a
  // borrowed DumpDir would be picking a path for root.
  if (view_state.preferences_state.dump_dir[0] == '\0') {
    dump_writer_default_dir(view_state.preferences_state.dump_dir,
                            sizeof(view_state.preferences_state.dump_dir));
  }
  if (view_state.preferences_state.recordings_dir[0] == '\0') {
    recorder_default_dir(view_state.preferences_state.recordings_dir,
                         sizeof(view_state.preferences_state.recordings_dir));
  }

  style_control_init(view_state.preferences_state.theme, main_scale,
                     view_state.preferences_state.target_fps);
  style_control_rebuild(view_state.preferences_state.zoom_scale_pct,
                        view_state.preferences_state.window_opacity_pct);
  style_control_load_fonts(view_state.preferences_state.font_path,
                           view_state.preferences_state.mono_font_path);

  // Setup Platform/Renderer backends
  glfwSetMouseButtonCallback(window, maintaining_second_update);
  glfwSetKeyCallback(window, key_maintaining_second_update);
  constexpr bool install_callbacks = true;
  ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Redraw while the window is being resized. The ImGui GLFW backend does not
  // install a framebuffer-size callback, so set ours after init.
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  // Setup state. In replay the system info comes from the recording (needed for
  // correct CPU%/RSS/start-time); otherwise it is read from the live host.
  if (params.replay_path) {
    app->state.system = replay_system;
  } else if (!state_init(app->state)) {
    fprintf(stderr, "Failed to get system configuration\n");
    return nullptr;
  }

  app->sync.replay_mode = params.replay_path != nullptr;
  view_state.sync = &app->sync;
  view_state.replay_state.active_path = params.replay_path;
  app->sync.update_period.store(view_state.preferences_state.update_period);
  Sync *sync = &app->sync;

  if (app->sync.replay_mode) {
    app->producer_thread =
        std::thread{[sync, replay_path = params.replay_path] {
          pthread_setname_np(pthread_self(), "playback");
          playback_loop(*sync, replay_path);
        }};
  } else {
    app->producer_thread =
        std::thread{[sync, ticks = app->state.system.ticks_in_second] {
          pthread_setname_np(pthread_self(), "gathering");
          GatheringState gathering_state = {};
          gathering_state.ticks_in_second = ticks;
          gathering_state.usernames =
              UsernameResolver::create(&gathering_state.persistent_arena);
          while (!sync->quit.load()) {
            process_stat_gather(gathering_state, *sync);
          }
        }};
  }

  app->proc_reader_thread = std::thread{[sync] {
    pthread_setname_np(pthread_self(), "proc_reader");
    on_demand_reader_loop(*sync);
  }};

  app->actions_thread = std::thread{[sync] {
    pthread_setname_np(pthread_self(), "od_actions");
    on_demand_actions_loop(*sync);
  }};

  app->recorder_thread = std::thread{[sync] {
    pthread_setname_np(pthread_self(), "recorder");
    recorder_loop(*sync);
  }};

  return app;
}

FrameContext app_start_frame(App *app) {
  while (true) {
    if (g_needs_updates > 0) {
      glfwPollEvents();
      --g_needs_updates;
    } else {
      glfwWaitEvents();
      const bool data_ready = app->sync.data_ready.exchange(false);
      const bool resized = g_framebuffer_resized;
      g_framebuffer_resized = false;
      if (!data_ready && !resized &&
          ImGui::GetCurrentContext()->InputEventsQueue.Size == 0) {
        continue;
      }
    }
    break;
  }
  return FrameContext{};
}

static void state_update(FrameContext &frame_ctx, App &app,
                         UpdateSnapshot &snapshot) {
  State &state = app.state;
  ViewState &view_state = app.view_state;

  BumpArena old_arena = state.snapshot_arena;
  state.snapshot_arena = snapshot.owner_arena;
  state.snapshot = state_snapshot_update(state.snapshot_arena, state, snapshot);
  state.update_count += 1;
  state.update_system_time = snapshot.system_time;

  entry_views_update(frame_ctx, view_state, state);
  recorder_update(view_state, snapshot, app.sync);

  // Save the old arena to continue to show it in all the tables:
  const bool paused = !view_state.preferences_state.auto_follow;
  if (paused && !state.frozen_snapshot_arena.cur_slab) {
    state.frozen_snapshot_arena = old_arena;
  } else {
    old_arena.destroy();
    if (!paused) {
      state.frozen_snapshot_arena.destroy();
    }
  }
}

static bool update(FrameContext &frame_ctx, App &app) {
  ZoneScoped;
  // A replay restart wipes the previous pass's chart + table history so the new
  // pass, streamed from the file's start, repopulates it from scratch. Clear
  // before draining the queue below - never drain here, or the restarted pass's
  // already-queued snapshots would be thrown away and the views stay empty.
  if (app.view_state.replay_state.reset_history_request) {
    app.view_state.replay_state.reset_history_request = false;
    entry_views_reset_history(app.view_state);
  }
  UpdateSnapshot snapshot = {};
  bool updated = false;
  while (app.sync.update_queue.pop(snapshot)) {
    state_update(frame_ctx, app, snapshot);
    updated = true;
  }
  return updated;
}

void app_update(App *app, FrameContext &frame_ctx) {
  if (update(frame_ctx, *app)) {
    g_needs_updates = 2;
  }

  ViewState &view_state = app->view_state;
  Sync &sync = app->sync;

  // Sync update period to the gathering thread (replay ignores it - the
  // playback thread paces itself from the recorded timestamps).
  const float new_period = view_state.preferences_state.update_period;
  if (!sync.replay_mode && sync.update_period.load() != new_period) {
    {
      std::lock_guard<std::mutex> lock(sync.quit_mutex);
      sync.update_period.store(new_period);
    }
    sync.quit_cv.notify_one();
  }

  if (view_state.preferences_state.font_needs_reload) {
    view_state.preferences_state.font_needs_reload = false;
    style_control_load_fonts(view_state.preferences_state.font_path,
                             view_state.preferences_state.mono_font_path);
  }

  // Recording start/stop is requested from the menu/palette (draw phase) and
  // performed here where State/Sync are mutable; responses become toasts.
  // Recording is meaningless during replay, so the request is dropped there.
  if (view_state.recorder.toggle_request) {
    view_state.recorder.toggle_request = false;
    if (!sync.replay_mode) recorder_toggle(view_state, app->state, sync);
  }
  recorder_drain_responses(view_state, sync);

  // A "Replay a recording..." dialog pick re-execs this binary in replay
  // mode; persist the layout first so the new session keeps it. execv only
  // returns on failure. Validate here (in the still-live process) so a bad
  // pick becomes a toast instead of a re-exec that immediately exits.
  if (view_state.replay_state.launch_request) {
    view_state.replay_state.launch_request = false;
    SystemInfo probe = {};
    if (!playback_validate(view_state.replay_state.open_path, &probe)) {
      notify_error(view_state.notifications, 0,
                   "Cannot replay %s: not a valid .prck recording",
                   view_state.replay_state.open_path);
    } else {
      // TODO: Ideally we should call Destroy* functions here,
      // but fundamentally it works w/o that of course.
      ImGuiIO &io = ImGui::GetIO();
      ImGui::SaveIniSettingsToDisk(io.IniFilename);
      char *const args[] = {const_cast<char *>("prock"),
                            const_cast<char *>("--replay"),
                            view_state.replay_state.open_path, nullptr};
      execv("/proc/self/exe", args);
      notify_error(view_state.notifications, errno,
                   "Failed to start replay: %s", strerror(errno));
    }
  }
}

static void draw_main_window(FrameContext &frame_ctx, const ImGuiIO &io,
                             const State &state, ViewState &view_state) {
  ZoneScoped;

  ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0.0, 0.0), ImGuiCond_Always);

  ImGuiWindowFlags main_window_flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;
  if (view_state.preferences_state.show_menu_on_alt &&
      !ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
    main_window_flags &= ~ImGuiWindowFlags_MenuBar;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::Begin("prock", nullptr, main_window_flags);
  ImGui::PopStyleVar(3);

  // Hide the dock-node menu caret on the main panels (system charts, process
  // table, ports). The flag inherits to every child node of this dockspace.
  // Per-process host windows host their own dockspace (without the flag), so
  // they keep the caret as a way to pick among their inspector sub-windows.
  const ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                   ImGuiDockNodeFlags_NoWindowMenuButton);

  entry_views_on_demand_update(view_state);
  entry_views_draw(frame_ctx, view_state, state);

  ImGui::End();
}

void app_draw(App *app, FrameContext &frame_ctx) {
  ZoneScoped;
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  vim_nav_translate_events();
  ImGui::NewFrame();

  ImGuiIO &io = ImGui::GetIO();
  draw_main_window(frame_ctx, io, app->state, app->view_state);
}

void app_render(App *app) {
  ImGui::Render();
  int display_w = 0;
  int display_h = 0;
  glfwGetFramebufferSize(app->window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  // Clear to a transparent framebuffer when transparency is enabled so gaps
  // show the desktop; opaque windows then write alpha 1.0 over it.
  const float clear_alpha =
      app->view_state.preferences_state.window_opacity_pct < 100 ? 0.0f : 1.0f;
  glClearColor(0.0f, 0.0f, 0.0f, clear_alpha);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void app_end_frame(FrameContext &frame_ctx) {
  frame_ctx.frame_arena.destroy();

  // The modal / Ctrl+Tab dim background fades via ImGui's DimBgRatio over
  // several frames. Because we render on demand, keep requesting frames while
  // that fade is mid-flight (0 < ratio < 1) so the dim doesn't stall until
  // the next input event. A saturated (1.0) or cleared (0.0) ratio needs no
  // redraw, so the loop goes back to blocking once the fade settles.
  const float dim_ratio = ImGui::GetCurrentContext()->DimBgRatio;
  if (dim_ratio > 0.0f && dim_ratio < 1.0f && g_needs_updates < 1) {
    g_needs_updates = 1;
  }
}

void app_destroy(App *app) {
  Sync &sync = app->sync;
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.quit.store(true);
  }
  sync.quit_cv.notify_one();
  sync.on_demand_reader.request_read_cv.notify_one();
  sync.on_demand_actions.request_cv.notify_one();
  sync.recorder.request_cv.notify_one();

  app->producer_thread.join();
  app->proc_reader_thread.join();
  app->actions_thread.join();
  app->recorder_thread.join();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(app->window);
  glfwTerminate();
}

void sock_notify_data_ready(Sync &sync) {
  sync.data_ready.store(true);
  glfwPostEmptyEvent();
}
