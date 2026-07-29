#pragma once

#include <cstdint>

void paths_default_out_dir(char *out, uint32_t size, const char *subdir);

bool paths_ensure_parent_dir(const char *out_path);

void paths_format_time_suffix(char *out, uint32_t size);
