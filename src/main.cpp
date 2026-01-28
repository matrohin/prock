#include "base.h"
#include "ring_buffer.h"
#include "sources/process_stat.h"
#include "sources/sync.h"
#include "state.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "implot.h"

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

// UNITY BUILD:
#include "base.cpp"
#include "sources/environ_reader.cpp"
#include "sources/library_reader.cpp"
#include "sources/process_stat.cpp"
#include "state.cpp"
#include "tracy/Tracy.hpp"
#include "views/brief_table.cpp"
#include "views/brief_table_logic.cpp"
#include "views/cpu_chart.cpp"
#include "views/entry.cpp"
#include "views/environ_viewer.cpp"
#include "views/io_chart.cpp"
#include "views/library_viewer.cpp"
#include "views/mem_chart.cpp"
#include "views/menu_bar.cpp"
#include "views/net_chart.cpp"
#include "views/process_host.cpp"
#include "views/process_window_flags.cpp"
#include "views/system_cpu_chart.cpp"
#include "views/threads_viewer.cpp"
#include "views/socket_viewer.cpp"
#include "views/system_io_chart.cpp"
#include "views/system_mem_chart.cpp"
#include "views/system_net_chart.cpp"

// See https://github.com/ocornut/imgui/issues/1206
// Sometimes imgui needs second frame update to handle some UI without delays.
// Reproducible example: context menus
static int g_needs_updates = 0;
static float g_applied_zoom_scale = 1.0f;
static bool g_applied_dark_mode = false;
static float g_monitor_scale = 1.0f;
static ImGuiStyle g_base_style;  // Style after theme + monitor scale, before zoom
void maintaining_second_update(GLFWwindow * /*window*/, int /*button*/,
                               int /*action*/, int /*mods*/) {
  g_needs_updates = 2;
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
  if (sscanf(line, "ShowPerCore=%d", &val) == 1) {
    view_state->system_cpu_chart_state.show_per_core = (val != 0);
  } else if (sscanf(line, "Stacked=%d", &val) == 1) {
    view_state->system_cpu_chart_state.stacked = (val != 0);
  } else if (sscanf(line, "DarkMode=%d", &val) == 1) {
    view_state->preferences_state.dark_mode = (val != 0);
  } else if (sscanf(line, "UpdatePeriod=%f", &fval) == 1) {
    view_state->preferences_state.update_period = fval;
  } else if (sscanf(line, "TargetFPS=%d", &val) == 1) {
    view_state->preferences_state.target_fps = val;
  } else if (sscanf(line, "TreeMode=%d", &val) == 1) {
    view_state->brief_table_state.tree_mode = (val != 0);
  } else if (sscanf(line, "ZoomScale=%f", &fval) == 1) {
    view_state->preferences_state.zoom_scale =
        fval < 0.75f ? 0.75f : (fval > 2.0f ? 2.0f : fval);
  } else if (strncmp(line, "FontPath=", 9) == 0) {
    const char *path = line + 9;
    size_t len = strlen(path);
    if (len < sizeof(view_state->preferences_state.font_path)) {
      memcpy(view_state->preferences_state.font_path, path, len + 1);
    }
  }
}

static void view_settings_write_all(ImGuiContext * /*ctx*/,
                                    ImGuiSettingsHandler *handler,
                                    ImGuiTextBuffer *buf) {
  ViewState *view_state = static_cast<ViewState *>(handler->UserData);
  if (!view_state) return;

  buf->appendf("[%s][SystemCpuChart]\n", handler->TypeName);
  buf->appendf(
      "ShowPerCore=%d\n",
      static_cast<int>(view_state->system_cpu_chart_state.show_per_core));
  buf->appendf("Stacked=%d\n",
               static_cast<int>(view_state->system_cpu_chart_state.stacked));
  buf->append("\n");

  buf->appendf("[%s][Preferences]\n", handler->TypeName);
  buf->appendf("DarkMode=%d\n",
               static_cast<int>(view_state->preferences_state.dark_mode));
  buf->appendf("UpdatePeriod=%.2f\n",
               view_state->preferences_state.update_period);
  buf->appendf("TargetFPS=%d\n", view_state->preferences_state.target_fps);
  buf->appendf("ZoomScale=%.2f\n", view_state->preferences_state.zoom_scale);
  if (view_state->preferences_state.font_path[0] != '\0') {
    buf->appendf("FontPath=%s\n", view_state->preferences_state.font_path);
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

static constexpr float BASE_FONT_SIZE = 15.0f;

static void load_fonts(ImGuiIO &io, const char *font_path, float scale) {
  io.Fonts->Clear();
  if (font_path && font_path[0] != '\0') {
    ImFont *font =
        io.Fonts->AddFontFromFileTTF(font_path, BASE_FONT_SIZE * scale);
    if (!font) {
      fprintf(stderr, "Failed to load font: %s, using default\n", font_path);
      io.Fonts->AddFontDefault();
    }
  } else {
    io.Fonts->AddFontDefault();
  }
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

  // Process thread snapshots before general update
  views_process_thread_snapshots(view_state, state, snapshot);
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

  constexpr ImGuiWindowFlags main_window_flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_MenuBar;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::Begin("prock", nullptr, main_window_flags);
  ImGui::PopStyleVar(3);

  const ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

  FrameContext frame_ctx = {};
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
  glClearColor(0.0, 0.0, 0.0, 1.0);
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

[Docking][Data]
DockSpace     ID=0xF352448A Window=0xEA9D8568 Pos=0,19 Size=1280,673 Split=Y
  DockNode    ID=0x00000001 Parent=0xF352448A SizeRef=1280,296 Split=X Selected=0x8286D95C
    DockNode  ID=0x00000003 Parent=0x00000001 SizeRef=640,397 Selected=0x8286D95C
    DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=638,397 Selected=0x49AB4810
  DockNode    ID=0x00000002 Parent=0xF352448A SizeRef=1280,502 CentralNode=1 Selected=0x67CD0030

[ViewSettings][SystemCpuChart]
ShowPerCore=0
Stacked=0
)";

constexpr const char *MAIN_FRAME = "main_frame";

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

  // Create window with graphics context
  float main_scale =
      ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
  g_monitor_scale = main_scale;
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

  // Set up config path in $HOME/.config/prock/
  static char ini_path[PATH_MAX] = {};
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

  ViewState view_state = {};

  // Register custom settings handler for view options
  ImGuiSettingsHandler handler = {};
  handler.TypeName = "ViewSettings";
  handler.TypeHash = ImHashStr(handler.TypeName);
  handler.ReadOpenFn = view_settings_read_open;
  handler.ReadLineFn = view_settings_read_line;
  handler.WriteAllFn = view_settings_write_all;
  handler.UserData = &view_state;
  ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

  if (access(io.IniFilename, F_OK) != 0) {
    ImGui::LoadIniSettingsFromMemory(DEFAULT_INI);
  } else {
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);
  }

  if (view_state.preferences_state.dark_mode) {
    ImGui::StyleColorsDark();
  } else {
    ImGui::StyleColorsLight();
  }

  // Apply monitor scale and save as base style (before zoom)
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(main_scale);
  style.WindowRounding = 0.0f;
  g_base_style = style;

  // Apply zoom on top of base style
  float zoom = view_state.preferences_state.zoom_scale;
  style.ScaleAllSizes(zoom);
  io.FontGlobalScale = zoom;
  g_applied_zoom_scale = zoom;
  g_applied_dark_mode = view_state.preferences_state.dark_mode;

  ImPlot::GetStyle().UseLocalTime = true;

  // Load fonts (before OpenGL backend init)
  load_fonts(io, view_state.preferences_state.font_path, main_scale);

  // Setup Platform/Renderer backends
  glfwSetMouseButtonCallback(window, maintaining_second_update);
  constexpr bool install_callbacks = true;
  ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks);
  ImGui_ImplOpenGL3_Init(glsl_version);

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
      glfwPostEmptyEvent();
    }
  }};

  std::thread proc_reader_thread{[&sync] {
    pthread_setname_np(pthread_self(), "proc_reader");
    library_reader_thread(sync);
  }};

  while (!glfwWindowShouldClose(window)) {
    FrameMark;

    if (g_needs_updates > 0) {
      glfwPollEvents();
      --g_needs_updates;
    } else {
      glfwWaitEvents();
    }

    // F3 toggles debug FPS display
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
      view_state.preferences_state.show_debug_fps =
          !view_state.preferences_state.show_debug_fps;
    }

    auto frame_start = SteadyClock::now();
    FrameMarkStart(MAIN_FRAME);
    if (update(state, view_state, sync)) {
      g_needs_updates = 2;
    }

    // Sync update period to gathering thread
    const float new_period = view_state.preferences_state.update_period;
    if (sync.update_period.load() != new_period) {
      sync.update_period.store(new_period);
      sync.quit_cv.notify_one();
    }

    // Update base style colors if theme changed
    const bool new_dark_mode = view_state.preferences_state.dark_mode;
    if (g_applied_dark_mode != new_dark_mode) {
      if (new_dark_mode) {
        ImGui::StyleColorsDark(&g_base_style);
      } else {
        ImGui::StyleColorsLight(&g_base_style);
      }
      g_applied_dark_mode = new_dark_mode;
    }

    // Apply zoom scale if changed
    const float new_zoom = view_state.preferences_state.zoom_scale;
    if (g_applied_zoom_scale != new_zoom) {
      // Restore base style (has monitor scale, no zoom)
      ImGuiStyle &style = ImGui::GetStyle();
      style = g_base_style;
      // Apply zoom on top
      style.ScaleAllSizes(new_zoom);
      io.FontGlobalScale = new_zoom;
      g_applied_zoom_scale = new_zoom;
    }

    // Reload font if requested
    if (view_state.preferences_state.font_needs_reload) {
      view_state.preferences_state.font_needs_reload = false;
      load_fonts(io, view_state.preferences_state.font_path, g_monitor_scale);
    }

    draw(window, io, state, view_state);

    glfwSwapBuffers(window);
    FrameMarkEnd(MAIN_FRAME);

    {
      const int target_fps = view_state.preferences_state.target_fps;
      const auto target_frame_time =
          std::chrono::microseconds(1'000'000 / target_fps);
      std::this_thread::sleep_until(frame_start + target_frame_time);
    }
  }

  // Cleanup
  sync.quit.store(true);
  sync.quit_cv.notify_one();
  sync.library_cv.notify_one();
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
