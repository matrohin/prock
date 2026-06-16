#pragma once

#include <cstdlib>
#include <sys/mman.h>

#include "tracy/Tracy.hpp"

inline void *vm_alloc(const size_t size) {
  void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) std::abort();
  TracyAlloc(p, size);
  return p;
}

inline void vm_free(void *ptr, size_t size) {
  if (ptr) {
    TracyFree(ptr);
    munmap(ptr, size);
  }
}
