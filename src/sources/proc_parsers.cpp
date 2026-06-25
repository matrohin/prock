#include "proc_parsers.h"

#include <cstdio>
#include <cstring>

void read_proc_comm(const Pid pid, char *out, const size_t out_size) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/comm", pid);
  FILE *f = fopen(path, "r");
  if (!f) {
    out[0] = '\0';
    return;
  }
  if (fgets(out, static_cast<int>(out_size), f)) {
    const size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
  } else {
    out[0] = '\0';
  }
  fclose(f);
}

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

bool parse_proc_status_uid(const char *status_buf, uid_t *out) {
  const char *line = strstr(status_buf, "Uid:");
  if (!line) return false;
  unsigned int real_uid;
  if (sscanf(line + 4, "%u", &real_uid) != 1) return false;
  *out = real_uid;
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
