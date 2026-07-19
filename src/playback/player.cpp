#include "player.h"

#include "base/base.h"
#include "base/const_string.h"
#include "state/serialize.h"
#include "state/snapshot.h"
#include "state/system_info.h"
#include "sync.h"

#include "tracy/Tracy.hpp"

#include <chrono>
#include <mutex>

struct PlaybackState {
  BumpArena persistent_arena;
  InternTable interner;
};

bool playback_read_header(SerializeControl &control, SystemInfo *out) {
  serialize_header(&control);
  serialize(&control, out);
  return !control.failed;
}

bool playback_validate(const char *path, SystemInfo *out) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  SerializeControl control = {};
  control.file = file;
  control.is_writing = false;
  const bool ok = playback_read_header(control, out);
  fclose(file);
  return ok;
}

static void playback_wait(Sync &sync, const SteadyTimeDataPoint at,
                          const SteadyTimeDataPoint prev_at,
                          const bool have_prev) {
  const SteadyTimePoint wait_start = SteadyClock::now();
  for (;;) {
    if (sync.quit.load() || sync.playback.restart.load()) return;

    std::unique_lock<std::mutex> lock(sync.quit_mutex);
    if (sync.playback.paused.load()) {
      sync.quit_cv.wait(lock, [&] {
        return sync.quit.load() || sync.playback.restart.load() ||
               !sync.playback.paused.load();
      });
      continue;
    }

    const float speed = sync.playback.speed.load();
    if (!have_prev || speed >= PLAYBACK_SPEED_MAX) return;
    const double delta = at.elapsed_seconds(prev_at);
    if (delta <= 0.0) return;

    const auto wait_dur = std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(delta / speed));
    const SteadyTimePoint target = wait_start + wait_dur;
    if (SteadyClock::now() >= target) return;
    sync.quit_cv.wait_until(lock, target);
  }
}

static void playback_park(Sync &sync) {
  if (!sync.playback.finished.exchange(true)) {
    sock_notify_data_ready(sync);
  }
  std::unique_lock<std::mutex> lock(sync.quit_mutex);
  sync.quit_cv.wait(
      lock, [&] { return sync.quit.load() || sync.playback.restart.load(); });
}

void playback_loop(Sync &sync, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    sync.playback.finished.store(true);
    sock_notify_data_ready(sync);
    return;
  }

  PlaybackState pb = {};
  pb.interner = InternTable::create(&pb.persistent_arena);

  SerializeControl control = {};
  control.file = file;
  control.intern_table = &pb.interner;
  control.arena = &pb.persistent_arena;
  control.is_writing = false;

  SystemInfo sys = {};
  if (!playback_read_header(control, &sys)) {
    fclose(file);
    sync.playback.finished.store(true);
    sock_notify_data_ready(sync);
    return;
  }
  const long first_record_pos = ftell(file);

  while (!sync.quit.load()) {
    fseek(file, first_record_pos, SEEK_SET);
    clearerr(file);
    control.failed = false;
    sync.playback.restart.store(false);
    sync.playback.finished.store(false);

    bool have_prev = false;
    SteadyTimeDataPoint prev_at = {};

    while (!sync.quit.load() && !sync.playback.restart.load()) {
      ZoneScopedN("playback: read record");
      UpdateSnapshot snap = {};
      RecordHeader header = {};
      header.at = &snap.at; // header carries `at`; the body does not
      serialize_record_header(&control, &header);
      serialize(&control, &snap);
      serialize_record_footer(&control, &header);
      if (control.failed) {
        snap.owner_arena.destroy();
        break;
      }

      playback_wait(sync, snap.at, prev_at, have_prev);
      if (sync.quit.load() || sync.playback.restart.load()) {
        snap.owner_arena.destroy();
        break;
      }
      prev_at = snap.at;
      have_prev = true;

      bool pushed = false;
      while (!sync.quit.load() && !sync.playback.restart.load()) {
        pushed = sync.update_queue.push(snap);
        if (pushed) break;
        std::unique_lock<std::mutex> lock(sync.quit_mutex);
        sync.quit_cv.wait_for(lock, std::chrono::milliseconds(5), [&] {
          return sync.quit.load() || sync.playback.restart.load();
        });
      }
      if (pushed) {
        sock_notify_data_ready(sync);
      } else {
        snap.owner_arena.destroy();
      }
    }

    if (!sync.quit.load() && !sync.playback.restart.load()) {
      playback_park(sync);
    }
  }

  fclose(file);
}
