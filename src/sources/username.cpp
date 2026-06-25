#include "username.h"

#include <cstdio>
#include <pwd.h>

PersistentString UsernameResolver::resolve(const uint32_t uid) {
  if (const PersistentString cached = cache.get(uid); cached.data) return cached;

  char buf[32];
  const char *name;
  if (const passwd *pw = getpwuid(uid)) {
    name = pw->pw_name;
  } else {
    snprintf(buf, sizeof(buf), "%u", uid);
    name = buf;
  }
  return cache.set(uid, name);
}
