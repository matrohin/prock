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
#include <cstring>
#include <dirent.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// UNITY BUILD:
#include "state.cpp"
#include "process_stat.cpp"
#include "views/entry.cpp"
#include "views/full_table.cpp"
#include "views/brief_table.cpp"
#include "views/cpu_chart.cpp"
#include "views/mem_chart.cpp"

namespace {

// See https://github.com/ocornut/imgui/issues/1206
// Sometimes imgui needs second frame update to handle some UI without delays.
// Reproducible example: context menus
static int g_needs_updates = 0;
void maintaining_second_update(GLFWwindow* /*window*/, int /*button*/, int /*action*/, int /*mods*/) {
  g_needs_updates = 2;
}


constexpr float MIN_DELTA_TIME = 1.0f / 60.0f; // 60fps

void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error: %s\n", description);
}

void state_init(State &state) {
  state.system.ticks_in_second = sysconf(_SC_CLK_TCK);
  state.system.mem_page_size = sysconf(_SC_PAGESIZE);
}

void state_update(State &state, ViewState &view_state, const UpdateSnapshot &snapshot) {
  BumpArena old_arena = state.snapshot_arena;
  StateSnapshot old_snapshot = state.snapshot;

  state.snapshot_arena = snapshot.owner_arena;
  state.snapshot = state_snapshot_update(state.snapshot_arena, state, snapshot);
  state.update_count += 1;
  state.update_system_time = snapshot.system_time;

  views_update(view_state, state, old_snapshot);

  old_arena.destroy();
}

void update(State &state, ViewState &view_state, Sync &sync) {
  UpdateSnapshot snapshot = {};
  while (sync.update_queue.pop(snapshot)) {
    state_update(state, view_state, snapshot);
  }
}

void draw(GLFWwindow *window, ImGuiIO &io, const State &state, ViewState &view_state) {
  /*if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
    ImGui_ImplGlfw_Sleep(50);
    return;
  }*/

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

  ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
  ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

  views_draw(view_state, state);
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
  Sync sync = {};
  std::thread gathering_thread{
    [&sync]() {
      GatheringState state = {};
      while (!sync.quit.load()) {
        gather(state, sync);
        glfwPostEmptyEvent();
      }
    }
  };

  State state = {};
  ViewState view_state = {};
  state_init(state);

  while (!glfwWindowShouldClose(window)) {
    if (g_needs_updates > 0) {
      glfwPollEvents();
      --g_needs_updates;
    } else {
      glfwWaitEvents();
    }

    update(state, view_state, sync);
    draw(window, io, state, view_state);

    glfwSwapBuffers(window);
  }

  // Cleanup
  sync.quit.store(true);
  gathering_thread.join();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
