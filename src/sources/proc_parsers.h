#pragma once

#include "process_stat.h"

// Parse /proc/[pid]/stat and /proc/[pid]/statm content.
// Sets: state, ppid, utime, stime, nice, num_threads, starttime, vsize,
// last_cpu, statm_resident.
// Does NOT set comm, cmdline, pid, wchan, or io fields.
// Returns false if stat_buf is malformed (no closing paren).
bool parse_proc_stat_bufs(const char *stat_buf, const char *statm_buf,
                          ProcessStat *out);

// Parse a single line from /proc/[pid]/io (format: "key: value").
// Sets io_read_bytes or io_write_bytes if the key matches. Other fields
// untouched.
void parse_io_line(const char *line, ProcessStat *out);

// Read /proc/[pid]/comm into out (null-terminated, trailing newline stripped).
// Sets out to an empty string on failure.
void read_proc_comm(Pid pid, char *out, size_t out_size);

// Read /proc/[pid]/cmdline into out with args space-joined and trailing
// separators trimmed (null-terminated). Returns the length, 0 on failure or
// empty cmdline.
size_t read_proc_cmdline(Pid pid, char *out, size_t out_size);

// Basename of the first argument of a space-joined cmdline. Returns nullptr
// (len untouched) if cmdline is empty or has no useful basename.
const char *cmdline_basename(const char *cmdline, uint32_t *len);

// Display name a process is listed under: basename of the first cmdline
// argument, comm as fallback. Sets out to an empty string on failure.
void read_proc_display_name(Pid pid, char *out, size_t out_size);

// Parse the real uid from the "Uid:" line of a /proc/[pid]/status buffer.
// Returns false (leaving *out untouched) if not found.
bool parse_proc_status_uid(const char *status_buf, uid_t *out);
