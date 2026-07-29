/// This whole mechanism will get rewritten to a separate helper later

#pragma once

#include <sys/types.h>

inline constexpr const char *DISPLAY_ENV_VARS[] = {
    "DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XAUTHORITY"};

// Set once at startup.
extern bool g_borrowed_config;

// Uid of the user who launched an elevated session, as pkexec/sudo report it.
bool invoking_user_uid(uid_t &out);

// Re-exec the app as root through pkexec. Returns only on failure.
void restart_with_pkexec();

// Apply one NAME=VALUE pair from --display-env.
bool apply_display_env(const char *assignment);
