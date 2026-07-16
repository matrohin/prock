#pragma once

#include <cstdint>

struct SystemInfo {
  uint64_t ticks_in_second;
  uint64_t mem_page_size;
  uint64_t boot_time_epoch_sec; // /proc/stat "btime", constant for the session
};
