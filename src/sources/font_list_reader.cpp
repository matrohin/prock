#include "font_list_reader.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

static void filename_to_name(const char *path, char *out, size_t out_sz) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  const char *dot = strrchr(base, '.');
  const size_t raw_len = dot ? (size_t)(dot - base) : strlen(base);
  const size_t len = raw_len < out_sz - 1 ? raw_len : out_sz - 1;
  for (size_t i = 0; i < len; i++) {
    const char c = base[i];
    out[i] = (c == '_' || c == '-') ? ' ' : c;
  }
  out[len] = '\0';
}

static void scan_dir(const char *dir_path, BumpArena &temp_arena,
                     GrowingArray<FontEntry> &entries) {
  DIR *dir = opendir(dir_path);
  if (!dir) return;

  struct dirent *ent;
  while ((ent = readdir(dir)) != nullptr) {
    if (ent->d_name[0] == '.') continue;

    char sub_path[PATH_MAX];
    const int path_written =
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, ent->d_name);
    if (path_written < 0 ||
        static_cast<size_t>(path_written) >= sizeof(sub_path))
      continue;

    unsigned char d_type = ent->d_type;
    if (d_type == DT_LNK || d_type == DT_UNKNOWN) {
      struct stat st;
      if (stat(sub_path, &st) != 0) continue;
      if (S_ISDIR(st.st_mode))
        d_type = DT_DIR;
      else if (S_ISREG(st.st_mode))
        d_type = DT_REG;
      else
        continue;
    }

    if (d_type == DT_DIR) {
      scan_dir(sub_path, temp_arena, entries);
    } else if (d_type == DT_REG) {
      const size_t name_len = strlen(ent->d_name);
      if (name_len < 5) continue;
      if (strcasecmp(ent->d_name + name_len - 4, ".ttf") != 0) continue;

      FontEntry *entry = entries.emplace_back(temp_arena);
      const int written =
          snprintf(entry->path, sizeof(entry->path), "%s", sub_path);
      if (written < 0 || static_cast<size_t>(written) >= sizeof(entry->path)) {
        entries.shrink_to(entries.size() - 1);
        continue;
      }
      filename_to_name(entry->path, entry->name, sizeof(entry->name));
    }
  }
  closedir(dir);
}

static void scan_xdg_base(const char *base_dir, BumpArena &temp_arena,
                          GrowingArray<FontEntry> &entries) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/fonts", base_dir);
  scan_dir(path, temp_arena, entries);
}

FontListResponse read_font_list(BumpArena &temp_arena) {
  FontListResponse response = {};
  response.owner_arena = BumpArena::create();

  GrowingArray<FontEntry> entries = {};

  const char *home = getenv("HOME");
  const char *xdg_data_home = getenv("XDG_DATA_HOME");

  if (xdg_data_home && xdg_data_home[0]) {
    scan_xdg_base(xdg_data_home, temp_arena, entries);
  } else if (home) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.local/share", home);
    scan_xdg_base(path, temp_arena, entries);
  }

  const char *xdg_dirs = getenv("XDG_DATA_DIRS");
  const char *dirs_str =
      (xdg_dirs && xdg_dirs[0]) ? xdg_dirs : "/usr/local/share:/usr/share";
  char dirs_buf[4096];
  snprintf(dirs_buf, sizeof(dirs_buf), "%s", dirs_str);
  char *token = strtok(dirs_buf, ":");
  while (token) {
    size_t tlen = strlen(token);
    while (tlen > 1 && token[tlen - 1] == '/')
      token[--tlen] = '\0';
    scan_xdg_base(token, temp_arena, entries);
    token = strtok(nullptr, ":");
  }

  if (home) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.fonts", home);
    scan_dir(path, temp_arena, entries);
  }

  std::sort(entries.begin(), entries.end(),
            [](const FontEntry &a, const FontEntry &b) {
              return strcasecmp(a.name, b.name) < 0;
            });

  response.fonts = Array<FontEntry>::copy_from(response.owner_arena,
                                               entries.data(), entries.size());
  return response;
}
