#pragma once

#include "base/ring_track.h"
#include "base/string.h"

enum NotificationSeverity {
  eNotificationSeverity_Info,
  eNotificationSeverity_Warning,
  eNotificationSeverity_Error,
};

struct Notification {
  NotificationSeverity severity;
  String text;
  double created_time;
  uint64_t id;

  String action_label;
  void (*action_fn)();
};

struct Notifications {
  static constexpr uint32_t CAP = 8;
  Notification items[CAP];
  RingTrack<CAP> track;
  BumpArena arena;
  uint64_t next_id;
};

struct FrameContext;

void notifications_push(Notifications &notifications,
                        NotificationSeverity severity, const char *fmt, ...);

void notifications_push_action(Notifications &notifications,
                               NotificationSeverity severity,
                               const char *action_label, void (*action_fn)(),
                               const char *fmt, ...);

void notifications_update(Notifications &notifications);
void notifications_draw(FrameContext &ctx, Notifications &notifications);
