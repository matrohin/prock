#include "open_files_reader.h"

#include "tracy/Tracy.hpp"

#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Read the access mode (O_RDONLY/O_WRONLY/O_RDWR) from /proc/<pid>/fdinfo/<fd>.
// The file has a line like "flags:\t0100002" with the open flags in octal.
static FdAccess read_fd_access(BumpArena &temp_arena, const Pid pid,
                               const int fd) {
  const String path =
      String::sprintf(temp_arena, "/proc/%d/fdinfo/%d", pid, fd);

  FILE *file = fopen(path.data, "r");
  if (!file) return eFdAccess_Unknown;

  FdAccess access = eFdAccess_Unknown;
  char line[64];
  while (fgets(line, sizeof(line), file)) {
    if (strncmp(line, "flags:", 6) != 0) continue;
    const long flags = strtol(line + 6, nullptr, 8);
    switch (flags & O_ACCMODE) {
    case O_RDONLY:
      access = eFdAccess_Read;
      break;
    case O_WRONLY:
      access = eFdAccess_Write;
      break;
    case O_RDWR:
      access = eFdAccess_ReadWrite;
      break;
    }
    break;
  }
  fclose(file);
  return access;
}

// Classify the descriptor and capture its size. Special kinds (socket, pipe,
// anon_inode) are detected from the readlink target prefix; everything else is
// a path on disk that we stat() to distinguish file/dir/char/block and to read
// the size. stat() failing (e.g. a "(deleted)" file) falls back to a plain
// file.
static void classify_fd(const char *target, const char *link_path,
                        FdType &out_type, long &out_size) {
  out_size = -1;
  if (strncmp(target, "socket:[", 8) == 0) {
    out_type = eFdType_Socket;
    return;
  }
  if (strncmp(target, "pipe:[", 6) == 0) {
    out_type = eFdType_Pipe;
    return;
  }
  if (strncmp(target, "anon_inode:", 11) == 0) {
    out_type = eFdType_Anon;
    return;
  }

  struct stat st;
  if (stat(link_path, &st) != 0) {
    out_type = eFdType_File;
    return;
  }
  if (S_ISDIR(st.st_mode)) {
    out_type = eFdType_Dir;
  } else if (S_ISCHR(st.st_mode)) {
    out_type = eFdType_Char;
  } else if (S_ISBLK(st.st_mode)) {
    out_type = eFdType_Block;
  } else if (S_ISREG(st.st_mode)) {
    out_type = eFdType_File;
    out_size = st.st_size;
  } else {
    out_type = eFdType_Other;
  }
}

OpenFilesResponse read_process_open_files(BumpArena &temp_arena,
                                          const OpenFilesRequest &request) {
  ZoneScoped;
  const Pid pid = request.pid;

  OpenFilesResponse response = {};
  response.pid = pid;
  response.owner_arena = BumpArena::create();

  const String fd_dir_path = String::sprintf(temp_arena, "/proc/%d/fd", pid);

  DIR *fd_dir = opendir(fd_dir_path.data);
  if (!fd_dir) {
    response.error_code = errno;
    response.files = Array<OpenFileEntry>::create(response.owner_arena, 0);
    return response;
  }

  GrowingArray<OpenFileEntry> entries = {};
  dirent *entry;
  while ((entry = readdir(fd_dir))) {
    if (entry->d_name[0] == '.') continue;

    const String link_path =
        String::sprintf(temp_arena, "%s/%s", fd_dir_path.data, entry->d_name);

    char target[PATH_MAX];
    const ssize_t len = readlink(link_path.data, target, sizeof(target) - 1);
    if (len < 0) continue;
    target[len] = '\0';

    OpenFileEntry *e = entries.emplace_back(temp_arena);
    e->fd = atoi(entry->d_name);
    classify_fd(target, link_path.data, e->type, e->size);
    e->access = read_fd_access(temp_arena, pid, e->fd);
    e->path = String::copy_from(response.owner_arena, target,
                                static_cast<uint32_t>(len));
  }
  closedir(fd_dir);

  response.files =
      Array<OpenFileEntry>::copy_from(response.owner_arena, entries.to_array());
  return response;
}
