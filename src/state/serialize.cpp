#include "serialize.h"

#include <bits/error_constants.h>

static constexpr uint32_t MAGIC_HEADER = 0x7072636b; // PRCK

static long ser_tell(SerializeControl *control) {
  return control->out_buffer ? static_cast<long>(control->out_buffer->size)
                             : ftell(control->file);
}

static void ser_write_bytes(SerializeControl *control, const void *src,
                            const size_t n) {
  if (control->out_buffer) {
    control->out_buffer->append(src, static_cast<uint32_t>(n));
  } else if (n > 0 && fwrite(src, n, 1, control->file) != 1) {
    // A zero-length write (e.g. the empty content of a "" string) is a no-op,
    // not a failure - fwrite(_, 0, 1, _) returns 0, which must not trip
    // `failed`.
    control->failed = true;
  }
}

static void ser_write_at(SerializeControl *control, const long pos,
                         const void *src, const size_t n) {
  if (control->out_buffer) {
    control->out_buffer->write_at(static_cast<uint32_t>(pos), src,
                                  static_cast<uint32_t>(n));
    return;
  }
  const long end = ftell(control->file);
  fseek(control->file, pos, SEEK_SET);
  if (fwrite(src, n, 1, control->file) != 1) control->failed = true;
  fseek(control->file, end, SEEK_SET);
}

template <class T>
static void serialize_primitive(SerializeControl *control, T *datum) {
  CHECK_FAILURE();

  if (control->is_writing) {
    ser_write_bytes(control, datum, sizeof(T));
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
    len = std::min(limit, len);
  }
  ADD_TO(eSerVer_Init, &len);
  VALIDATE(len <= limit);

  if (control->is_writing) {
    if (len > 0) {
      ser_write_bytes(control, *datum, len - 1);
      const char *empty = "";
      ser_write_bytes(control, empty, 1);
    }
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
  ZoneScoped;
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
  ZoneScoped;
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
  ZoneScoped;
  CHECK_FAILURE();
  serialize(control, &header->record_type);
  VALIDATE(header->record_type < eSerRecordType_Invalid);

  serialize(control, header->at);
  CHECK_FAILURE();

  header->len_pos = ser_tell(control);
  serialize(control, &header->len);
}

void serialize_record_footer(SerializeControl *control, RecordHeader *header) {
  ZoneScoped;
  CHECK_FAILURE();

  const long end = ser_tell(control);
  const long body_len =
      end - header->len_pos - static_cast<long>(sizeof(header->len));

  if (control->is_writing) {
    header->len = static_cast<uint32_t>(body_len);
    ser_write_at(control, header->len_pos, &header->len, sizeof(header->len));
  } else {
    VALIDATE(header->len == static_cast<uint32_t>(body_len));
  }
}
