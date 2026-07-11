#pragma once

#include "base/base.h"

struct FontEntry {
  char name[128];
  char path[512];
};

struct FontListRequest {};

struct FontListResponse {
  BumpArena owner_arena;
  Array<FontEntry> fonts;
};

FontListResponse font_list_reader_read(BumpArena &temp_arena);
