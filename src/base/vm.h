#pragma once

#include <cstdlib>
#include <sys/mman.h>

inline void *vm_alloc(const size_t size) {
  void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) std::abort();
  return p;
}

inline void vm_free(void *ptr, size_t size) {
  if (ptr) munmap(ptr, size);
}
