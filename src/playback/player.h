#pragma once

#include <atomic>
#include <cstdio>

struct Sync;
struct SystemInfo;
struct SerializeControl;

constexpr float PLAYBACK_SPEED_MAX = 1e9f;

struct PlaybackSync {
  std::atomic<float> speed{1.0f};
  std::atomic<bool> paused{false};
  std::atomic<bool> restart{false};
  std::atomic<bool> finished{false};
};

bool playback_read_header(SerializeControl &control, SystemInfo *out);

// Open `path`, read + validate its preamble into `*out`, and close it. Returns
// false if the file can't be opened or isn't a valid .prck recording.
bool playback_validate(const char *path, SystemInfo *out);

void playback_loop(Sync &sync, const char *path);
