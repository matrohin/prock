#include "serialize.h"

#include <bits/error_constants.h>

static constexpr uint32_t MAGIC_HEADER = 0x7072636b; // PRCK

template <class T>
static void serialize_primitive(SerializeControl *control, T *datum) {
  CHECK_FAILURE();

  if (control->is_writing) {
    const size_t written = fwrite(datum, sizeof(T), 1, control->file);
    VALIDATE(written == 1);
  } else {
    const size_t read = fread(datum, sizeof(T), 1, control->file);
    VALIDATE(read == 1);
  }
}

void serialize(SerializeControl *control, char *datum) {
  serialize_primitive(control, datum);
}

void serialize(SerializeControl *control, int32_t *datum) {
  serialize_primitive(control, datum);
}

void serialize(SerializeControl *control, uint32_t *datum) {
  serialize_primitive(control, datum);
}

void serialize(SerializeControl *control, int64_t *datum) {
  serialize_primitive(control, datum);
}

void serialize(SerializeControl *control, uint64_t *datum) {
  serialize_primitive(control, datum);
}

void serialize(SerializeControl *control, SystemTimePoint *datum) {
  int64_t since_epoch_time = datum->time_since_epoch().count();
  ADD_TO(eSerVer_Init, &since_epoch_time);
  if (!control->is_writing) {
    *datum = SystemTimePoint{std::chrono::nanoseconds{since_epoch_time}};
  }
}

void serialize(SerializeControl *control, SteadyTimeDataPoint *datum) {
  ADD_TO(eSerVer_Init, &datum->at_ns);
}

void serialize_with_limit(SerializeControl *control, const char **datum,
                          const uint32_t limit) {
  uint32_t len = 0;
  if (control->is_writing) {
    len = strlen(*datum) + 1;
  }
  ADD_TO(eSerVer_Init, &len);
  VALIDATE(len < limit);

  if (control->is_writing) {
    const size_t written = fwrite(*datum, len, 1, control->file);
    VALIDATE(written == 1);
  } else {
    char *buf = control->arena->alloc_string(len);
    const size_t read = fread(buf, len, 1, control->file);
    VALIDATE(read == 1);
    *datum = buf;
  }
}

void serialize_with_limit(SerializeControl *control, PersistentString *datum,
                          const uint32_t limit) {
  const char *copy_ptr = datum->data;
  serialize_with_limit(control, &copy_ptr, limit);
  CHECK_FAILURE();

  if (!control->is_writing) {
    const ConstString result = control->intern_table->intern(copy_ptr);
    datum->data = result.data;
  }
}

void serialize(SerializeControl *control, CpuCoreStat *datum) {
  ADD_FIELD(eSerVer_Init, total);
  ADD_FIELD(eSerVer_Init, busy);
  ADD_FIELD(eSerVer_Init, kernel);
  ADD_FIELD(eSerVer_Init, interrupts);
}

void serialize(SerializeControl *control, MemInfo *datum) {
  ADD_FIELD(eSerVer_Init, mem_total);
  ADD_FIELD(eSerVer_Init, mem_available);
}

void serialize(SerializeControl *control, DiskIoStat *datum) {
  ADD_FIELD(eSerVer_Init, sectors_read);
  ADD_FIELD(eSerVer_Init, sectors_written);
}

void serialize(SerializeControl *control, NetIoStat *datum) {
  ADD_FIELD(eSerVer_Init, bytes_received);
  ADD_FIELD(eSerVer_Init, bytes_transmitted);
}

void serialize(SerializeControl *control, SystemInfo *datum) {
  ADD_FIELD(eSerVer_Init, ticks_in_second);
  ADD_FIELD(eSerVer_Init, mem_page_size);
  ADD_FIELD(eSerVer_Init, boot_time_epoch_sec);
}

void serialize(SerializeControl *control, ProcessStat *datum) {
  ADD_FIELD_LIMITED(eSerVer_Init, comm, 128);
  ADD_FIELD_LIMITED(eSerVer_Init, cmdline, 1024);
  ADD_FIELD_LIMITED(eSerVer_Init, wchan, 128);
  ADD_FIELD_LIMITED(eSerVer_Init, username, 128);
  ADD_FIELD(eSerVer_Init, utime);
  ADD_FIELD(eSerVer_Init, stime);
  ADD_FIELD(eSerVer_Init, num_threads);
  ADD_FIELD(eSerVer_Init, nice);
  ADD_FIELD(eSerVer_Init, vsize);
  ADD_FIELD(eSerVer_Init, statm_resident);
  ADD_FIELD(eSerVer_Init, starttime);

  ADD_FIELD(eSerVer_Init, io_read_bytes);
  ADD_FIELD(eSerVer_Init, io_write_bytes);

  ADD_FIELD(eSerVer_Init, read_time_ns);

  ADD_FIELD(eSerVer_Init, pid);
  ADD_FIELD(eSerVer_Init, ppid);
  ADD_FIELD(eSerVer_Init, last_cpu);
  ADD_FIELD(eSerVer_Init, state);
}

void serialize(SerializeControl *control, UpdateSnapshot *datum) {
  BumpArena *prev_arena = control->arena;
  control->arena = &datum->owner_arena;

  // Array<ThreadSnapshot> thread_snapshots;

  ADD_FIELD_LIMITED(eSerVer_Init, stats, 16384);
  ADD_FIELD_LIMITED(eSerVer_Init, cpu_stats, 1024);
  ADD_FIELD(eSerVer_Init, mem_info);
  ADD_FIELD(eSerVer_Init, disk_io_stats);
  ADD_FIELD(eSerVer_Init, net_io_stats);
  ADD_FIELD(eSerVer_Init, system_time);

  // "at" is serialized by the top layer as part of the record header

  control->arena = prev_arena;
}

void serialize_header(SerializeControl *control) {
  uint32_t magic_header = MAGIC_HEADER;
  uint32_t byte_order = 0;
  if (control->is_writing) {
    control->data_version = eSerVer_Latest;
    byte_order = __BYTE_ORDER__;
  }

  serialize(control, &magic_header);
  serialize(control, &control->data_version);
  serialize(control, &byte_order);

  VALIDATE(control->data_version <= eSerVer_Latest &&
           byte_order == __BYTE_ORDER__ && magic_header == MAGIC_HEADER);
}

void serialize_record_header(SerializeControl *control, RecordHeader *header) {
  CHECK_FAILURE();
  serialize(control, &header->record_type);
  VALIDATE(header->record_type < eSerRecordType_Invalid);

  serialize(control, header->at);
  CHECK_FAILURE();

  header->len_pos = ftell(control->file);
  serialize(control, &header->len);
}

void serialize_record_footer(SerializeControl *control, RecordHeader *header) {
  CHECK_FAILURE();

  const long end = ftell(control->file);
  const long body_len =
      end - header->len_pos - static_cast<long>(sizeof(header->len));

  if (control->is_writing) {
    header->len = static_cast<uint32_t>(body_len);
    fseek(control->file, header->len_pos, SEEK_SET);
    serialize(control, &header->len);
    fseek(control->file, end, SEEK_SET);
  } else {
    VALIDATE(header->len == static_cast<uint32_t>(body_len));
  }
}
