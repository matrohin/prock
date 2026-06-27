#pragma once

#include "base/base.h"

#include <atomic>
#include <limits.h>

// Creates a core dump of a live process by shelling out to gcore (part of gdb).
// The dump does not terminate the target. Runs on the on-demand actions thread.

struct DumpRequest {
  Pid pid;
  uint64_t id;             // correlates the reply with its in-progress toast
  char out_path[PATH_MAX]; // gcore -o base; final file is "<out_path>.<pid>"
};

struct DumpResponse {
  Pid pid;
  uint64_t id;             // echoed from the request
  char out_path[PATH_MAX]; // echoed back for the success message
  int error_code;          // 0 on success; non-zero drives the pkexec button
  bool gcore_missing;      // gcore binary not found on PATH
  int exit_status;         // gcore's exit code when it ran but failed
};

DumpResponse write_process_dump(const DumpRequest &request,
                                const std::atomic<bool> &quit);

// Default folder for core dumps ("$HOME/prock-dumps", or under /tmp if $HOME is
// unset). Used to seed the preference and as the fallback when it is empty.
void default_dump_dir(char *out, uint32_t size);
