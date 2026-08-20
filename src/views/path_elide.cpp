#include "path_elide.h"

static const char *ELLIPSIS = "…";
static constexpr uint32_t ELLIPSIS_CHARS = 1;
// "/…/" between the kept leading components and the basename
static constexpr uint32_t SEPARATOR_CHARS = ELLIPSIS_CHARS + 2;

String path_elide(BumpArena &arena, const char *path, const uint32_t len,
                  const uint32_t max_chars) {
  if (len <= max_chars) return String{path, len};
  if (max_chars < ELLIPSIS_CHARS) return String{"", 0};

  const char *last_slash = strrchr(path, '/');

  // Not a path ("[heap]", "anon_inode:[eventpoll]", ...)
  if (!last_slash) {
    const uint32_t budget = max_chars - ELLIPSIS_CHARS;
    const uint32_t tail = budget / 2;
    const uint32_t head = budget - tail;
    return String::sprintf(arena, "%.*s%s%.*s", static_cast<int>(head), path,
                           ELLIPSIS, static_cast<int>(tail), path + len - tail);
  }

  const uint32_t tail_start = static_cast<uint32_t>(last_slash + 1 - path);
  const uint32_t tail_len = len - tail_start;

  // Not even "…/" plus the basename fits
  if (tail_len + ELLIPSIS_CHARS + 1 > max_chars) {
    const uint32_t keep = max_chars - ELLIPSIS_CHARS;
    return String::sprintf(arena, "%s%.*s", ELLIPSIS, static_cast<int>(keep),
                           path + len - keep);
  }

  // Largest leading run of whole components that fits
  const uint32_t head_budget = max_chars > tail_len + SEPARATOR_CHARS
                                   ? max_chars - tail_len - SEPARATOR_CHARS
                                   : 0;
  uint32_t head_len = 0;
  for (uint32_t i = 1; i + 1 < tail_start; ++i) {
    if (path[i] == '/' && i <= head_budget) head_len = i;
  }

  if (head_len == 0) {
    return String::sprintf(arena, "%s/%s", ELLIPSIS, path + tail_start);
  }
  return String::sprintf(arena, "%.*s/%s/%s", static_cast<int>(head_len), path,
                         ELLIPSIS, path + tail_start);
}
