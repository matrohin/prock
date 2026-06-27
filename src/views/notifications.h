#pragma once

#include "base/ring_track.h"
#include "base/string.h"

#include <cstdarg>

// Non-modal status messages stacked in the bottom-right corner, IntelliJ-style.
// They auto-expire so the user keeps working instead of dismissing a popup.
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
  bool sticky; // never auto-expires; removed explicitly via notifications_remove

  String action_label;
  void (*action_fn)(const void *user_data);
  const void *action_data;
};

struct Notifications {
  static constexpr uint32_t CAP = 8;
  Notification items[CAP];
  RingTrack<CAP> track;
  BumpArena arena;
  uint64_t next_id;
};

struct FrameContext;

// Push a message with an optional action button. A null action_fn degrades to a
// plain message; otherwise action_label (copied into the arena) invokes
// action_fn(action_data) when clicked. See notify_error in common.h for the
// usual entry.
void notifications_vpush_action(Notifications &notifications,
                                NotificationSeverity severity,
                                const char *action_label,
                                void (*action_fn)(const void *user_data),
                                const void *action_data, const char *fmt,
                                va_list args);

// Push a sticky "in progress" message that stays until notifications_remove is
// called with the returned id. Use it to track a long-running action and swap it
// for a result toast on completion.
uint64_t notifications_push_progress(Notifications &notifications,
                                     const char *fmt, ...);
void notifications_remove(Notifications &notifications, uint64_t id);

void notifications_update(Notifications &notifications);
void notifications_draw(FrameContext &ctx, Notifications &notifications);
