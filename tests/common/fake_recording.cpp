#include "common/fake_recording.h"

#include "state/snapshot.h"

#include <cstdio>

constexpr int64_t NS_IN_SECOND = 1000 * 1000 * 1000;

// Recording starts an hour into the session
constexpr uint64_t UPTIME_BASE_SECONDS = 3600;

// Steady-clock origin of the recording. Callers pass offsets from the start of
// the scenario; those are shifted by this so `at` looks like a real recording's
// nanoseconds-since-boot. It cannot start near zero: brief_table_update stamps
// rows present at first sight with first_seen_ns = 0, and the draw side reads
// that as an age, so timestamps within the newborn window of zero would
// highlight the whole table as new.
constexpr int64_t SESSION_START_NS =
    static_cast<int64_t>(UPTIME_BASE_SECONDS) * NS_IN_SECOND;

static uint64_t ticks_for(const SystemInfo &system, const int64_t ns) {
  return static_cast<uint64_t>(ns) * system.ticks_in_second / NS_IN_SECOND;
}

static void sort_by_pid(GrowingArray<ProcessStat> &processes) {
  ProcessStat *data = processes.data();
  for (uint32_t i = 1; i < processes.size(); ++i) {
    for (uint32_t j = i; j > 0 && data[j].pid < data[j - 1].pid; --j) {
      const ProcessStat tmp = data[j];
      data[j] = data[j - 1];
      data[j - 1] = tmp;
    }
  }
}

FakeRecording FakeRecording::create(const SystemInfo &system,
                                    const uint32_t num_cores) {
  FakeRecording rec = {};
  rec.arena = BumpArena::create();
  rec.system = system;
  rec.uptime_ticks = UPTIME_BASE_SECONDS * system.ticks_in_second;

  rec.cpu_stats = Array<CpuCoreStat>::create(rec.arena, num_cores + 1);
  for (CpuCoreStat &core : rec.cpu_stats) {
    core = {};
  }

  rec.control.out_buffer = &rec.buffer;
  rec.control.is_writing = true;
  serialize_header(&rec.control);
  SystemInfo system_copy = system;
  serialize(&rec.control, &system_copy);
  return rec;
}

ProcessStat &FakeRecording::add(const ProcessStat &proc) {
  ProcessStat *slot = processes.emplace_back(arena);
  *slot = proc;
  if (slot->starttime == 0) {
    // Strictly past the previous record's uptime, which is what marks the
    // process as born inside the interval.
    slot->starttime = uptime_ticks + 1;
  }
  return *slot;
}

ProcessStat *FakeRecording::find(const Pid pid) {
  for (ProcessStat &proc : processes) {
    if (proc.pid == pid) return &proc;
  }
  return nullptr;
}

void FakeRecording::remove(const Pid pid) {
  const ProcessStat *found = find(pid);
  if (!found) return;

  ProcessStat *data = processes.data();
  const uint32_t index = static_cast<uint32_t>(found - data);
  const uint32_t tail = processes.size() - index - 1;
  if (tail > 0) {
    memmove(data + index, data + index + 1, tail * sizeof(ProcessStat));
  }
  processes.shrink_to(processes.size() - 1);
}

void FakeRecording::advance(const uint64_t utime, const uint64_t stime,
                            const uint64_t io_read, const uint64_t io_write) {
  for (ProcessStat &proc : processes) {
    proc.utime += utime;
    proc.stime += stime;
    proc.io_read_bytes += io_read;
    proc.io_write_bytes += io_write;
  }
}

void FakeRecording::record(const int64_t record_at_ns) {
  // System CPU ticks are a function of elapsed time, so they are derived here;
  // per-process counters are left to advance(), being the thing under test.
  const uint64_t elapsed_ticks = ticks_for(system, record_at_ns - at_ns);
  at_ns = record_at_ns;
  uptime_ticks = UPTIME_BASE_SECONDS * system.ticks_in_second +
                 ticks_for(system, record_at_ns);

  CpuCoreStat total = {};
  for (uint32_t i = 1; i < cpu_stats.size; ++i) {
    CpuCoreStat &core = cpu_stats.data[i];
    core.total += elapsed_ticks;
    core.busy += elapsed_ticks / 4;
    core.kernel += elapsed_ticks / 8;
    core.interrupts += elapsed_ticks / 16;
    total.total += core.total;
    total.busy += core.busy;
    total.kernel += core.kernel;
    total.interrupts += core.interrupts;
  }
  cpu_stats.data[0] = total;

  const int64_t at = SESSION_START_NS + record_at_ns;
  for (ProcessStat &proc : processes) {
    proc.read_time_ns = {at};
  }
  sort_by_pid(processes);

  UpdateSnapshot snapshot = {};
  snapshot.stats = processes.to_array();
  snapshot.cpu_stats = cpu_stats;
  snapshot.mem_info = {16u * 1024 * 1024, 8u * 1024 * 1024};
  snapshot.disk_io_stats = {elapsed_ticks * 32, elapsed_ticks * 16};
  snapshot.net_io_stats = {elapsed_ticks * 4096, elapsed_ticks * 2048};
  snapshot.uptime_ticks = uptime_ticks;
  snapshot.at = {at};
  snapshot.system_time = SystemTimePoint{std::chrono::nanoseconds{
      static_cast<int64_t>(system.boot_time_epoch_sec) * NS_IN_SECOND + at}};

  RecordHeader header = {};
  header.record_type = eSerRecordType_UpdateSnapshot;
  header.at = &snapshot.at;
  serialize_record_header(&control, &header);
  serialize(&control, &snapshot);
  serialize_record_footer(&control, &header);
}

bool FakeRecording::write(const char *path) {
  if (control.failed) return false;

  FILE *file = fopen(path, "wb");
  if (!file) return false;
  const bool ok = fwrite(buffer.data, buffer.size, 1, file) == 1;
  return fclose(file) == 0 && ok;
}

void FakeRecording::destroy() {
  buffer.destroy();
  arena.destroy();
  *this = {};
}
