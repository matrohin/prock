#include "base.h"
#include "state.h"
#include "process_stat.h"
#include "ring_buffer.h"
#include "sync.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "implot.h"

#include <GLES2/gl2.h>
#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// UNITY BUILD:
#include "state.cpp"
#include "process_stat.cpp"
#include "library_reader.cpp"
#include "views/entry.cpp"
#include "views/brief_table_logic.cpp"
#include "views/brief_table.cpp"
#include "views/cpu_chart.cpp"
#include "views/mem_chart.cpp"
#include "views/io_chart.cpp"
#include "views/system_cpu_chart.cpp"
#include "views/system_mem_chart.cpp"
#include "views/system_io_chart.cpp"
#include "views/library_viewer.cpp"

namespace {

// See https://github.com/ocornut/imgui/issues/1206
// Sometimes imgui needs second frame update to handle some UI without delays.
// Reproducible example: context menus
static int g_needs_updates = 0;
void maintaining_second_update(GLFWwindow* /*window*/, int /*button*/, int /*action*/, int /*mods*/) {
  g_needs_updates = 2;
}

static void* view_settings_read_open(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) {
  if (strcmp(name, "SystemCpuChart") == 0) {
    return handler->UserData;
  }
  return nullptr;
}

static void view_settings_read_line(ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
  ViewState* view_state = static_cast<ViewState *>(entry);
  if (!view_state) return;

  int val = 0;
  if (sscanf(line, "ShowPerCore=%d", &val) == 1) {
    view_state->system_cpu_chart_state.show_per_core = (val != 0);
  } else if (sscanf(line, "Stacked=%d", &val) == 1) {
    view_state->system_cpu_chart_state.stacked = (val != 0);
  }
}

static void view_settings_write_all(ImGuiContext */*ctx*/, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf) {
  ViewState* view_state = static_cast<ViewState *>(handler->UserData);
  if (!view_state) return;

  buf->appendf("[%s][SystemCpuChart]\n", handler->TypeName);
  buf->appendf("ShowPerCore=%d\n", (int) view_state->system_cpu_chart_state.show_per_core);
  buf->appendf("Stacked=%d\n", (int) view_state->system_cpu_chart_state.stacked);
  buf->append("\n");
}

void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error: %x: %s\n", error, description);
}

bool state_init(State &state) {
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

void state_update(State &state, ViewState &view_state, const UpdateSnapshot &snapshot) {
  BumpArena old_arena = state.snapshot_arena;

  state.snapshot_arena = snapshot.owner_arena;
  state.snapshot = state_snapshot_update(state.snapshot_arena, state, snapshot);
  state.update_count += 1;
  state.update_system_time = snapshot.system_time;

  views_update(view_state, state);

  old_arena.destroy();
}

bool update(State &state, ViewState &view_state, Sync &sync) {
  UpdateSnapshot snapshot = {};
  bool updated = false;
  while (sync.update_queue.pop(snapshot)) {
    state_update(state, view_state, snapshot);
    updated = true;
  }
  return updated;
}

void draw(GLFWwindow *window, ImGuiIO &io, const State &state, ViewState &view_state) {
  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(0.0, 0.0), ImGuiCond_Always);

  ImGuiWindowFlags main_window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

  ImGui::Begin("prock", nullptr, main_window_flags);
  ImGui::PopStyleVar(3);

  ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

  FrameContext frame_ctx = {};
  views_draw(frame_ctx, view_state, state);
  frame_ctx.frame_arena.destroy();

  ImGui::End();

  ImGui::Render();
  int display_w = 0;
  int display_h = 0;
  glfwGetFramebufferSize(window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

const char* DEFAULT_INI = R"(
[Window][prock]
Pos=0,0
Size=1280,800
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
Pos=642,0
Size=638,296
Collapsed=0
DockId=0x00000004,0

[Window][System Memory Usage]
Pos=0,0
Size=640,296
Collapsed=0
DockId=0x00000003,0

[Window][System I/O]
Pos=0,0
Size=640,296
Collapsed=0
DockId=0x00000003,1

[Docking][Data]
DockSpace     ID=0xF352448A Window=0xEA9D8568 Pos=0,0 Size=1280,800 Split=Y
  DockNode    ID=0x00000001 Parent=0xF352448A SizeRef=1280,296 Split=X Selected=0x8286D95C
    DockNode  ID=0x00000003 Parent=0x00000001 SizeRef=640,397 Selected=0x8286D95C
    DockNode  ID=0x00000004 Parent=0x00000001 SizeRef=638,397 Selected=0x49AB4810
  DockNode    ID=0x00000002 Parent=0xF352448A SizeRef=1280,502 CentralNode=1 Selected=0x5DB0E023

[ViewSettings][SystemCpuChart]
ShowPerCore=0
Stacked=0
)";

} // unnamed namespace

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

  // Create window with graphics context
  float main_scale =
      ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
  GLFWwindow *window =
      glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale),
                       "Prock", nullptr, nullptr);
  if (window == nullptr) {
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

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
  const char* home = getenv("HOME");
  if (home) {
    char dir_path[PATH_MAX] = {};
    int n = 0;
    // Ensure .config directory exists
    n = snprintf(dir_path, sizeof(dir_path), "%s/.config", home);
    if (n > 0 && (size_t)n < sizeof(dir_path)) {
      mkdir(dir_path, 0755);
      // Ensure .config/prock directory exists
      n = snprintf(dir_path, sizeof(dir_path), "%s/.config/prock", home);
      if (n > 0 && (size_t)n < sizeof(dir_path)) {
        mkdir(dir_path, 0755);
        // Set the ini file path
        n = snprintf(ini_path, sizeof(ini_path), "%s/.config/prock/settings.ini", home);
        if (n > 0 && (size_t)n < sizeof(ini_path)) {
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
  }

  ImGui::StyleColorsLight();

  ImGuiStyle &style = ImGui::GetStyle();
  style.ScaleAllSizes(main_scale);
  style.WindowRounding = 0.0f;

  ImPlot::GetStyle().UseLocalTime = true;

  /// Experimental flags:
  // io.ConfigDpiScaleFonts = true;
  // io.ConfigDpiScaleViewports = true;

  // Setup Platform/Renderer backends
  glfwSetMouseButtonCallback(window, maintaining_second_update);
  const bool install_callbacks = true;
  ImGui_ImplGlfw_InitForOpenGL(window, install_callbacks);
  ImGui_ImplOpenGL3_Init(glsl_version);


  // Setup state
  State state = {};
  if (!state_init(state)) {
    return 1;
  }

  Sync sync = {};
  view_state.sync = &sync;

  std::thread gathering_thread{
    [&sync]() {
      GatheringState state = {};
      while (!sync.quit.load()) {
        gather(state, sync);
        glfwPostEmptyEvent();
      }
    }
  };

  std::thread library_thread{
    [&sync]() {
      library_reader_thread(sync);
    }
  };

  while (!glfwWindowShouldClose(window)) {
    if (g_needs_updates > 0) {
      glfwPollEvents();
      --g_needs_updates;
    } else {
      glfwWaitEvents();
    }

    if (update(state, view_state, sync)) {
      g_needs_updates = 2;
    }
    draw(window, io, state, view_state);

    glfwSwapBuffers(window);
  }

  // Cleanup
  sync.quit.store(true);
  sync.quit_cv.notify_one();
  sync.library_cv.notify_one();
  gathering_thread.join();
  library_thread.join();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
