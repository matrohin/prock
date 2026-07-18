#include "doctest.h"

#include "state/serialize.h"

#include <chrono>
#include <cstdio>
#include <cstring>

// ============================================================================
// Serialization round-trip tests
//
// The serializer runs the same code for both directions, keyed on
// control->is_writing. Every test writes into a seekable tmpfile(), rewinds,
// reads back into a zero-initialized target, and compares field-by-field (none
// of the domain structs define operator==). A tmpfile() is required because the
// record footer back-patches the length via ftell/fseek.
// ============================================================================

namespace {

// Owns the arena, intern table and temp file backing a single round trip.
struct SerFixture {
  BumpArena arena = BumpArena::create();
  InternTable intern = InternTable::create(&arena);
  FILE *file = tmpfile();

  ~SerFixture() {
    if (file) fclose(file);
    intern.destroy();
    arena.destroy();
  }

  SerializeControl writer() {
    SerializeControl control = {};
    control.arena = &arena;
    control.intern_table = &intern;
    control.file = file;
    control.data_version = eSerVer_Latest;
    control.is_writing = true;
    return control;
  }

  // Rewinds the file so reads start from the beginning of what was written.
  SerializeControl reader() {
    rewind(file);
    SerializeControl control = {};
    control.arena = &arena;
    control.intern_table = &intern;
    control.file = file;
    control.data_version = eSerVer_Latest;
    control.is_writing = false;
    return control;
  }
};

// Fully-populated ProcessStat with distinct, seed-derived values. Every string
// is non-null (the string path strlen()s them).
ProcessStat make_stat(Pid pid, const char *comm, const char *cmdline,
                      const char *wchan, const char *user) {
  ProcessStat stat = {};
  stat.comm = comm;
  stat.cmdline = cmdline;
  stat.wchan = wchan;
  stat.username = {user};
  stat.utime = pid * 10u;
  stat.stime = pid * 20u;
  stat.num_threads = pid;
  stat.nice = -5;
  stat.vsize = pid * 1000u;
  stat.statm_resident = pid * 4096u;
  stat.starttime = pid * 7u;
  stat.io_read_bytes = pid * 3u;
  stat.io_write_bytes = pid * 4u;
  stat.read_time_ns = {static_cast<int64_t>(pid) * 1000000};
  stat.pid = pid;
  stat.ppid = 1;
  stat.last_cpu = pid % 4;
  stat.state = 'R';
  return stat;
}

void check_stat_eq(const ProcessStat &a, const ProcessStat &b) {
  CHECK(strcmp(a.comm, b.comm) == 0);
  CHECK(strcmp(a.cmdline, b.cmdline) == 0);
  CHECK(strcmp(a.wchan, b.wchan) == 0);
  CHECK(strcmp(a.username.data, b.username.data) == 0);
  CHECK(a.utime == b.utime);
  CHECK(a.stime == b.stime);
  CHECK(a.num_threads == b.num_threads);
  CHECK(a.nice == b.nice);
  CHECK(a.vsize == b.vsize);
  CHECK(a.statm_resident == b.statm_resident);
  CHECK(a.starttime == b.starttime);
  CHECK(a.io_read_bytes == b.io_read_bytes);
  CHECK(a.io_write_bytes == b.io_write_bytes);
  CHECK(a.read_time_ns.at_ns == b.read_time_ns.at_ns);
  CHECK(a.pid == b.pid);
  CHECK(a.ppid == b.ppid);
  CHECK(a.last_cpu == b.last_cpu);
  CHECK(a.state == b.state);
}

// Builds a snapshot whose nested arrays live in its own owner_arena, mirroring
// how the gathering thread produces one. Caller owns owner_arena.destroy().
UpdateSnapshot make_snapshot(uint64_t seed) {
  UpdateSnapshot snap = {};

  snap.stats = Array<ProcessStat>::create(snap.owner_arena, 2);
  snap.stats.data[0] = make_stat(static_cast<Pid>(100 + seed), "init",
                                 "/sbin/init", "wchan_a", "root");
  snap.stats.data[1] = make_stat(static_cast<Pid>(200 + seed), "sshd",
                                 "/usr/sbin/sshd -D", "wchan_b", "sshd");

  snap.cpu_stats = Array<CpuCoreStat>::create(snap.owner_arena, 3);
  snap.cpu_stats.data[0] = {seed + 1, seed + 2, seed + 3, seed + 4};
  snap.cpu_stats.data[1] = {seed + 5, seed + 6, seed + 7, seed + 8};
  snap.cpu_stats.data[2] = {seed + 9, seed + 10, seed + 11, seed + 12};

  snap.mem_info = {seed + 16000000u, seed + 8000000u};
  snap.disk_io_stats = {seed + 111u, seed + 222u};
  snap.net_io_stats = {seed + 333u, seed + 444u};
  snap.system_time = SystemTimePoint{std::chrono::nanoseconds{
      static_cast<int64_t>(1500000000000000000LL + seed)}};
  snap.at = {static_cast<int64_t>(987654321 + seed)};
  return snap;
}

// Compares the fields that serialize(UpdateSnapshot*) actually writes. "at" is
// carried by the record header, so it is checked by the caller, not here.
void check_snapshot_eq(UpdateSnapshot &a, UpdateSnapshot &b) {
  REQUIRE(a.stats.size == b.stats.size);
  for (uint32_t i = 0; i < a.stats.size; ++i)
    check_stat_eq(a.stats.data[i], b.stats.data[i]);

  REQUIRE(a.cpu_stats.size == b.cpu_stats.size);
  for (uint32_t i = 0; i < a.cpu_stats.size; ++i) {
    CHECK(a.cpu_stats.data[i].total == b.cpu_stats.data[i].total);
    CHECK(a.cpu_stats.data[i].busy == b.cpu_stats.data[i].busy);
    CHECK(a.cpu_stats.data[i].kernel == b.cpu_stats.data[i].kernel);
    CHECK(a.cpu_stats.data[i].interrupts == b.cpu_stats.data[i].interrupts);
  }

  CHECK(a.mem_info.mem_total == b.mem_info.mem_total);
  CHECK(a.mem_info.mem_available == b.mem_info.mem_available);
  CHECK(a.disk_io_stats.sectors_read == b.disk_io_stats.sectors_read);
  CHECK(a.disk_io_stats.sectors_written == b.disk_io_stats.sectors_written);
  CHECK(a.net_io_stats.bytes_received == b.net_io_stats.bytes_received);
  CHECK(a.net_io_stats.bytes_transmitted == b.net_io_stats.bytes_transmitted);
  CHECK(a.system_time.time_since_epoch().count() ==
        b.system_time.time_since_epoch().count());
}

} // namespace

TEST_CASE("serialize primitives round-trip") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  int32_t i32 = -123456;
  uint32_t u32 = 4000000000u;
  int64_t i64 = -9000000000000LL;
  uint64_t u64 = 18000000000000000000ULL;
  char ch = 'Z';

  auto w = fix.writer();
  serialize(&w, &i32);
  serialize(&w, &u32);
  serialize(&w, &i64);
  serialize(&w, &u64);
  serialize(&w, &ch);
  CHECK_FALSE(w.failed);

  int32_t r_i32 = 0;
  uint32_t r_u32 = 0;
  int64_t r_i64 = 0;
  uint64_t r_u64 = 0;
  char r_ch = 0;

  auto r = fix.reader();
  serialize(&r, &r_i32);
  serialize(&r, &r_u32);
  serialize(&r, &r_i64);
  serialize(&r, &r_u64);
  serialize(&r, &r_ch);
  CHECK_FALSE(r.failed);

  CHECK(r_i32 == i32);
  CHECK(r_u32 == u32);
  CHECK(r_i64 == i64);
  CHECK(r_u64 == u64);
  CHECK(r_ch == ch);
}

TEST_CASE("serialize raw stats round-trip") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  SUBCASE("CpuCoreStat") {
    CpuCoreStat in = {11, 22, 33, 44};
    auto w = fix.writer();
    serialize(&w, &in);
    CHECK_FALSE(w.failed);

    CpuCoreStat out = {};
    auto r = fix.reader();
    serialize(&r, &out);
    CHECK_FALSE(r.failed);
    CHECK(out.total == 11);
    CHECK(out.busy == 22);
    CHECK(out.kernel == 33);
    CHECK(out.interrupts == 44);
  }

  SUBCASE("MemInfo") {
    MemInfo in = {16000000, 8000000};
    auto w = fix.writer();
    serialize(&w, &in);
    MemInfo out = {};
    auto r = fix.reader();
    serialize(&r, &out);
    CHECK_FALSE(r.failed);
    CHECK(out.mem_total == 16000000);
    CHECK(out.mem_available == 8000000);
  }

  SUBCASE("DiskIoStat") {
    DiskIoStat in = {123, 456};
    auto w = fix.writer();
    serialize(&w, &in);
    DiskIoStat out = {};
    auto r = fix.reader();
    serialize(&r, &out);
    CHECK_FALSE(r.failed);
    CHECK(out.sectors_read == 123);
    CHECK(out.sectors_written == 456);
  }

  SUBCASE("NetIoStat") {
    NetIoStat in = {789, 1011};
    auto w = fix.writer();
    serialize(&w, &in);
    NetIoStat out = {};
    auto r = fix.reader();
    serialize(&r, &out);
    CHECK_FALSE(r.failed);
    CHECK(out.bytes_received == 789);
    CHECK(out.bytes_transmitted == 1011);
  }

  SUBCASE("SystemInfo") {
    SystemInfo in = {100, 4096, 1700000000};
    auto w = fix.writer();
    serialize(&w, &in);
    SystemInfo out = {};
    auto r = fix.reader();
    serialize(&r, &out);
    CHECK_FALSE(r.failed);
    CHECK(out.ticks_in_second == 100);
    CHECK(out.mem_page_size == 4096);
    CHECK(out.boot_time_epoch_sec == 1700000000);
  }
}

TEST_CASE("serialize SteadyTimeDataPoint round-trip") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  SteadyTimeDataPoint in = {1234567890};
  auto w = fix.writer();
  serialize(&w, &in);
  CHECK_FALSE(w.failed);

  SteadyTimeDataPoint out = {};
  auto r = fix.reader();
  serialize(&r, &out);
  CHECK_FALSE(r.failed);
  CHECK(out.at_ns == 1234567890);
}

TEST_CASE("serialize SystemTimePoint round-trip") {
  // Regression: the read-side reconstruct was gated on is_writing, dropping the
  // value on read.
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  SystemTimePoint in{std::chrono::nanoseconds{1500000000123456789LL}};
  auto w = fix.writer();
  serialize(&w, &in);
  CHECK_FALSE(w.failed);

  SystemTimePoint out{std::chrono::nanoseconds{0}};
  auto r = fix.reader();
  serialize(&r, &out);
  CHECK_FALSE(r.failed);
  CHECK(out.time_since_epoch().count() == 1500000000123456789LL);
}

TEST_CASE("serialize string field round-trip") {
  // Regression: write/read operated on the pointer, not the string bytes.
  SUBCASE("non-empty") {
    SerFixture fix;
    REQUIRE(fix.file != nullptr);

    const char *in = "hello world";
    auto w = fix.writer();
    serialize_with_limit(&w, &in, 128);
    CHECK_FALSE(w.failed);

    const char *out = nullptr;
    auto r = fix.reader();
    serialize_with_limit(&r, &out, 128);
    CHECK_FALSE(r.failed);
    REQUIRE(out != nullptr);
    CHECK(strcmp(out, "hello world") == 0);
  }

  SUBCASE("empty string") {
    SerFixture fix;
    REQUIRE(fix.file != nullptr);

    const char *in = "";
    auto w = fix.writer();
    serialize_with_limit(&w, &in, 128);
    CHECK_FALSE(w.failed);

    const char *out = nullptr;
    auto r = fix.reader();
    serialize_with_limit(&r, &out, 128);
    CHECK_FALSE(r.failed);
    REQUIRE(out != nullptr);
    CHECK(strcmp(out, "") == 0);
  }

  SUBCASE("over limit fails") {
    SerFixture fix;
    REQUIRE(fix.file != nullptr);

    const char *in = "abcdef"; // len 7 including the terminator
    auto w = fix.writer();
    serialize_with_limit(&w, &in, 4);
    CHECK(w.failed);
  }
}

TEST_CASE("serialize PersistentString round-trip interns") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  PersistentString in = {"myuser"};
  auto w = fix.writer();
  serialize_with_limit(&w, &in, 128);
  CHECK_FALSE(w.failed);

  PersistentString out = {};
  auto r = fix.reader();
  serialize_with_limit(&r, &out, 128);
  CHECK_FALSE(r.failed);
  REQUIRE(out.data != nullptr);
  CHECK(strcmp(out.data, "myuser") == 0);
  // The read value is interned, so it shares the canonical pointer.
  CHECK(out.data == fix.intern.intern("myuser").data);
}

TEST_CASE("serialize ProcessStat round-trip") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  ProcessStat in =
      make_stat(4242, "bash", "/usr/bin/bash -l", "do_wait", "root");
  auto w = fix.writer();
  serialize(&w, &in);
  CHECK_FALSE(w.failed);

  ProcessStat out = {};
  auto r = fix.reader();
  serialize(&r, &out);
  CHECK_FALSE(r.failed);
  check_stat_eq(in, out);
}

TEST_CASE("serialize_header validates the stream") {
  SUBCASE("valid header round-trips") {
    SerFixture fix;
    REQUIRE(fix.file != nullptr);

    auto w = fix.writer();
    serialize_header(&w);
    CHECK_FALSE(w.failed);

    auto r = fix.reader();
    serialize_header(&r);
    CHECK_FALSE(r.failed);
    CHECK(r.data_version == eSerVer_Latest);
  }

  SUBCASE("corrupted magic fails") {
    SerFixture fix;
    REQUIRE(fix.file != nullptr);

    auto w = fix.writer();
    serialize_header(&w);
    REQUIRE_FALSE(w.failed);

    // Clobber the magic word at the start of the stream.
    rewind(fix.file);
    uint32_t bad_magic = 0;
    fwrite(&bad_magic, sizeof(bad_magic), 1, fix.file);

    auto r = fix.reader();
    serialize_header(&r);
    CHECK(r.failed);
  }
}

TEST_CASE("UpdateSnapshot record round-trip") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  UpdateSnapshot snap = make_snapshot(0);

  auto w = fix.writer();
  serialize_header(&w);
  RecordHeader wh = {};
  wh.record_type = eSerRecordType_UpdateSnapshot;
  wh.at = &snap.at; // the record header carries the snapshot's own "at"
  serialize_record_header(&w, &wh);
  serialize(&w, &snap);
  serialize_record_footer(&w, &wh);
  CHECK_FALSE(w.failed);

  UpdateSnapshot out = {};
  auto r = fix.reader();
  serialize_header(&r);
  REQUIRE_FALSE(r.failed);
  CHECK(r.data_version == eSerVer_Latest);

  RecordHeader rh = {};
  rh.at = &out.at;
  serialize_record_header(&r, &rh);
  REQUIRE_FALSE(r.failed);
  CHECK(rh.record_type == eSerRecordType_UpdateSnapshot);
  serialize(&r, &out);
  serialize_record_footer(&r, &rh);
  CHECK_FALSE(r.failed);

  check_snapshot_eq(snap, out);
  CHECK(out.at.at_ns == snap.at.at_ns);
  CHECK(out.thread_snapshots.size == 0); // not serialized (yet)

  out.owner_arena.destroy();
  snap.owner_arena.destroy();
}

TEST_CASE("record length is back-patched to the body size") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  UpdateSnapshot snap = make_snapshot(0);

  auto w = fix.writer();
  serialize_header(&w);
  RecordHeader wh = {};
  wh.record_type = eSerRecordType_UpdateSnapshot;
  wh.at = &snap.at;
  serialize_record_header(&w, &wh);
  const long body_start = ftell(fix.file); // just past the len field
  serialize(&w, &snap);
  const long body_end = ftell(fix.file);
  serialize_record_footer(&w, &wh);
  CHECK_FALSE(w.failed);

  CHECK(wh.len > 0);
  CHECK(wh.len == static_cast<uint32_t>(body_end - body_start));

  snap.owner_arena.destroy();
}

TEST_CASE("record footer rejects a corrupted length on read") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  UpdateSnapshot snap = make_snapshot(0);

  auto w = fix.writer();
  serialize_header(&w);
  RecordHeader wh = {};
  wh.record_type = eSerRecordType_UpdateSnapshot;
  wh.at = &snap.at;
  serialize_record_header(&w, &wh);
  serialize(&w, &snap);
  serialize_record_footer(&w, &wh);
  REQUIRE_FALSE(w.failed);

  // Overwrite the back-patched length field with a wrong value.
  fseek(fix.file, wh.len_pos, SEEK_SET);
  uint32_t bad_len = wh.len + 1;
  fwrite(&bad_len, sizeof(bad_len), 1, fix.file);

  UpdateSnapshot out = {};
  auto r = fix.reader();
  serialize_header(&r);
  REQUIRE_FALSE(r.failed);
  RecordHeader rh = {};
  rh.at = &out.at;
  serialize_record_header(&r, &rh);
  serialize(&r, &out);
  serialize_record_footer(&r, &rh);
  CHECK(r.failed); // body length no longer matches the stored len

  out.owner_arena.destroy();
  snap.owner_arena.destroy();
}

TEST_CASE("multiple records round-trip and terminate at EOF") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  UpdateSnapshot s0 = make_snapshot(0);
  UpdateSnapshot s1 = make_snapshot(1000);

  UpdateSnapshot *snaps[2] = {&s0, &s1};
  auto w = fix.writer();
  serialize_header(&w);
  for (UpdateSnapshot *snap : snaps) {
    RecordHeader wh = {};
    wh.record_type = eSerRecordType_UpdateSnapshot;
    wh.at = &snap->at;
    serialize_record_header(&w, &wh);
    serialize(&w, snap);
    serialize_record_footer(&w, &wh);
  }
  CHECK_FALSE(w.failed);

  auto r = fix.reader();
  serialize_header(&r);
  REQUIRE_FALSE(r.failed);

  // The stream carries no record count; read until a header hits EOF.
  UpdateSnapshot outs[4] = {};
  int count = 0;
  while (count < 4) {
    RecordHeader rh = {};
    rh.at = &outs[count].at;
    serialize_record_header(&r, &rh);
    if (r.failed) break; // clean EOF
    CHECK(rh.record_type == eSerRecordType_UpdateSnapshot);
    serialize(&r, &outs[count]);
    serialize_record_footer(&r, &rh);
    if (r.failed) break;
    ++count;
  }

  REQUIRE(count == 2);
  check_snapshot_eq(s0, outs[0]);
  CHECK(outs[0].at.at_ns == s0.at.at_ns);
  check_snapshot_eq(s1, outs[1]);
  CHECK(outs[1].at.at_ns == s1.at.at_ns);

  for (UpdateSnapshot &o : outs)
    o.owner_arena.destroy();
  s0.owner_arena.destroy();
  s1.owner_arena.destroy();
}

TEST_CASE("reading past a truncated stream fails gracefully") {
  SerFixture fix;
  REQUIRE(fix.file != nullptr);

  // Header only, no records.
  auto w = fix.writer();
  serialize_header(&w);
  CHECK_FALSE(w.failed);

  auto r = fix.reader();
  serialize_header(&r);
  REQUIRE_FALSE(r.failed);

  SteadyTimeDataPoint at = {};
  RecordHeader rh = {};
  rh.at = &at;
  serialize_record_header(&r, &rh);
  CHECK(r.failed); // hit EOF, no crash
}
