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

// A stable, arena-owned string. Unlike ConstString it is not interned, so it is
// not deduplicated by content and equality is not a pointer compare. Valid for
// the owning arena's lifetime. Use for cached values (e.g. StringCache).
struct PersistentString {
  const char *data;
};

inline uint32_t fnv1a(const char *data, const uint32_t len) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint8_t>(data[i]);
    hash *= 16777619u;
  }
  return hash;
}

// Shared mechanics for the open-addressing, power-of-two-sized, linear-probing
// hash tables below. A Slot must be empty when zero-initialized and expose
// `bool occupied() const` plus a cached `uint32_t hash` (used when rehashing).
// Each table keeps its own lookup/insert probing, since the match test differs.
template <class Slot> Slot *probe_alloc(const uint32_t cap) {
  return static_cast<Slot *>(vm_alloc(cap * sizeof(Slot)));
}

template <class Slot> void probe_free(Slot *slots, const uint32_t cap) {
  vm_free(slots, cap * sizeof(Slot));
}

// Reinsert all occupied slots into a fresh array of new_cap, free the old array,
// and return the new one. The caller updates its capacity to new_cap.
template <class Slot>
Slot *probe_rehash(Slot *slots, const uint32_t cap, const uint32_t new_cap) {
  Slot *new_slots = probe_alloc<Slot>(new_cap);
  const uint32_t mask = new_cap - 1;
  for (uint32_t i = 0; i < cap; ++i) {
    if (!slots[i].occupied()) continue;
    uint32_t idx = slots[i].hash & mask;
    while (new_slots[idx].occupied())
      idx = (idx + 1) & mask;
    new_slots[idx] = slots[i];
  }
  probe_free(slots, cap);
  return new_slots;
}

// True when inserting one more entry would exceed a 75% load factor.
inline bool probe_should_grow(const uint32_t count, const uint32_t cap) {
  return (count + 1) * 4 >= cap * 3;
}

struct InternTable {
  struct Slot {
    const char *data;
    uint32_t len;
    uint32_t hash;
    bool occupied() const { return data != nullptr; }
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
    t.slots = probe_alloc<Slot>(INITIAL_CAP);
    return t;
  }

  void destroy() {
    probe_free(slots, cap);
    slots = nullptr;
    cap = 0;
    count = 0;
  }

  void grow(const uint32_t new_cap) {
    slots = probe_rehash(slots, cap, new_cap);
    cap = new_cap;
  }

  ConstString intern(const char *str, const uint32_t len) {
    if (probe_should_grow(count, cap)) grow(cap * 2);

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
};

// Cache from a uint32 key to a PersistentString, mirroring InternTable: the
// arena is caller-owned and set() copies the value into it once, so the result
// is stable for the arena's lifetime and never needs re-copying. get() returns
// a null PersistentString on a miss so the caller can resolve and store.
struct StringCache {
  struct Slot {
    PersistentString value;
    uint32_t key;
    uint32_t hash;
    bool occupied() const { return value.data != nullptr; }
  };

  static constexpr uint32_t INITIAL_CAP = 64;

  Slot *slots; // self-owned, power-of-two capacity
  uint32_t cap;
  uint32_t count;
  BumpArena *arena; // caller-owned

  static StringCache create(BumpArena *string_arena) {
    StringCache c = {};
    c.arena = string_arena;
    c.cap = INITIAL_CAP;
    c.slots = probe_alloc<Slot>(INITIAL_CAP);
    return c;
  }

  void destroy() {
    probe_free(slots, cap);
    slots = nullptr;
    cap = 0;
    count = 0;
  }

  void grow(const uint32_t new_cap) {
    slots = probe_rehash(slots, cap, new_cap);
    cap = new_cap;
  }

  static uint32_t key_hash(const uint32_t key) {
    return fnv1a(reinterpret_cast<const char *>(&key), sizeof(key));
  }

  PersistentString get(const uint32_t key) const {
    if (!slots) return PersistentString{nullptr};
    const uint32_t mask = cap - 1;
    uint32_t idx = key_hash(key) & mask;
    while (slots[idx].value.data) {
      if (slots[idx].key == key) return slots[idx].value;
      idx = (idx + 1) & mask;
    }
    return PersistentString{nullptr};
  }

  PersistentString set(const uint32_t key, const char *value) {
    if (probe_should_grow(count, cap)) grow(cap * 2);

    const uint32_t hash = key_hash(key);
    const uint32_t mask = cap - 1;
    uint32_t idx = hash & mask;
    while (slots[idx].value.data) {
      if (slots[idx].key == key) return slots[idx].value;
      idx = (idx + 1) & mask;
    }

    const PersistentString stored = {arena->alloc_string_copy(value)};
    slots[idx] = {stored, key, hash};
    ++count;
    return stored;
  }
};
