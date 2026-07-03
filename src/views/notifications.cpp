#include "views/notifications.h"

#include "themes.h"
#include "views/common.h"
#include "views/view_state.h"

#include "imgui.h"

#include <cfloat>
#include <cstdarg>

constexpr double NOTIFY_TTL_SECONDS = 8.0;
constexpr double NOTIFY_INFO_TTL_SECONDS = 4.0;
constexpr float NOTIFY_WIDTH = 320.0f;
constexpr float NOTIFY_EDGE_PAD = 12.0f;
constexpr float NOTIFY_STACK_GAP = 8.0f;

static ImVec4 severity_color(const NotificationSeverity severity) {
  switch (severity) {
  case eNotificationSeverity_Error:
    return g_app_colors[eAppColor_ErrorText];
  case eNotificationSeverity_Warning:
    return g_app_colors[eAppColor_WarningText];
  case eNotificationSeverity_Info:
  default:
    return g_app_colors[eAppColor_InfoText];
  }
}

static const char *severity_label(const NotificationSeverity severity) {
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

static bool is_expired(const Notification &note, const double now) {
  if (note.sticky) {
    return false;
  }
  const double ttl = note.severity == eNotificationSeverity_Info
                         ? NOTIFY_INFO_TTL_SECONDS
                         : NOTIFY_TTL_SECONDS;
  return now - note.created_time > ttl;
}

void notifications_vpush_action(Notifications &notifications,
                                const NotificationSeverity severity,
                                const char *action_label,
                                void (*action_fn)(const void *user_data),
                                const void *action_data, const char *fmt,
                                va_list args) {
  const uint32_t idx = notifications.track.emplace_back();
  Notification &note = notifications.items[idx];
  note = {};
  note.severity = severity;
  note.created_time = ImGui::GetTime();
  note.id = ++notifications.next_id;
  note.action_fn = action_fn;
  note.action_data = action_data;
  note.action_label = action_fn
                          ? String::copy_from(notifications.arena, action_label)
                          : String{};
  note.text = String::vsprintf(notifications.arena, fmt, args);
}

uint64_t notifications_push_progress(Notifications &notifications,
                                     const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  notifications_vpush_action(notifications, eNotificationSeverity_Info, nullptr,
                             nullptr, nullptr, fmt, args);
  va_end(args);
  Notification &note = notifications.items[notifications.track.last_idx()];
  note.sticky = true;
  return note.id;
}

void notifications_remove(Notifications &notifications, const uint64_t id) {
  RingTrack<Notifications::CAP> &track = notifications.track;
  uint32_t write = 0;
  for (uint32_t read = 0; read < track.size; ++read) {
    const Notification &note = notifications.items[track.to_data_idx(read)];
    if (note.id == id) {
      continue;
    }
    if (write != read) {
      notifications.items[track.to_data_idx(write)] = note;
    }
    ++write;
  }
  track.size = write;
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

  const float scale = ui_scale();
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float right =
      viewport->WorkPos.x + viewport->WorkSize.x - NOTIFY_EDGE_PAD * scale;
  float bottom =
      viewport->WorkPos.y + viewport->WorkSize.y - NOTIFY_EDGE_PAD * scale;

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
    ImGui::SetNextWindowSizeConstraints(ImVec2(NOTIFY_WIDTH * scale, 0.0f),
                                        ImVec2(NOTIFY_WIDTH * scale, FLT_MAX));

    if (ImGui::Begin(window_id.data, nullptr, flags)) {
      const ImVec2 header_pos = ImGui::GetCursorScreenPos();
      const float header_width = ImGui::GetContentRegionAvail().x;
      ImGui::TextColored(severity_color(note.severity), "%s",
                         note.sticky ? "Working"
                                     : severity_label(note.severity));

      // Sticky progress entries are removed when their action completes, so
      // they have no manual close button.
      if (!note.sticky) {
        const float close_width =
            ImGui::CalcTextSize("x").x + style.FramePadding.x * 2.0f;
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(
            ImVec2(header_pos.x + header_width - close_width, header_pos.y));
        if (ImGui::SmallButton("x")) {
          note.created_time = -100;
        }
      }

      ImGui::TextWrapped("%s", note.text.data);

      if (note.action_fn && ImGui::SmallButton(note.action_label.data)) {
        note.action_fn(note.action_data);
      }

      bottom = ImGui::GetWindowPos().y - NOTIFY_STACK_GAP * scale;
    }
    ImGui::End();
  }
}
