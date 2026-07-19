#include "views/replay_controls.h"

#include "playback/player.h"
#include "sync.h"
#include "views/common.h"
#include "views/ui.h"
#include "views/view_state.h"

#include "imgui.h"

#include <cfloat>
#include <cstring>
#include <mutex>

static constexpr float SPEED_VALUES[] = {0.5f, 1.0f,  2.0f,
                                         5.0f, 10.0f, PLAYBACK_SPEED_MAX};
static const char *SPEED_LABELS[] = {"0.5x", "1x", "2x", "5x", "10x", "Max"};
static const char *REPLAY_DIALOG_TITLE = "Replay a recording";

static void notify_playback(Sync &sync) {
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
  }
  sync.quit_cv.notify_one();
}

void replay_toggle_pause(Sync &sync) {
  sync.playback.paused.store(!sync.playback.paused.load());
  notify_playback(sync);
}

void replay_open_dialog_draw(ViewState &view_state) {
  ReplayViewState &rs = view_state.replay_state;
  if (rs.show_open_dialog) {
    rs.show_open_dialog = false;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
      ImGui::OpenPopup(REPLAY_DIALOG_TITLE);
    }
  }

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(480 * ui_scale(), 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(REPLAY_DIALOG_TITLE, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (popup_close_on_escape()) {
    ImGui::EndPopup();
    return;
  }

  ImGui::TextUnformatted("Path to a .prck recording:");
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool entered = ImGui::InputTextWithHint(
      "##ReplayPath", "/path/to/recording.prck", rs.open_path,
      sizeof(rs.open_path), ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::TextDisabled("Loads in a fresh replay session.");
  ImGui::Spacing();

  const bool has_path = rs.open_path[0] != '\0';
  ImGui::BeginDisabled(!has_path);
  const bool open = ImGui::Button("Replay") || (entered && has_path);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

  if (open && has_path) {
    rs.launch_request = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void replay_overlay_draw(ViewState &view_state) {
  Sync &sync = *view_state.sync;
  if (!sync.replay_mode) return;
  ReplayViewState &rs = view_state.replay_state;

  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const float pad = 8.0f * ui_scale();
  // Default at the bottom-center (a transport bar's usual home); FirstUseEver,
  // not Always, so the user can drag it elsewhere.
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                 vp->WorkPos.y + vp->WorkSize.y - pad),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
  if (ImGui::Begin("##ReplayOverlay", nullptr, flags)) {
    const char *name = rs.active_path ? rs.active_path : "";
    if (const char *slash = strrchr(name, '/')) name = slash + 1;
    ImGui::TextUnformatted(name[0] ? name : "replay");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    const bool paused = sync.playback.paused.load();
    if (ImGui::Button(paused ? "Play" : "Pause")) {
      replay_toggle_pause(sync);
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(64.0f * ui_scale());
    if (ImGui::Combo("##speed", &rs.speed_index, SPEED_LABELS,
                     IM_ARRAYSIZE(SPEED_LABELS))) {
      sync.playback.speed.store(SPEED_VALUES[rs.speed_index]);
      notify_playback(sync);
    }
    ImGui::SameLine();

    if (ImGui::Button("Restart")) {
      sync.playback.restart.store(true);
      sync.playback.paused.store(false);
      rs.reset_history_request = true; // drop the previous pass's view history
      notify_playback(sync);
    }

    if (sync.playback.finished.load()) {
      ImGui::SameLine();
      ImGui::TextDisabled("- finished");
    }
  }
  ImGui::End();
}
