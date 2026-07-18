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
  eSerVer_Latest = eSerVer_Init,
};

enum SerRecordType : uint32_t {
  eSerRecordType_UpdateSnapshot = 0,
  eSerRecordType_Invalid = 1
};

struct SerializeControl {
  BumpArena *arena;
  InternTable *intern_table;

  FILE *file;
  uint32_t data_version;
  bool is_writing;

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
  VALIDATE(datum->size < limit);

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
