#include "actions/elevate.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <linux/limits.h>
#include <unistd.h>

bool g_borrowed_config = false;

bool invoking_user_uid(uid_t &out) {
  const char *uid_str = getenv("PKEXEC_UID");
  if (!uid_str) uid_str = getenv("SUDO_UID");
  if (!uid_str) return false;
  char *end = nullptr;
  const unsigned long uid = strtoul(uid_str, &end, 10);
  if (end == uid_str || *end != '\0') return false;
  out = static_cast<uid_t>(uid);
  return true;
}

void restart_with_pkexec() {
  char exe_path[PATH_MAX];
  const ssize_t len =
      readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) return;
  exe_path[len] = '\0';

  constexpr size_t count = std::size(DISPLAY_ENV_VARS);
  char env_args[count][512];
  // pkexec, the binary, a --display-env pair per variable, and the terminator.
  const char *args[3 + 2 * count] = {"pkexec", exe_path};
  size_t arg_idx = 2;
  for (size_t i = 0; i < count; ++i) {
    const char *val = getenv(DISPLAY_ENV_VARS[i]);
    if (!val) continue;
    const int n = snprintf(env_args[i], sizeof(env_args[i]), "%s=%s",
                           DISPLAY_ENV_VARS[i], val);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(env_args[i])) continue;
    args[arg_idx++] = "--display-env";
    args[arg_idx++] = env_args[i];
  }
  args[arg_idx] = nullptr;
  execvp("pkexec", const_cast<char *const *>(args));
}

// The name whitelist keeps this from becoming a way to set any variable at all
// in an elevated process. Values need no check: getting here as root means
// pkexec already authorized one on the caller's display.
bool apply_display_env(const char *assignment) {
  const char *eq = strchr(assignment, '=');
  if (!eq || eq == assignment) return false;
  const size_t name_len = static_cast<size_t>(eq - assignment);
  for (const char *name : DISPLAY_ENV_VARS) {
    if (strlen(name) != name_len || strncmp(assignment, name, name_len) != 0) {
      continue;
    }
    setenv(name, eq + 1, 1);
    return true;
  }
  return false;
}
