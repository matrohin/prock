#include "username.h"

#include <cstdio>
#include <grp.h>
#include <pwd.h>

const char *resolve_uid_name(const uint32_t uid, char *buf,
                             const size_t buf_size) {
  passwd pw;
  passwd *result = nullptr;
  if (getpwuid_r(uid, &pw, buf, buf_size, &result) == 0 && result) {
    return pw.pw_name;
  }
  snprintf(buf, buf_size, "%u", uid);
  return buf;
}

const char *resolve_gid_name(const uint32_t gid, char *buf,
                             const size_t buf_size) {
  group gr;
  group *result = nullptr;
  if (getgrgid_r(gid, &gr, buf, buf_size, &result) == 0 && result) {
    return gr.gr_name;
  }
  snprintf(buf, buf_size, "%u", gid);
  return buf;
}

PersistentString UsernameResolver::resolve(const uint32_t uid) {
  if (const PersistentString cached = cache.get(uid); cached.data)
    return cached;

  char buf[UID_NAME_BUF_SIZE];
  return cache.set(uid, resolve_uid_name(uid, buf, sizeof(buf)));
}
