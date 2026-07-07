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

size_t read_proc_cmdline(const Pid pid, char *out, const size_t out_size) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
  FILE *f = fopen(path, "r");
  if (!f) {
    out[0] = '\0';
    return 0;
  }
  size_t len = fread(out, 1, out_size - 1, f);
  fclose(f);
  for (size_t i = 0; i < len; ++i) {
    if (out[i] == '\0') out[i] = ' ';
  }
  while (len > 0 && out[len - 1] == ' ')
    --len;
  out[len] = '\0';
  return len;
}

const char *cmdline_basename(const char *cmdline, uint32_t *len) {
  if (!cmdline || !cmdline[0]) return nullptr;
  const char *end = cmdline;
  while (*end && *end != ' ')
    ++end;
  const char *base = cmdline;
  for (const char *p = cmdline; p < end; ++p)
    if (*p == '/') base = p + 1;
  if (base == end) return nullptr;
  *len = static_cast<uint32_t>(end - base);
  return base;
}

void read_proc_display_name(const Pid pid, char *out, const size_t out_size) {
  char cmdline[4096];
  read_proc_cmdline(pid, cmdline, sizeof(cmdline));
  uint32_t len = 0;
  const char *base = cmdline_basename(cmdline, &len);
  if (!base) {
    read_proc_comm(pid, out, out_size);
    return;
  }
  if (len >= out_size) len = static_cast<uint32_t>(out_size) - 1;
  memcpy(out, base, len);
  out[len] = '\0';
}

bool parse_proc_stat_bufs(const char *stat_buf, const char *statm_buf,
                          ProcessStat *out) {
  const char *after_comm = strrchr(stat_buf, ')');
  if (!after_comm) {
    return false;
  }

  sscanf(after_comm + 1,
         " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d "
         "%*d %ld %ld %*d %llu %lu "
         // skip rss..exit_signal (fields 24-38) to reach processor (field 39)
         "%*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %d",
         &out->state, &out->ppid, &out->utime, &out->stime, &out->nice,
         &out->num_threads, &out->starttime, &out->vsize, &out->last_cpu);

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
