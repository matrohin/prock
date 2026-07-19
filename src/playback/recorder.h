#pragma once

#include "base/channel.h"
#include "state/serialize.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <linux/limits.h>

struct ViewState;
struct UpdateSnapshot;
struct State;
struct Notifications;
struct Sync;

enum RecordCmdType : uint8_t {
  eRecordCmd_Start,
  eRecordCmd_Write,
  eRecordCmd_Stop,
};

struct RecordCommand {
  RecordCmdType type;
  uint64_t session;
  SerializeBuffer buffer;
  char path[PATH_MAX];
};

enum RecordStatus : uint8_t {
  eRecordStatus_Started,
  eRecordStatus_Saved,
  eRecordStatus_StartError,
  eRecordStatus_WriteError,
  eRecordStatus_Overflow,
};

struct RecordResponse {
  RecordStatus status;
  uint64_t session_id;
  int error_code;
  uint32_t snapshots;
  char path[PATH_MAX];
};

struct RecorderSync {
  Channel<RecordCommand, 16> command_queue;
  Channel<RecordResponse, 8> response_queue;
  std::atomic<uint64_t> abort_session; // force-stop this session; 0 = none
  std::condition_variable request_cv;
};

struct RecorderViewState {
  uint64_t session_id;
  uint32_t capacity_hint;
  bool active;
  bool toggle_request;
};

// Main/UI thread: start or stop recording
void recorder_toggle(ViewState &view_state, const State &state, Sync &sync);

// Main/UI thread: serialize the snapshot into a buffer and enqueue it
void recorder_update(ViewState &view_state, UpdateSnapshot &snapshot,
                     Sync &sync);

// Main/UI thread: drain writer results into notifications.
void recorder_drain_responses(ViewState &view_state, Sync &sync);

// Recorder thread entry point: owns the FILE* and performs all disk I/O.
void recorder_loop(Sync &sync);

void recorder_default_dir(char *out, uint32_t size);

const char *recorder_toggle_label(ViewState &view_state, char *buf, size_t n);
