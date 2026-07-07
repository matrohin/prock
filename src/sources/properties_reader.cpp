#include "properties_reader.h"

#include "base/base.h"
#include "base/string.h"
#include "proc_parsers.h"
#include "process_stat.h"
#include "username.h"

#include "tracy/Tracy.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

// Capability names indexed by bit number (see capabilities(7)).
static const char *CAP_NAMES[] = {"cap_chown",
                                  "cap_dac_override",
                                  "cap_dac_read_search",
                                  "cap_fowner",
                                  "cap_fsetid",
                                  "cap_kill",
                                  "cap_setgid",
                                  "cap_setuid",
                                  "cap_setpcap",
                                  "cap_linux_immutable",
                                  "cap_net_bind_service",
                                  "cap_net_broadcast",
                                  "cap_net_admin",
                                  "cap_net_raw",
                                  "cap_ipc_lock",
                                  "cap_ipc_owner",
                                  "cap_sys_module",
                                  "cap_sys_rawio",
                                  "cap_sys_chroot",
                                  "cap_sys_ptrace",
                                  "cap_sys_pacct",
                                  "cap_sys_admin",
                                  "cap_sys_boot",
                                  "cap_sys_nice",
                                  "cap_sys_resource",
                                  "cap_sys_time",
                                  "cap_sys_tty_config",
                                  "cap_mknod",
                                  "cap_lease",
                                  "cap_audit_write",
                                  "cap_audit_control",
                                  "cap_setfcap",
                                  "cap_mac_override",
                                  "cap_mac_admin",
                                  "cap_syslog",
                                  "cap_wake_alarm",
                                  "cap_block_suspend",
                                  "cap_audit_read",
                                  "cap_perfmon",
                                  "cap_bpf",
                                  "cap_checkpoint_restore"};

static String read_proc_link(BumpArena &temp_arena, BumpArena &arena,
                             const Pid pid, const char *name, bool &ok) {
  const String path = String::sprintf(temp_arena, "/proc/%d/%s", pid, name);
  char buf[PATH_MAX];
  const ssize_t n = readlink(path.data, buf, sizeof(buf) - 1);
  if (n < 0) {
    ok = false;
    return String::static_string("");
  }
  ok = true;
  return String::copy_from(arena, buf, static_cast<uint32_t>(n));
}

static String resolve_user(BumpArena &arena, const uid_t uid) {
  char buf[UID_NAME_BUF_SIZE];
  return String::copy_from(arena, resolve_uid_name(uid, buf, sizeof(buf)));
}

static String resolve_group(BumpArena &arena, const gid_t gid) {
  char buf[GID_NAME_BUF_SIZE];
  return String::copy_from(arena, resolve_gid_name(gid, buf, sizeof(buf)));
}

// Append into a fixed buffer, clamping to what was actually written.
static void append_clamped(const size_t size, size_t &used, const int written) {
  if (written <= 0) return;
  const size_t remaining = size - used;
  used += static_cast<size_t>(written) < remaining
              ? static_cast<size_t>(written)
              : remaining - 1;
}

// Comma-joined names for the set bits; "all" when every known capability is
// present, "none" when the mask is empty.
static String decode_caps(BumpArena &arena, const unsigned long long mask) {
  constexpr int count = sizeof(CAP_NAMES) / sizeof(CAP_NAMES[0]);
  constexpr unsigned long long full = count >= 64 ? ~0ull : (1ull << count) - 1;
  if ((mask & full) == full) return String::static_string("all");

  char buf[1024];
  size_t used = 0;
  for (int i = 0; i < count; ++i) {
    if (!(mask & 1ull << i)) continue;
    append_clamped(sizeof(buf), used,
                   snprintf(buf + used, sizeof(buf) - used, "%s%s",
                            used ? ", " : "", CAP_NAMES[i]));
  }
  if (used == 0) return String::static_string("none");
  return String::copy_from(arena, buf, static_cast<uint32_t>(used));
}

static String decode_tty(BumpArena &arena, const int tty_nr) {
  if (tty_nr == 0) return String::static_string("-");
  const unsigned major = (tty_nr >> 8) & 0xfff;
  const unsigned minor = (tty_nr & 0xff) | (tty_nr >> 12 & 0xfff00);
  if (major == 136) return String::sprintf(arena, "/dev/pts/%u", minor);
  if (major == 4) return String::sprintf(arena, "/dev/tty%u", minor);
  return String::sprintf(arena, "%u:%u", major, minor);
}

// Supplementary group ids (a space-separated list) resolved to names.
static String build_groups(BumpArena &arena, const char *gids) {
  char buf[1024];
  size_t used = 0;
  const char *p = gids;
  while (*p) {
    char *end = nullptr;
    const unsigned long gid = strtoul(p, &end, 10);
    if (end == p) break;
    p = end;
    char namebuf[GID_NAME_BUF_SIZE];
    const char *name =
        resolve_gid_name(static_cast<uint32_t>(gid), namebuf, sizeof(namebuf));
    append_clamped(sizeof(buf), used,
                   snprintf(buf + used, sizeof(buf) - used, "%s%s",
                            used ? ", " : "", name));
  }
  if (used == 0) return String::static_string("");
  return String::copy_from(arena, buf, static_cast<uint32_t>(used));
}

// The cgroup the process belongs to: the unified (v2, hierarchy 0) path when
// present, otherwise the first hierarchy's path.
static String read_cgroup(BumpArena &temp_arena, BumpArena &arena,
                          const Pid pid) {
  const String path = String::sprintf(temp_arena, "/proc/%d/cgroup", pid);
  FILE *file = fopen(path.data, "r");
  if (!file) return String::static_string("");

  char best[1024] = "";
  char line[1024];
  while (fgets(line, sizeof(line), file)) {
    char *first = strchr(line, ':');
    if (!first) continue;
    char *second = strchr(first + 1, ':');
    if (!second) continue;
    char *cg_path = second + 1;
    size_t len = strlen(cg_path);
    while (len > 0 && (cg_path[len - 1] == '\n' || cg_path[len - 1] == '\r'))
      cg_path[--len] = '\0';

    const bool is_v2 =
        first == line + 1 && line[0] == '0' && second == first + 1;
    if (is_v2) {
      snprintf(best, sizeof(best), "%s", cg_path);
      break;
    }
    if (best[0] == '\0') snprintf(best, sizeof(best), "%s", cg_path);
  }
  fclose(file);
  return best[0] != '\0' ? String::copy_from(arena, best)
                         : String::static_string("");
}

static String read_security_label(BumpArena &temp_arena, BumpArena &arena,
                                  const Pid pid) {
  const String path = String::sprintf(temp_arena, "/proc/%d/attr/current", pid);
  FILE *file = fopen(path.data, "r");
  if (!file) return String::static_string("");
  char buf[256];
  size_t n = fread(buf, 1, sizeof(buf) - 1, file);
  fclose(file);
  buf[n] = '\0';
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\0'))
    buf[--n] = '\0';
  if (n == 0) return String::static_string("");
  return String::copy_from(arena, buf, static_cast<uint32_t>(n));
}

PropertiesResponse read_process_properties(BumpArena &temp_arena,
                                           const PropertiesRequest &request) {
  ZoneScoped;
  ZoneValue(request.pid);

  const Pid pid = request.pid;

  PropertiesResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();
  BumpArena &arena = response.owner_arena;
  ProcessProperties &props = response.props;
  props.pid = pid;
  props.umask = -1;
  props.no_new_privs = -1;
  props.seccomp = -1;
  props.groups = String::static_string("");

  // /proc/<pid>/stat is required; statm is read only to satisfy the shared
  // parser (its memory output is unused here).
  const String stat_path = String::sprintf(temp_arena, "/proc/%d/stat", pid);
  FILE *stat_file = fopen(stat_path.data, "r");
  if (!stat_file) {
    response.error_code = errno;
    return response;
  }
  char stat_buf[512];
  const bool got_stat = fgets(stat_buf, sizeof(stat_buf), stat_file) != nullptr;
  fclose(stat_file);
  if (!got_stat) {
    response.error_code = EIO;
    return response;
  }

  char statm_buf[128] = "";
  const String statm_path = String::sprintf(temp_arena, "/proc/%d/statm", pid);
  if (FILE *statm_file = fopen(statm_path.data, "r")) {
    if (!fgets(statm_buf, sizeof(statm_buf), statm_file)) statm_buf[0] = '\0';
    fclose(statm_file);
  }

  ProcessStat tmp = {};
  if (!parse_proc_stat_bufs(stat_buf, statm_buf, &tmp)) {
    response.error_code = EIO;
    return response;
  }
  props.ppid = tmp.ppid;
  props.starttime = tmp.starttime;

  // Controlling terminal: stat field (7) tty_nr, after state (3), ppid (4),
  // pgrp (5) and session (6), just past the comm's closing paren.
  props.tty = String::static_string("-");
  if (const char *after = strrchr(stat_buf, ')')) {
    int tty_nr = 0;
    sscanf(after + 1, " %*c %*d %*d %*d %d", &tty_nr);
    props.tty = decode_tty(arena, tty_nr);
  }

  char comm_buf[64];
  read_proc_comm(pid, comm_buf, sizeof(comm_buf));
  props.comm = String::copy_from(arena, comm_buf);

  char parent_buf[256];
  read_proc_display_name(props.ppid, parent_buf, sizeof(parent_buf));
  props.parent_name = String::copy_from(arena, parent_buf);

  // Credentials and security posture from /proc/<pid>/status. Read line by
  // line because the capability/seccomp lines sit near the end of the file.
  unsigned int ruid = 0, euid = 0, suid = 0;
  unsigned int rgid = 0, egid = 0, sgid = 0;
  const String status_path =
      String::sprintf(temp_arena, "/proc/%d/status", pid);
  if (FILE *status_file = fopen(status_path.data, "r")) {
    char line[512];
    // Decode a "CapXxx:\t<hex>" line into dst; returns true if the prefix
    // matched (so the else-if chain stops).
    const auto parse_cap = [&](const char *prefix, String &dst) {
      if (strncmp(line, prefix, 7) != 0) return false;
      unsigned long long m = 0;
      if (sscanf(line + 7, "%llx", &m) == 1) {
        dst = decode_caps(arena, m);
        props.caps_ok = true;
      }
      return true;
    };
    while (fgets(line, sizeof(line), status_file)) {
      if (strncmp(line, "Uid:", 4) == 0) {
        sscanf(line + 4, "%u %u %u", &ruid, &euid, &suid);
      } else if (strncmp(line, "Gid:", 4) == 0) {
        sscanf(line + 4, "%u %u %u", &rgid, &egid, &sgid);
      } else if (strncmp(line, "Groups:", 7) == 0) {
        props.groups = build_groups(arena, line + 7);
      } else if (strncmp(line, "Umask:", 6) == 0) {
        unsigned u;
        if (sscanf(line + 6, "%o", &u) == 1) props.umask = static_cast<int>(u);
      } else if (strncmp(line, "NoNewPrivs:", 11) == 0) {
        sscanf(line + 11, "%d", &props.no_new_privs);
      } else if (strncmp(line, "Seccomp:", 8) == 0) {
        sscanf(line + 8, "%d", &props.seccomp);
      } else if (strncmp(line, "Cap", 3) == 0) {
        parse_cap("CapInh:", props.cap_inh) ||
            parse_cap("CapPrm:", props.cap_prm) ||
            parse_cap("CapEff:", props.cap_eff) ||
            parse_cap("CapBnd:", props.cap_bnd) ||
            parse_cap("CapAmb:", props.cap_amb);
      }
    }
    fclose(status_file);
  }
  props.uid = ruid;
  props.euid = euid;
  props.suid = suid;
  props.gid = rgid;
  props.egid = egid;
  props.sgid = sgid;
  props.username = resolve_user(arena, ruid);
  props.groupname = resolve_group(arena, rgid);

  props.exe = read_proc_link(temp_arena, arena, pid, "exe", props.exe_ok);
  props.cwd = read_proc_link(temp_arena, arena, pid, "cwd", props.cwd_ok);
  props.root = read_proc_link(temp_arena, arena, pid, "root", props.root_ok);

  props.cgroup = read_cgroup(temp_arena, arena, pid);
  props.security_label = read_security_label(temp_arena, arena, pid);

  char cmdline_buf[4096];
  const size_t cmdline_len =
      read_proc_cmdline(pid, cmdline_buf, sizeof(cmdline_buf));
  props.cmdline = cmdline_len > 0
                      ? String::copy_from(arena, cmdline_buf,
                                          static_cast<uint32_t>(cmdline_len))
                      : String::static_string("");

  response.error_code = 0;
  return response;
}
