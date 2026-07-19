#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <linux/limits.h>
#include <sys/stat.h>

void paths_default_out_dir(char *out, const uint32_t size, const char *subdir) {
  const char *home = getenv("HOME");
  if (home) {
    snprintf(out, size, "%s/prock/%s", home, subdir);
  } else {
    snprintf(out, size, "/tmp/prock/%s", subdir);
  }
}

void paths_ensure_parent_dir(const char *out_path) {
  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s", out_path);
  char *slash = strrchr(dir, '/');
  if (!slash || slash == dir) {
    return;
  }
  *slash = '\0';

  for (char *p = dir + 1; *p; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(dir, 0700); // ignore EEXIST and other errors
    *p = '/';
  }
  mkdir(dir, 0700);
}

void paths_format_time_suffix(char *out, const uint32_t size) {
  const time_t now = time(nullptr);
  tm tm_now;
  localtime_r(&now, &tm_now);
  strftime(out, size, "%Y%m%d-%H%M%S", &tm_now);
}
