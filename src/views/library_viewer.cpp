#include "library_viewer.h"

#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>


void library_viewer_request(LibraryViewerState &state, Sync &sync, int pid, const char *comm) {
  // Check if window exists for pid - reopen if found
  for (size_t i = 0; i < state.windows.size(); ++i) {
    if (state.windows.data()[i].pid == pid) {
      state.windows.data()[i].open = true;
      return;
    }
  }

  // Create new window
  LibraryViewerWindow *win = state.windows.emplace_back(state.cur_arena, state.wasted_bytes);
  win->open = true;
  win->status = eLibraryViewerStatus_Loading;
  win->pid = pid;
  strncpy(win->process_name, comm, sizeof(win->process_name) - 1);
  win->process_name[sizeof(win->process_name) - 1] = '\0';
  win->libraries = {};
  win->error_code = 0;
  win->error_message[0] = '\0';

  LibraryRequest req = {pid};
  sync.library_request_queue.push(req);
  sync.library_cv.notify_one();
}


void library_viewer_update(LibraryViewerState &state, Sync &sync) {
  // Process responses
  LibraryResponse response;
  while (sync.library_response_queue.pop(response)) {
    for (size_t i = 0; i < state.windows.size(); ++i) {
      LibraryViewerWindow &win = state.windows.data()[i];
      if (win.pid == response.pid) {
        if (response.error_code == 0) {
          win.status = eLibraryViewerStatus_Ready;
          // Copy libraries to our arena
          win.libraries = Array<LibraryEntry>::create(state.cur_arena, response.libraries.size);
          memcpy(win.libraries.data, response.libraries.data,
                 response.libraries.size * sizeof(LibraryEntry));
        } else {
          win.status = eLibraryViewerStatus_Error;
          win.error_code = response.error_code;
          snprintf(win.error_message, sizeof(win.error_message),
                   "Error: %s", strerror(response.error_code));
        }
        response.owner_arena.destroy();
        break;
      }
    }
  }

  // Compact arena if wasted too much
  if (state.wasted_bytes > SLAB_SIZE) {
    BumpArena old_arena = state.cur_arena;
    BumpArena new_arena = BumpArena::create();

    state.windows.realloc(new_arena);
    for (size_t i = 0; i < state.windows.size(); ++i) {
      LibraryViewerWindow &win = state.windows.data()[i];
      if (win.libraries.size > 0) {
        Array<LibraryEntry> new_libs = Array<LibraryEntry>::create(
            new_arena, win.libraries.size);
        memcpy(new_libs.data, win.libraries.data,
               win.libraries.size * sizeof(LibraryEntry));
        win.libraries = new_libs;
      }
    }

    state.cur_arena = new_arena;
    state.wasted_bytes = 0;
    old_arena.destroy();
  }
}


void library_viewer_draw(ViewState &view_state, const State &state) {
  LibraryViewerState &my_state = view_state.library_viewer_state;
  size_t last = 0;

  for (size_t i = 0; i < my_state.windows.size(); ++i) {
    if (last != i) {
      my_state.windows.data()[last] = my_state.windows.data()[i];
    }
    LibraryViewerWindow &win = my_state.windows.data()[last];
    if (!win.open) {
      my_state.wasted_bytes += sizeof(LibraryViewerWindow);
      my_state.wasted_bytes += win.libraries.size * sizeof(LibraryEntry);
      continue;
    }

    char title[128];
    snprintf(title, sizeof(title), "Libraries: %s (%d)", win.process_name, win.pid);
    view_state.cascade.next_if_new(title);

    if (ImGui::Begin(title, &win.open, COMMON_VIEW_FLAGS)) {
      // Title line: status/count + refresh controls
      bool loading = (win.status == eLibraryViewerStatus_Loading);

      if (loading) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Refresh")) {
        win.status = eLibraryViewerStatus_Loading;
        LibraryRequest req = {win.pid};
        view_state.sync->library_request_queue.push(req);
        view_state.sync->library_cv.notify_one();
      }
      if (loading) {
        ImGui::EndDisabled();
      }

      // Status/count text
      ImGui::SameLine();
      if (win.status == eLibraryViewerStatus_Error) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error");
      } else if (loading && win.libraries.size > 0) {
        ImGui::Text("%zu libraries (updating...)", win.libraries.size);
      } else if (loading) {
        ImGui::Text("Loading...");
      } else {
        ImGui::Text("%zu libraries", win.libraries.size);
      }

      ImGui::Separator();

      // Content area - show previous data while loading, or error message
      if (win.status == eLibraryViewerStatus_Error) {
        ImGui::TextWrapped("%s", win.error_message);
        if (win.error_code == EACCES) {
          if (ImGui::Button("Restart with pkexec")) {
            execlp("pkexec", "pkexec", "/proc/self/exe", nullptr);
          }
        }
      } else if (win.libraries.size > 0) {
        if (ImGui::BeginChild("List")) {
          for (size_t j = 0; j < win.libraries.size; ++j) {
            ImGui::Text("%s", win.libraries.data[j].path);
          }
        }
        ImGui::EndChild();
      }
    }
    ImGui::End();
    ++last;
  }
  my_state.windows.shrink_to(last);
}
