#include "dump_writer.h"

#include "tracy/Tracy.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char **environ;

void default_dump_dir(char *out, const uint32_t size) {
  const char *home = getenv("HOME");
  snprintf(out, size, "%s/prock-dumps", home ? home : "/tmp");
}

// Ensure the directory holding out_path exists. Best-effort; failures surface
// later when gcore cannot write its file.
static void ensure_parent_dir(const char *out_path) {
  char dir[PATH_MAX];
  snprintf(dir, sizeof(dir), "%s", out_path);
  char *slash = strrchr(dir, '/');
  if (!slash || slash == dir) {
    return;
  }
  *slash = '\0';
  mkdir(dir, 0700); // ignore EEXIST and other errors
}

DumpResponse write_process_dump(const DumpRequest &request,
                                const std::atomic<bool> &quit) {
  ZoneScoped;
  ZoneValue(request.pid);

  DumpResponse response = {};
  response.pid = request.pid;
  snprintf(response.out_path, sizeof(response.out_path), "%s",
           request.out_path);

  ensure_parent_dir(request.out_path);

  char pid_str[16];
  snprintf(pid_str, sizeof(pid_str), "%d", request.pid);

  // posix_spawn is the right tool for "exec a child and wait": in this
  // multithreaded process it avoids the fork() async-signal-safety pitfalls,
  // and file actions redirect gcore's chatty stdout/stderr to /dev/null without
  // any fragile post-fork code.
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                   O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                   O_WRONLY, 0);

  char *const argv[] = {const_cast<char *>("gcore"), const_cast<char *>("-o"),
                        const_cast<char *>(request.out_path), pid_str, nullptr};

  pid_t child = 0;
  const int rc =
      posix_spawnp(&child, "gcore", &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);

  if (rc != 0) {
    // Some libc versions report a failed exec here rather than via exit 127.
    if (rc == ENOENT) {
      response.gcore_missing = true;
    } else {
      response.error_code = rc;
    }
    return response;
  }

  // Poll instead of a blocking wait so a long-running gcore doesn't stall app
  // shutdown: on quit we kill the child and reap it ourselves. The owning
  // thread does both the kill and the reap, so there is no PID-recycle race.
  int status = 0;
  for (;;) {
    const pid_t r = waitpid(child, &status, WNOHANG);
    if (r == child) break; // exited
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) {
      response.error_code = errno;
      return response;
    }
    if (quit.load()) {
      kill(child, SIGKILL);
      while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        // reap
      }
      response.error_code = ECANCELED; // shutting down; not surfaced
      return response;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (WIFEXITED(status)) {
    const int code = WEXITSTATUS(status);
    if (code == 0) {
      return response; // success
    }
    if (code == 127) {
      response.gcore_missing = true;
      return response;
    }
    response.exit_status = code;
  } else {
    // Killed by a signal.
    response.exit_status = WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
  }
  // Non-zero, non-missing failure: most likely ptrace permission or the
  // process exited. error_code stays 0 here; the view layer decides whether to
  // offer a pkexec restart based on the current euid.
  return response;
}
