#include "recorder.h"

#include "paths.h"
#include "state/serialize.h"
#include "state/snapshot.h"
#include "state/state.h"
#include "sync.h"
#include "views/common.h"
#include "views/view_state.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>

// ============================================================================
// Main/UI thread: serialize into an in-memory buffer and send to the recorder
// ============================================================================

static bool recorder_send(Sync &sync, const RecordCommand &cmd,
                          const uint64_t abort_session) {
  RecorderSync &rec = sync.recorder;
  bool pushed = false;
  {
    std::lock_guard<std::mutex> lock(sync.quit_mutex);
    pushed = rec.command_queue.push(cmd);
    if (!pushed && abort_session != 0) rec.abort_session.store(abort_session);
  }
  rec.request_cv.notify_one();
  return pushed;
}

void recorder_toggle(ViewState &view_state, const State &state, Sync &sync) {
  ZoneScoped;
  if (view_state.recorder.active) {
    view_state.recorder.active = false;
    RecordCommand cmd = {};
    cmd.type = eRecordCmd_Stop;
    cmd.session = view_state.recorder.session_id;
    recorder_send(sync, cmd, view_state.recorder.session_id);
    return;
  }

  const char *dir = view_state.preferences_state.recordings_dir;
  char default_dir[512];
  if (!dir || dir[0] == '\0') {
    recorder_default_dir(default_dir, sizeof(default_dir));
    dir = default_dir;
  }
  if (dir[0] == '\0') {
    notify_error(view_state.notifications, 0,
                 "No recordings folder: set one in Preferences.");
    return;
  }

  char timestamp[32];
  paths_format_time_suffix(timestamp, sizeof(timestamp));

  ++view_state.recorder.session_id;

  SerializeBuffer buffer = {};
  SerializeControl control = {};
  control.out_buffer = &buffer;
  control.is_writing = true;
  serialize_header(&control);
  SystemInfo system = state.system;
  serialize(&control, &system);

  RecordCommand cmd = {};
  cmd.type = eRecordCmd_Start;
  cmd.session = view_state.recorder.session_id;
  cmd.buffer = buffer;
  snprintf(cmd.path, sizeof(cmd.path), "%s/recording.%s.prck", dir, timestamp);

  if (!recorder_send(sync, cmd, 0)) {
    buffer.destroy();
    return;
  }
  view_state.recorder.active = true;
}

void recorder_update(ViewState &view_state, UpdateSnapshot &snapshot,
                     Sync &sync) {
  if (!view_state.recorder.active) return;
  ZoneScoped;

  RecordHeader header = {};
  header.record_type = eSerRecordType_UpdateSnapshot;
  SteadyTimeDataPoint at = snapshot.at;
  header.at = &at;

  SerializeBuffer buffer = {};
  buffer.reserve(view_state.recorder.capacity_hint);
  SerializeControl control = {};
  control.out_buffer = &buffer;
  control.is_writing = true;
  control.data_version = eSerVer_Latest;
  serialize_record_header(&control, &header);
  serialize(&control, &snapshot);
  serialize_record_footer(&control, &header);

  if (control.failed) {
    // A field/array exceeded its limit; the record is truncated. Drop it and
    // stop, finalizing the valid records already on disk.
    buffer.destroy();
    view_state.recorder.active = false;
    RecordCommand stop = {};
    stop.type = eRecordCmd_Stop;
    stop.session = view_state.recorder.session_id;
    recorder_send(sync, stop, view_state.recorder.session_id);
    notify_error(view_state.notifications, 0,
                 "Recording stopped: failed to serialize snapshot");
    return;
  }

  // Size the next record's buffer to ~1.2x this one so it almost never grows.
  view_state.recorder.capacity_hint = buffer.size + buffer.size / 5;

  RecordCommand cmd = {};
  cmd.type = eRecordCmd_Write;
  cmd.session = view_state.recorder.session_id;
  cmd.buffer = buffer;

  if (!recorder_send(sync, cmd, view_state.recorder.session_id)) {
    buffer.destroy();
    view_state.recorder.active = false;
  }
}

void recorder_drain_responses(ViewState &view_state, Sync &sync) {
  ZoneScoped;
  Notifications &notifications = view_state.notifications;
  RecordResponse r;
  while (sync.recorder.response_queue.pop(r)) {
    const bool current = r.session_id == view_state.recorder.session_id;
    switch (r.status) {
    case eRecordStatus_Started:
      notify_info(notifications, "Recording to %s", r.path);
      break;
    case eRecordStatus_Saved:
      notify_info_copy_path(notifications, r.path,
                            "Recording saved to %s (%u snapshots)", r.path,
                            r.snapshots);
      if (current) view_state.recorder.active = false;
      break;
    case eRecordStatus_StartError:
      notify_error(notifications, r.error_code,
                   "Cannot start recording to %s: %s", r.path,
                   strerror(r.error_code));
      if (current) view_state.recorder.active = false;
      break;
    case eRecordStatus_WriteError:
      // The partial file holds the records written so far, so offer Copy path.
      notify_error_copy_path(notifications, r.path,
                             "Recording failed after %u snapshots: %s",
                             r.snapshots, strerror(r.error_code));
      if (current) view_state.recorder.active = false;
      break;
    case eRecordStatus_Overflow:
      notify_error(notifications, 0,
                   "Recording stopped: disk can't keep up (%u snapshots)",
                   r.snapshots);
      if (current) view_state.recorder.active = false;
      break;
    }
  }
}

// ============================================================================
// Recorder thread: owns the FILE* and performs all disk I/O
// ============================================================================

// `file` is held open across queue iterations and closed on Stop:
// NOLINTBEGIN(clang-analyzer-unix.Stream)
void recorder_loop(Sync &sync) {
  RecorderSync &rec = sync.recorder;

  FILE *file = nullptr;
  char path[PATH_MAX] = {};
  uint64_t session = 0;
  uint32_t snapshots = 0;

  const auto push_response = [&](const RecordStatus status,
                                 const int error_code) {
    RecordResponse r = {};
    r.status = status;
    r.session_id = session;
    r.error_code = error_code;
    r.snapshots = snapshots;
    snprintf(r.path, sizeof(r.path), "%s", path);
    rec.response_queue.push(r);
    sock_notify_data_ready(sync);
  };

  const auto close_file = [&] {
    if (file) {
      fclose(file);
      file = nullptr;
    }
  };

  const auto discard_backlog = [&] {
    RecordCommand cmd;
    while (rec.command_queue.pop(cmd)) {
      cmd.buffer.destroy();
    }
  };

  while (!sync.quit.load()) {
    {
      std::unique_lock<std::mutex> lock(sync.quit_mutex);
      rec.request_cv.wait(lock, [&] {
        return sync.quit.load() || rec.abort_session.load() != 0 ||
               rec.command_queue.has_data();
      });
    }
    if (sync.quit.load()) break;

    // A non-zero abort_session force-stops that session. Close only if the open
    // file is that session's; a newer Start already (or concurrently) queued is
    // kept by the session-aware drain below.
    const uint64_t aborted = rec.abort_session.exchange(0);
    if (aborted != 0 && file && session == aborted) {
      push_response(eRecordStatus_Overflow, 0);
      close_file();
      snapshots = 0;
      path[0] = '\0';
    }

    RecordCommand cmd;
    while (rec.command_queue.pop(cmd)) {
      if (cmd.session <= aborted) {
        // stale command from an aborted/older session
        cmd.buffer.destroy();
        continue;
      }
      switch (cmd.type) {
      case eRecordCmd_Start: {
        ZoneScopedN("recorder: start");
        session = cmd.session;
        snprintf(path, sizeof(path), "%s", cmd.path);
        snapshots = 0;
        if (!paths_ensure_parent_dir(path)) {
          const int err = errno;
          cmd.buffer.destroy();
          push_response(eRecordStatus_StartError, err);
          break;
        }
        file = fopen(path, "wb");
        if (!file) {
          const int err = errno;
          cmd.buffer.destroy();
          push_response(eRecordStatus_StartError, err);
          break;
        }
        if (cmd.buffer.size > 0 &&
            fwrite(cmd.buffer.data, cmd.buffer.size, 1, file) != 1) {
          const int err = errno;
          cmd.buffer.destroy();
          close_file();
          remove(path);
          push_response(eRecordStatus_StartError, err);
          break;
        }
        cmd.buffer.destroy();
        push_response(eRecordStatus_Started, 0);
      } break;

      case eRecordCmd_Write: {
        ZoneScopedN("recorder: write");
        if (file) {
          if (cmd.buffer.size > 0 &&
              fwrite(cmd.buffer.data, cmd.buffer.size, 1, file) != 1) {
            const int err = errno;
            cmd.buffer.destroy();
            close_file();
            push_response(eRecordStatus_WriteError, err);
            break;
          }
          ++snapshots;
        }
        cmd.buffer.destroy();
      } break;

      case eRecordCmd_Stop: {
        ZoneScopedN("recorder: stop");
        cmd.buffer.destroy();
        if (file) {
          close_file();
          push_response(eRecordStatus_Saved, 0);
        }
      } break;
      }
    }
  }

  // Shutdown: nothing drains responses anymore, so just release resources.
  discard_backlog();
  close_file();
}
// NOLINTEND(clang-analyzer-unix.Stream)

void recorder_default_dir(char *out, const uint32_t size) {
  return paths_default_out_dir(out, size, "recordings");
}

const char *recorder_toggle_label(ViewState &view_state, char *buf,
                                  const size_t n) {
  snprintf(buf, n, "Toggle recording (now: %s)",
           view_state.recorder.active ? "ON" : "OFF");
  return buf;
}
