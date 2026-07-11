#pragma once

#include "base/base.h"
#include "base/string.h"

#include <sys/types.h>

// Static, read-once identity facts about a process. Dynamic/runtime values
// (CPU, memory, I/O, state, context switches, faults, swap, OOM, ...) are
// intentionally excluded here and will be provided by a separate struct.
struct ProcessProperties {
  String comm;
  String parent_name;
  String exe;
  String cwd;
  String root;
  String cmdline;
  String username;
  String groupname;
  String tty;            // decoded controlling terminal, "-" if none
  String groups;         // supplementary groups, comma-joined names
  String cgroup;         // cgroup path (systemd unit / container)
  String cap_inh;        // inheritable capabilities, comma-joined names
  String cap_prm;        // permitted capabilities
  String cap_eff;        // effective capabilities
  String cap_bnd;        // bounding-set capabilities
  String cap_amb;        // ambient capabilities
  String security_label; // SELinux/AppArmor context, empty if none
  Pid pid;
  Pid ppid;
  uid_t uid, euid, suid;
  gid_t gid, egid, sgid;
  unsigned long long starttime; // raw clock ticks since boot
  int umask;                    // -1 if unavailable
  int no_new_privs;             // -1 unknown, else 0/1
  int seccomp;                  // -1 unknown, else mode (0/1/2)
  bool exe_ok;
  bool cwd_ok;
  bool root_ok;
  bool caps_ok;
};

struct PropertiesRequest {
  Pid pid;
};

struct PropertiesResponse {
  Pid pid;
  int error_code; // 0=success, errno otherwise
  BumpArena owner_arena;
  ProcessProperties props;
};

PropertiesResponse properties_reader_read(BumpArena &temp_arena,
                                          const PropertiesRequest &request);
