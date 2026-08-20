#pragma once

#include "base/string.h"

// Middle-elides `path` so it renders in at most `max_chars` characters
String path_elide(BumpArena &arena, const char *path, uint32_t len,
                  uint32_t max_chars);
