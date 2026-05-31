#pragma once

#include "base/base.h"
#include "base/vm.h"

// A deduplicated, immutable string. Interned through an InternTable, so equal
// content always shares one canonical pointer - equality is a pointer compare.
struct ConstString {
  const char *data;

  bool operator==(const ConstString &o) const { return data == o.data; }
  bool operator!=(const ConstString &o) const { return data != o.data; }
};

inline uint32_t fnv1a(const char *data, const uint32_t len) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint8_t>(data[i]);
    hash *= 16777619u;
  }
  return hash;
}

struct InternTable {
  struct Slot {
    const char *data;
    uint32_t len;
    uint32_t hash;
  };

  static constexpr uint32_t INITIAL_CAP = 256; // 4KB

  Slot *slots; // self-owned, power-of-two capacity
  uint32_t cap;
  uint32_t count;
  BumpArena *arena; // caller-owned

  static InternTable create(BumpArena *string_arena) {
    InternTable t = {};
    t.arena = string_arena;
    t.cap = INITIAL_CAP;
    t.slots = static_cast<Slot *>(vm_alloc(INITIAL_CAP * sizeof(Slot)));
    return t;
  }

  void destroy() {
    vm_free(slots, cap * sizeof(Slot));
    slots = nullptr;
    cap = 0;
    count = 0;
  }

  ConstString intern(const char *str, const uint32_t len) {
    if ((count + 1) * 4 >= cap * 3) {
      grow(cap * 2);
    }

    const uint32_t hash = fnv1a(str, len);
    const uint32_t mask = cap - 1;
    uint32_t idx = hash & mask;
    while (slots[idx].data) {
      const Slot &slot = slots[idx];
      if (slot.hash == hash && slot.len == len &&
          memcmp(slot.data, str, len) == 0) {
        return ConstString{slot.data};
      }
      idx = (idx + 1) & mask;
    }

    const char *stored = arena->alloc_string_copy(str, len);
    slots[idx] = {stored, len, hash};
    ++count;
    return ConstString{stored};
  }

  ConstString intern(const char *str) {
    return intern(str, static_cast<uint32_t>(strlen(str)));
  }

  void grow(const uint32_t new_cap) {
    Slot *old_slots = slots;
    const uint32_t old_cap = cap;

    slots = static_cast<Slot *>(vm_alloc(new_cap * sizeof(Slot)));
    cap = new_cap;

    const uint32_t mask = cap - 1;
    for (uint32_t i = 0; i < old_cap; ++i) {
      if (!old_slots[i].data) continue;
      uint32_t idx = old_slots[i].hash & mask;
      while (slots[idx].data)
        idx = (idx + 1) & mask;
      slots[idx] = old_slots[i];
    }

    vm_free(old_slots, old_cap * sizeof(Slot));
  }
};
