#pragma once

#include "base/containers.h"
#include "base/string.h"

enum FdType {
  eFdType_File,
  eFdType_Dir,
  eFdType_Char,
  eFdType_Block,
  eFdType_Socket,
  eFdType_Pipe,
  eFdType_Anon,
  eFdType_Other,
};

enum FdAccess {
  eFdAccess_Unknown,
  eFdAccess_Read,
  eFdAccess_Write,
  eFdAccess_ReadWrite,
};

struct OpenFileEntry {
  int fd;
  FdType type;
  FdAccess access;
  long size;   // st_size for regular files, -1 otherwise
  String path; // readlink target, owned by owner_arena
};

struct OpenFilesRequest {
  Pid pid;
};

struct OpenFilesResponse {
  Pid pid;
  int error_code; // 0=success, errno otherwise
  BumpArena owner_arena;
  Array<OpenFileEntry> files;
};

OpenFilesResponse open_files_reader_read(BumpArena &temp_arena,
                                         const OpenFilesRequest &request);
