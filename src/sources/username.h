#pragma once

#include "base/const_string.h"

#include <cstddef>
#include <cstdint>

// Resolve a uid/gid to its name via the reentrant (thread-safe) libc lookups,
// falling back to the decimal id when there is no matching entry. The result
// points into the caller-provided buffer, so copy it out before buf goes out of
// scope.
//
// Size buf with the constants below: passwd entries are small, while a group
// can carry a large member list and needs more room. An undersized buffer
// (ERANGE) also yields the numeric fallback.
constexpr size_t UID_NAME_BUF_SIZE = 1024;
constexpr size_t GID_NAME_BUF_SIZE = 4096;

const char *resolve_uid_name(uint32_t uid, char *buf, size_t buf_size);
const char *resolve_gid_name(uint32_t gid, char *buf, size_t buf_size);

struct UsernameResolver {
  StringCache cache; // uid -> username

  static UsernameResolver create(BumpArena *arena) {
    return UsernameResolver{StringCache::create(arena)};
  }

  PersistentString resolve(uint32_t uid);
};
