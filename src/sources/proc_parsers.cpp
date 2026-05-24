#include "proc_parsers.h"

#include <cstdio>
#include <cstring>

bool parse_proc_stat_bufs(const char *stat_buf, const char *statm_buf,
                          ProcessStat *out) {
  const char *after_comm = strrchr(stat_buf, ')');
  if (!after_comm) {
    return false;
  }

  sscanf(after_comm + 1,
         " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d "
         "%*d %*d %ld %*d %*u %lu",
         &out->state, &out->ppid, &out->utime, &out->stime, &out->num_threads,
         &out->vsize);

  sscanf(statm_buf, "%*u %lu", &out->statm_resident);

  return true;
}

void parse_io_line(const char *line, ProcessStat *out) {
  char key[32];
  ulonglong value;
  if (sscanf(line, "%31[^:]: %llu", key, &value) == 2) {
    if (strcmp(key, "read_bytes") == 0) {
      out->io_read_bytes = value;
    } else if (strcmp(key, "write_bytes") == 0) {
      out->io_write_bytes = value;
    }
  }
}
