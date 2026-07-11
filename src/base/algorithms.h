
#pragma once

#include <cstdint>

template <class T, class F>
uint32_t lower_bound(const uint32_t size, F get_val, const T val) {
  uint32_t left = 0;
  uint32_t right = size;
  while (right - left > 1) {
    const uint32_t mid = (left + right) / 2;
    const T &mid_val = get_val(mid);
    if (mid_val <= val) {
      left = mid;
    } else {
      right = mid;
    }
  }
  return left;
}

template <class T, class F>
uint32_t bin_search_exact(const uint32_t size, F get_val, const T val) {
  const uint32_t result = lower_bound(size, get_val, val);
  if (result >= size || get_val(result) != val) {
    return UINT32_MAX;
  }
  return result;
}
