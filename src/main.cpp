#include "base/base.h"
#include "base/channel.h"
#include "constants.h"
#include "sources/process_stat.h"
#include "sources/sync.h"
#include "state.h"
#include "views/icons.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "implot.h"
#include "misc/freetype/imgui_freetype.h"

#include <GLES2/gl2.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

void notify_data_ready(Sync &sync) {
  sync.data_ready.store(true);
  glfwPostEmptyEvent();
}

// UNITY BUILD:
#include "base/base.cpp"
#include "sources/dump_writer.cpp"
#include "sources/environ_reader.cpp"
#include "sources/font_list_reader.cpp"
#include "sources/library_reader.cpp"
#include "sources/on_demand_reader.cpp"
#include "sources/port_scan_reader.cpp"
#include "sources/proc_parsers.cpp"
#include "sources/process_stat.cpp"
#include "sources/smaps_reader.cpp"
#include "sources/socket_reader.cpp"
#include "state.cpp"
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
#include "views/ports_viewer.cpp"
#include "views/process_host.cpp"
#include "views/process_window_flags.cpp"
#include "views/smaps_viewer.cpp"
#include "views/socket_format.cpp"
#include "views/socket_viewer.cpp"
#include "views/system_cpu_chart.cpp"
#include "views/system_io_chart.cpp"
#include "views/system_mem_chart.cpp"
#include "views/system_net_chart.cpp"
#include "views/threads_viewer.cpp"

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

static void framebuffer_size_callback(GLFWwindow * /*window*/, int /*width*/,
                                      int /*height*/) {
  g_framebuffer_resized = true;
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

static void view_settings_read_line(ImGuiContext *, ImGuiSettingsHandler *,
                                    void *entry, const char *line) {
  ViewState *view_state = static_cast<ViewState *>(entry);
  if (!view_state) return;

  int val = 0;
  float fval = 0.0f;
  if (sscanf(line, "CpuPerCore=%d", &val) == 1) {
    view_state->preferences_state.cpu_per_core = (val != 0);
  } else if (sscanf(line, "Stacked=%d", &val) == 1) {
    view_state->system_cpu_chart_state.stacked = (val != 0);
  } else if (sscanf(line, "ShowMenuOnAlt=%d", &val) == 1) {
    view_state->preferences_state.show_menu_on_alt = (val != 0);
  } else if (sscanf(line, "Theme=%d", &val) == 1) {
    if (val >= 0 && val < static_cast<int>(Theme::COUNT)) {
      view_state->preferences_state.theme = static_cast<Theme>(val);
    }
  } else if (sscanf(line, "UpdatePeriod=%f", &fval) == 1) {
    view_state->preferences_state.update_period = fval;
  } else if (sscanf(line, "TargetFPS=%d", &val) == 1) {
    view_state->preferences_state.target_fps =
        std::clamp(val, TARGET_FPS_MIN, TARGET_FPS_MAX);
  } else if (sscanf(line, "TreeMode=%d", &val) == 1) {
    view_state->brief_table_state.tree_mode = (val != 0);
  } else if (sscanf(line, "ZoomScalePct=%d", &val) == 1) {
    view_state->preferences_state.zoom_scale_pct =
        std::clamp(val, ZOOM_MIN_PCT, ZOOM_MAX_PCT);
  } else if (sscanf(line, "WindowOpacityPct=%d", &val) == 1) {
    view_state->preferences_state.window_opacity_pct = std::clamp(val, 0, 100);
  } else if (strncmp(line, "FontPath=", 9) == 0) {
    const char *path = line + 9;
    const uint32_t len = static_cast<uint32_t>(strlen(path));
    if (len < sizeof(view_state->preferences_state.font_path)) {
      memcpy(view_state->preferences_state.font_path, path, len + 1);
    }
  } else if (strncmp(line, "DumpDir=", 8) == 0) {
    const char *path = line + 8;
    const uint32_t len = static_cast<uint32_t>(strlen(path));
    if (len < sizeof(view_state->preferences_state.dump_dir)) {
      memcpy(view_state->preferences_state.dump_dir, path, len + 1);
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
  if (view_state->preferences_state.dump_dir[0] != '\0') {
    buf->appendf("DumpDir=%s\n", view_state->preferences_state.dump_dir);
  }
  buf->append("\n");

  buf->appendf("[%s][ProcessTable]\n", handler->TypeName);
  buf->appendf("TreeMode=%d\n",
               static_cast<int>(view_state->brief_table_state.tree_mode));
  buf->append("\n");
}

static void glfw_error_callback(const int error, const char *description) {
  fprintf(stderr, "GLFW Error: %x: %s\n", error, description);
}

static bool state_init(State &state) {
  const long ticks = sysconf(_SC_CLK_TCK);
  const long page_size = sysconf(_SC_PAGESIZE);
  if (ticks <= 0 || page_size <= 0) {
    fprintf(stderr, "Failed to get system configuration\n");
    return false;
  }
  state.system.ticks_in_second = ticks;
  state.system.mem_page_size = page_size;
  return true;
}

static void state_update(State &state, ViewState &view_state,
                         const UpdateSnapshot &snapshot) {
  BumpArena old_arena = state.snapshot_arena;

  state.snapshot_arena = snapshot.owner_arena;
  state.snapshot = state_snapshot_update(state.snapshot_arena, state, snapshot);
  state.update_count += 1;
  state.update_system_time = snapshot.system_time;

  views_update(view_state, state);

  old_arena.destroy();
}

static bool update(State &state, ViewState &view_state, Sync &sync) {
  ZoneScoped;
  UpdateSnapshot snapshot = {};
  bool updated = false;
  while (sync.update_queue.pop(snapshot)) {
    state_update(state, view_state, snapshot);
    updated = true;
  }
  return updated;
}

static void draw_main_window(const ImGuiIO &io, const State &state,
                             ViewState &view_state) {
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

  FrameContext frame_ctx = {};
  views_on_demand_update(view_state);
  views_draw(frame_ctx, view_state, state);
  frame_ctx.frame_arena.destroy();

  ImGui::End();
}

static void draw(GLFWwindow *window, const ImGuiIO &io, const State &state,
                 ViewState &view_state) {
  ZoneScoped;
  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  {
    ZoneScopedN("ImGui frame");
    ImGui::NewFrame();

    draw_main_window(io, state, view_state);

    ImGui::Render();
  }
  int display_w = 0;
  int display_h = 0;
  glfwGetFramebufferSize(window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  // Clear to a transparent framebuffer when transparency is enabled so gaps
  // show the desktop; opaque windows then write alpha 1.0 over it.
  const float clear_alpha =
      view_state.preferences_state.window_opacity_pct < 100 ? 0.0f : 1.0f;
  glClearColor(0.0f, 0.0f, 0.0f, clear_alpha);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

const char *DEFAULT_INI = R"(
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

[[maybe_unused]] constexpr const char *MAIN_FRAME = "main_frame";

int main(int, char **) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    return 1;
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

  // Create window with graphics context
  float main_scale =
      ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
  GLFWwindow *window = glfwCreateWindow(static_cast<int>(1280 * main_scale),
                                        static_cast<int>(800 * main_scale),
                                        "Prock", nullptr, nullptr);
  if (window == nullptr) {
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(0);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;           // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
  io.ConfigInputTextCursorBlink = false;

  // Set up config path: PROCK_CONFIG_DIR or $HOME/.config/prock/
  static char ini_path[PATH_MAX] = {};
  const char *config_dir = getenv("PROCK_CONFIG_DIR");
  if (config_dir) {
    // Use explicit config dir (e.g., when running elevated via pkexec)
    mkdir(config_dir, 0755);
    int n = snprintf(ini_path, sizeof(ini_path), "%s/settings.ini", config_dir);
    if (n > 0 && static_cast<size_t>(n) < sizeof(ini_path)) {
      io.IniFilename = ini_path;
    }
  } else {
    const char *home = getenv("HOME");
    if (home) {
      char dir_path[PATH_MAX] = {};
      int n = 0;
      // Ensure .config directory exists
      n = snprintf(dir_path, sizeof(dir_path), "%s/.config", home);
      if (n > 0 && static_cast<size_t>(n) < sizeof(dir_path)) {
        mkdir(dir_path, 0755);
        // Ensure .config/prock directory exists
        n = snprintf(dir_path, sizeof(dir_path), "%s/.config/prock", home);
        if (n > 0 && static_cast<size_t>(n) < sizeof(dir_path)) {
          mkdir(dir_path, 0755);
          // Set the ini file path
          n = snprintf(ini_path, sizeof(ini_path),
                       "%s/.config/prock/settings.ini", home);
          if (n > 0 && static_cast<size_t>(n) < sizeof(ini_path)) {
            io.IniFilename = ini_path;
          }
        }
      }
    }
  }

  ViewState *view_state_ptr = create_with_arena<ViewState>();
  ViewState &view_state = *view_state_ptr;
  view_state.string_interner = InternTable::create(&view_state.arena);

  // Register custom settings handler for view options
  ImGuiSettingsHandler handler = {};
  handler.TypeName = "ViewSettings";
  handler.TypeHash = ImHashStr(handler.TypeName);
  handler.ReadOpenFn = view_settings_read_open;
  handler.ReadLineFn = view_settings_read_line;
  handler.WriteAllFn = view_settings_write_all;
  handler.UserData = view_state_ptr;
  ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

  if (access(io.IniFilename, F_OK) != 0) {
    ImGui::LoadIniSettingsFromMemory(DEFAULT_INI);
  } else {
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);
  }

  // Seed the dump folder on first run (while $HOME still points at the real
  // user) so it persists in settings; an elevated relaunch then reads it back
  // instead of defaulting to /root.
  if (view_state.preferences_state.dump_dir[0] == '\0') {
    default_dump_dir(view_state.preferences_state.dump_dir,
                     sizeof(view_state.preferences_state.dump_dir));
  }

  style_control_init(view_state.preferences_state.theme, main_scale,
                     view_state.preferences_state.target_fps);
  style_control_rebuild(view_state.preferences_state.zoom_scale_pct,
                        view_state.preferences_state.window_opacity_pct);
  style_control_load_fonts(view_state.preferences_state.font_path);

  // Setup Platform/Renderer backends
  glfwSetMouseButtonCallback(window, maintaining_second_update);
  constexpr bool install_callbacks = true;
  ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Redraw while the window is being resized. The ImGui GLFW backend does not
  // install a framebuffer-size callback, so set ours after init.
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  // Setup state
  State state = {};
  if (!state_init(state)) {
    return 1;
  }

  Sync sync = {};
  view_state.sync = &sync;
  sync.update_period.store(view_state.preferences_state.update_period);

  std::thread gathering_thread{[&sync] {
    pthread_setname_np(pthread_self(), "gathering");
    GatheringState gathering_state = {};
    while (!sync.quit.load()) {
      gather(gathering_state, sync);
    }
  }};

  std::thread proc_reader_thread{[&sync] {
    pthread_setname_np(pthread_self(), "proc_reader");
    on_demand_reader_loop(sync);
  }};

  while (!glfwWindowShouldClose(window)) {
    FrameMark;

    while (true) {
      if (g_needs_updates > 0) {
        glfwPollEvents();
        --g_needs_updates;
      } else {
        glfwWaitEvents();
        const bool data_ready = sync.data_ready.exchange(false);
        const bool resized = g_framebuffer_resized;
        g_framebuffer_resized = false;
        if (!data_ready && !resized &&
            ImGui::GetCurrentContext()->InputEventsQueue.Size == 0) {
          continue;
        }
      }
      break;
    }

    auto frame_start = SteadyClock::now();
    FrameMarkStart(MAIN_FRAME);
    if (update(state, view_state, sync)) {
      g_needs_updates = 2;
    }

    // Sync update period to gathering thread
    const float new_period = view_state.preferences_state.update_period;
    if (sync.update_period.load() != new_period) {
      {
        std::lock_guard<std::mutex> lock(sync.quit_mutex);
        sync.update_period.store(new_period);
      }
      sync.quit_cv.notify_one();
    }

    if (view_state.preferences_state.font_needs_reload) {
      view_state.preferences_state.font_needs_reload = false;
      style_control_load_fonts(view_state.preferences_state.font_path);
    }

    draw(window, io, state, view_state);

    // The modal / Ctrl+Tab dim background fades via ImGui's DimBgRatio over
    // several frames. Because we render on demand, keep requesting frames while
    // that fade is mid-flight (0 < ratio < 1) so the dim doesn't stall until
    // the next input event. A saturated (1.0) or cleared (0.0) ratio needs no
    // redraw, so the loop goes back to blocking once the fade settles.
    const float dim_ratio = ImGui::GetCurrentContext()->DimBgRatio;
    if (dim_ratio > 0.0f && dim_ratio < 1.0f && g_needs_updates < 1) {
      g_needs_updates = 1;
    }

    glfwSwapBuffers(window);
    FrameMarkEnd(MAIN_FRAME);

    std::this_thread::sleep_until(frame_start +
                                  style_control_target_framerate());
  }

  // Cleanup
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    sync.quit.store(true);
  }
  sync.quit_cv.notify_one();
  sync.on_demand_reader.request_read_cv.notify_one();
  gathering_thread.join();
  proc_reader_thread.join();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
