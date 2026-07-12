#include "base.h"

SlabCache g_slab_cache;

#ifdef PROCK_SANITIZE
// Ignore leaks because of GUI exit leaks (e.g. fontconfig):
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" const char *__asan_default_options() { return "detect_leaks=0"; }
// Add readable stack-traces to the UBSan reports:
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" const char *__ubsan_default_options() {
  return "print_stacktrace=1";
}
#endif
