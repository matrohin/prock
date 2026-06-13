#include "smaps_reader.h"

#include "tracy/Tracy.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

SmapsResponse read_process_smaps(BumpArena &temp_arena,
                                 const SmapsRequest &request) {
  ZoneScoped;
  const Pid pid = request.pid;

  SmapsResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/smaps", pid);
  FILE *f = fopen(path, "r");
  if (!f) {
    response.error_code = errno;
    return response;
  }

  GrowingArray<SmapsSegment> segments = {};
  SmapsSegment *cur = nullptr;
  char line[1024];

  while (fgets(line, sizeof(line), f)) {
    ulong start_addr = 0, end_addr = 0, offset = 0;
    uint dev_maj = 0, dev_min = 0;
    ulong inode = 0;
    char perms[8] = {};
    int chars_read = 0;

    if (sscanf(line, "%lx-%lx %7s %lx %x:%x %lu%n", &start_addr, &end_addr,
               perms, &offset, &dev_maj, &dev_min, &inode, &chars_read) == 7) {
      cur = segments.emplace_back(temp_arena);
      cur->start_addr = start_addr;
      cur->end_addr = end_addr;
      memcpy(cur->perms, perms, sizeof(cur->perms));

      const char *p = line + chars_read;
      while (*p == ' ')
        p++;
      uint32_t plen = static_cast<uint32_t>(strlen(p));
      while (plen > 0 && (p[plen - 1] == '\n' || p[plen - 1] == '\r'))
        plen--;
      if (plen > 0) {
        cur->path = String::copy_from(response.owner_arena, p, plen);
      }
      continue;
    }

    if (!cur) continue;

    char key[64] = {};
    ulong value = 0;
    if (sscanf(line, "%63[^:]: %lu kB", key, &value) == 2) {
      if (strcmp(key, "Size") == 0)
        cur->size_kb = value;
      else if (strcmp(key, "Rss") == 0)
        cur->rss_kb = value;
      else if (strcmp(key, "Pss") == 0)
        cur->pss_kb = value;
      else if (strcmp(key, "Shared_Clean") == 0)
        cur->shared_clean_kb = value;
      else if (strcmp(key, "Shared_Dirty") == 0)
        cur->shared_dirty_kb = value;
      else if (strcmp(key, "Private_Clean") == 0)
        cur->private_clean_kb = value;
      else if (strcmp(key, "Private_Dirty") == 0)
        cur->private_dirty_kb = value;
      else if (strcmp(key, "Swap") == 0)
        cur->swap_kb = value;
      else if (strcmp(key, "SwapPss") == 0)
        cur->swap_pss_kb = value;
    }
  }

  fclose(f);
  response.segments = Array<SmapsSegment>::copy_from(
      response.owner_arena, segments.data(), segments.size());
  return response;
}
