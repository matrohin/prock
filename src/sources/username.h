#pragma once

#include "base/const_string.h"

struct UsernameResolver {
  StringCache cache; // uid -> username

  static UsernameResolver create(BumpArena *arena) {
    return UsernameResolver{StringCache::create(arena)};
  }

  PersistentString resolve(uint32_t uid);
};
