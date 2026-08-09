#pragma once

#include "base/base.h"
#include "state/state.h"
#include "sync.h"
#include "views/view_state.h"

#include "GLFW/glfw3.h"

struct AppParams {
  const char *replay_path; // static storage required, can be null
  const char *config_path; // static storage required, can be null
  bool is_config_borrowed;
  bool hidden_window;
};

struct App {
  Sync sync;
  BumpArena arena;
  GLFWwindow *window;
  std::thread producer_thread;
  std::thread proc_reader_thread;
  std::thread actions_thread;
  std::thread recorder_thread;

  ViewState view_state;
  State state;
};

App *app_create(const AppParams &params);
FrameContext app_start_frame(App *app);
void app_update(App *app, FrameContext &frame_ctx);
void app_draw(App *app, FrameContext &frame_ctx);
void app_render(App *app);
void app_end_frame(FrameContext &frame_ctx);
void app_destroy(App *app);

inline void app_swap_buffers(App *app) { glfwSwapBuffers(app->window); }

inline bool app_should_close(App *app) {
  return glfwWindowShouldClose(app->window);
}