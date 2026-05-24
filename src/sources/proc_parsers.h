#pragma once

#include "process_stat.h"

// Parse /proc/[pid]/stat and /proc/[pid]/statm content.
// Sets: state, ppid, utime, stime, num_threads, vsize, statm_resident.
// Does NOT set comm, cmdline, pid, or io fields.
// Returns false if stat_buf is malformed (no closing paren).
bool parse_proc_stat_bufs(const char *stat_buf, const char *statm_buf,
                          ProcessStat *out);

// Parse a single line from /proc/[pid]/io (format: "key: value").
// Sets io_read_bytes or io_write_bytes if the key matches. Other fields untouched.
void parse_io_line(const char *line, ProcessStat *out);
