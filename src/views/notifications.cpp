#include "views/notifications.h"

#include "views/view_state.h"

#include "imgui.h"

#include <cfloat>
#include <cstdarg>

namespace {

constexpr double NOTIFY_TTL_SECONDS = 8.0;
constexpr float NOTIFY_WIDTH = 320.0f;
constexpr float NOTIFY_EDGE_PAD = 12.0f;
constexpr float NOTIFY_STACK_GAP = 8.0f;

ImVec4 severity_color(const NotificationSeverity severity) {
  switch (severity) {
  case eNotificationSeverity_Error:
    return ImVec4(0.92f, 0.36f, 0.32f, 1.0f);
  case eNotificationSeverity_Warning:
    return ImVec4(0.95f, 0.73f, 0.25f, 1.0f);
  case eNotificationSeverity_Info:
  default:
    return ImVec4(0.40f, 0.66f, 0.96f, 1.0f);
  }
}

const char *severity_label(const NotificationSeverity severity) {
  switch (severity) {
  case eNotificationSeverity_Error:
    return "Error";
  case eNotificationSeverity_Warning:
    return "Warning";
  case eNotificationSeverity_Info:
  default:
    return "Info";
  }
}

bool is_expired(const Notification &note, const double now) {
  return now - note.created_time > NOTIFY_TTL_SECONDS;
}

} // namespace

void notifications_vpush_action(Notifications &notifications,
                                const NotificationSeverity severity,
                                const char *action_label, void (*action_fn)(),
                                const char *fmt, va_list args) {
  const uint32_t idx = notifications.track.emplace_back();
  Notification &note = notifications.items[idx];
  note = {};
  note.severity = severity;
  note.created_time = ImGui::GetTime();
  note.id = ++notifications.next_id;
  note.action_fn = action_fn;
  note.action_label = action_fn
                          ? String::copy_from(notifications.arena, action_label)
                          : String{};
  note.text = String::vsprintf(notifications.arena, fmt, args);
}

void notifications_update(Notifications &notifications) {
  RingTrack<Notifications::CAP> &track = notifications.track;
  const double now = ImGui::GetTime();

  uint32_t write = 0;
  for (uint32_t read = 0; read < track.size; ++read) {
    const Notification &note = notifications.items[track.to_data_idx(read)];
    if (is_expired(note, now)) {
      continue;
    }
    if (write != read) {
      notifications.items[track.to_data_idx(write)] = note;
    }
    ++write;
  }
  track.size = write;
  if (track.size == 0) {
    notifications.arena.destroy();
  }
}

void notifications_draw(FrameContext &ctx, Notifications &notifications) {
  RingTrack<Notifications::CAP> &track = notifications.track;
  if (track.size == 0) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float right =
      viewport->WorkPos.x + viewport->WorkSize.x - NOTIFY_EDGE_PAD;
  float bottom = viewport->WorkPos.y + viewport->WorkSize.y - NOTIFY_EDGE_PAD;

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_AlwaysAutoResize;
  const ImGuiStyle &style = ImGui::GetStyle();

  for (uint32_t i = track.size; i-- > 0;) {
    Notification &note = notifications.items[track.to_data_idx(i)];

    const String window_id =
        String::sprintf(ctx.frame_arena, "##notification_%llu",
                        static_cast<unsigned long long>(note.id));

    ImGui::SetNextWindowPos(ImVec2(right, bottom), ImGuiCond_Always,
                            ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(NOTIFY_WIDTH, 0.0f),
                                        ImVec2(NOTIFY_WIDTH, FLT_MAX));

    if (ImGui::Begin(window_id.data, nullptr, flags)) {
      const ImVec2 header_pos = ImGui::GetCursorScreenPos();
      const float header_width = ImGui::GetContentRegionAvail().x;
      ImGui::TextColored(severity_color(note.severity), "%s",
                         severity_label(note.severity));

      const float close_width =
          ImGui::CalcTextSize("x").x + style.FramePadding.x * 2.0f;
      ImGui::SameLine();
      ImGui::SetCursorScreenPos(
          ImVec2(header_pos.x + header_width - close_width, header_pos.y));
      if (ImGui::SmallButton("x")) {
        note.created_time = -100;
      }

      ImGui::TextWrapped("%s", note.text.data);

      if (note.action_fn && ImGui::SmallButton(note.action_label.data)) {
        note.action_fn();
      }

      bottom = ImGui::GetWindowPos().y - NOTIFY_STACK_GAP;
    }
    ImGui::End();
  }
}
