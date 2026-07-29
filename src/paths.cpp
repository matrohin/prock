#include "paths.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <linux/limits.h>
#include <sys/stat.h>

void paths_default_out_dir(char *out, const uint32_t size, const char *subdir) {
  const char *home = getenv("HOME");
  if (!home) {
    out[0] = '\0';
    return;
  }
  const int n = snprintf(out, size, "%s/prock/%s", home, subdir);
  if (n < 0 || static_cast<uint32_t>(n) >= size) out[0] = '\0';
}

bool paths_ensure_parent_dir(const char *out_path) {
  char dir[PATH_MAX];
  const int n = snprintf(dir, sizeof(dir), "%s", out_path);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(dir)) {
    errno = ENAMETOOLONG;
    return false;
  }
  char *slash = strrchr(dir, '/');
  // Parent is the working directory or the root directory: both already exist.
  if (!slash || slash == dir) {
    return true;
  }
  *slash = '\0';

  for (char *p = dir + 1; *p; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return false;
    *p = '/';
  }
  return mkdir(dir, 0700) == 0 || errno == EEXIST;
}

void paths_format_time_suffix(char *out, const uint32_t size) {
  const time_t now = time(nullptr);
  tm tm_now;
  localtime_r(&now, &tm_now);
  strftime(out, size, "%Y%m%d-%H%M%S", &tm_now);
}
