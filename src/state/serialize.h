#pragma once

#include "base/base.h"
#include "base/containers.h"
#include "snapshot.h"
#include "state/raw_stats.h"
#include "state/system_info.h"

#include <cstdint>
#include <cstdio>

enum SerVer : uint32_t {
  eSerVer_Init = 0,
  eSerVer_Uptime = 1,
  eSerVer_Latest = eSerVer_Uptime,
};

enum SerRecordType : uint32_t {
  eSerRecordType_UpdateSnapshot = 0,
  eSerRecordType_Invalid = 1
};

// Contiguous, page-mapped growth buffer used as an in-memory serialization sink
// (see SerializeControl::out_buffer). Backed directly by vm_alloc/vm_free: an
// arena is pointless here since its abandoned-on-grow slabs and 256KB
// granularity would waste memory per buffer. Grows by mapping a bigger block
// and copying; destroy() unmaps it. Handed to the recorder thread by value,
// which destroys it after writing.
struct SerializeBuffer {
  uint8_t *data;
  uint32_t size;
  uint32_t capacity;

  void reserve(const uint32_t needed) {
    if (needed <= capacity) return;
    // The first allocation honors an explicit reserve() hint as-is, so a caller
    // can size the buffer to ~1.2x the previous record and (almost) never grow;
    // any later overflow doubles to stay amortized. 4096 floors tiny buffers.
    uint32_t new_capacity = capacity ? capacity * 2 : needed;
    if (new_capacity < needed) new_capacity = needed;
    if (new_capacity < 4096) new_capacity = 4096;
    uint8_t *new_data = static_cast<uint8_t *>(vm_alloc(new_capacity));
    if (size > 0) memcpy(new_data, data, size);
    vm_free(data, capacity); // no-op when data is null
    data = new_data;
    capacity = new_capacity;
  }

  void append(const void *src, const uint32_t n) {
    reserve(size + n);
    memcpy(data + size, src, n);
    size += n;
  }

  // Overwrite already-written bytes (record-length back-patch). offset + n must
  // lie within the written region.
  void write_at(const uint32_t offset, const void *src, const uint32_t n) {
    memcpy(data + offset, src, n);
  }

  void destroy() {
    vm_free(data, capacity);
    *this = {};
  }
};

struct SerializeControl {
  BumpArena *arena;
  InternTable *intern_table;

  FILE *file;
  SerializeBuffer *out_buffer;

  uint32_t data_version;
  bool is_writing;

  // TODO: At some point we should have a readable error here:
  bool failed;
};

#define ADD_TO(_field_added, where)                                            \
  if (control->data_version >= (_field_added)) {                               \
    serialize(control, where);                                                 \
  }

#define ADD_FIELD(_field_added, _field_name)                                   \
  if (control->data_version >= (_field_added)) {                               \
    serialize(control, &(datum->_field_name));                                 \
  }

#define ADD_FIELD_LIMITED(_field_added, _field_name, limit)                    \
  if (control->data_version >= (_field_added)) {                               \
    serialize_with_limit(control, &(datum->_field_name), limit);               \
  }

#define VALIDATE(condition)                                                    \
  if (condition) {                                                             \
  } else {                                                                     \
    control->failed = true;                                                    \
    return;                                                                    \
  }

#define CHECK_FAILURE()                                                        \
  if (control->failed) {                                                       \
    return;                                                                    \
  }

void serialize(SerializeControl *control, char *datum);
void serialize(SerializeControl *control, int32_t *datum);
void serialize(SerializeControl *control, uint32_t *datum);
void serialize(SerializeControl *control, int64_t *datum);
void serialize(SerializeControl *control, uint64_t *datum);

void serialize(SerializeControl *control, SystemTimePoint *datum);
void serialize(SerializeControl *control, SteadyTimeDataPoint *datum);

void serialize_with_limit(SerializeControl *control, const char **datum,
                          uint32_t limit);
void serialize_with_limit(SerializeControl *control, PersistentString *datum,
                          uint32_t limit);

void serialize(SerializeControl *control, CpuCoreStat *datum);
void serialize(SerializeControl *control, MemInfo *datum);
void serialize(SerializeControl *control, DiskIoStat *datum);
void serialize(SerializeControl *control, NetIoStat *datum);
void serialize(SerializeControl *control, SystemInfo *datum);
void serialize(SerializeControl *control, ProcessStat *datum);

void serialize(SerializeControl *control, UpdateSnapshot *datum);

template <class T>
void serialize_with_limit(SerializeControl *control, Array<T> *datum,
                          const uint32_t limit) {
  ADD_TO(eSerVer_Init, &datum->size);
  VALIDATE(datum->size <= limit);

  if (!control->is_writing) {
    *datum = Array<T>::create(*control->arena, datum->size);
  }

  for (uint32_t i = 0; i < datum->size; ++i) {
    ADD_TO(eSerVer_Init, &datum->data[i]);
    CHECK_FAILURE();
  }
}

// serialize_header()
// for (i in record_in_stream) {
//    serialize_record_header()
//    switch() { serialize() }
//    serialize_record_footer()
// }
// control->failed contains the failure state
void serialize_header(SerializeControl *control);

struct RecordHeader {
  uint32_t record_type;
  SteadyTimeDataPoint *at;

  uint32_t len;
  long len_pos; // not serialized, used for determining len
};

void serialize_record_header(SerializeControl *control, RecordHeader *header);

void serialize_record_footer(SerializeControl *control, RecordHeader *header);
