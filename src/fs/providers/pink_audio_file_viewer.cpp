#include "fs/providers/pink_audio_file_viewer.h"

#include "fs/file_provider.h"
#include "fs/artwork_decode_worker.h"
#include "fs/providers/image_file_viewer.h"
#include "sys/audio/audio_decode_queue.h"
#include "sys/audio/audio_playback_renderer.h"
#include "sys/pink_codec.h"
#include "sys/psram_allocator.h"
#include "sys/services/device_controls.h"
#include "ui/components/audio_player_component.h"

#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr size_t kPathCapacity = 288;
constexpr size_t kScratchFrames = 1152;
constexpr unsigned long kProgressRedrawMs = 1000;
constexpr unsigned long kTrackFeedbackMs = 180;
constexpr uint32_t kArtworkPlaybackWarmupChunks = 8;

struct ViewerState {
  char providerId[12] = {};
  char path[kPathCapacity] = {};
  char extension[12] = {};
  File file;
  PinkAudioCodec codec;
  PinkAudioCodecInfo info;
  PinkAudioArtworkInfo artwork;
  ImageRenderCachePool artworkCache;
  ArtworkDecodeWorker artworkWorker;
  AudioPlaybackRenderer renderer;
  AudioDecodeQueue decodeQueue;
  int16_t *scratch = nullptr;
  std::atomic<uint32_t> framesPlayed{0};
  bool playing = false;
  bool reachedEnd = false;
  bool artworkInitialized = false;
  bool artworkFailed = false;
  bool artworkDecodeStarted = false;
  bool autoplayPending = true;
  bool playerDrawn = false;
  int16_t artworkTargetWidth = 0;
  int16_t artworkTargetHeight = 0;
  AudioPlayerAction pressedAction = AudioPlayerAction::None;
  unsigned long pressedUntil = 0;
  unsigned long nextProgressRedrawMs = 0;
};

ViewerState *state = nullptr;

AudioPlayerComponent &player() {
  static AudioPlayerComponent component;
  return component;
}

const char *artworkExtension(const char *mime) {
  if (mime == nullptr) {
    return "";
  }
  if (strcasecmp(mime, "image/jpeg") == 0 ||
      strcasecmp(mime, "image/jpg") == 0) {
    return "jpg";
  }
  if (strcasecmp(mime, "image/png") == 0) {
    return "png";
  }
  if (strcasecmp(mime, "image/webp") == 0) {
    return "webp";
  }
  return "";
}

bool renderArtwork(void *user, Adafruit_GFX &gfx, int16_t x, int16_t y,
                   int16_t width, int16_t height) {
  ViewerState *current = static_cast<ViewerState *>(user);
  if (current != nullptr) {
    current->artworkTargetWidth = width;
    current->artworkTargetHeight = height;
  }
  if (current == nullptr || !current->artworkInitialized ||
      current->artworkFailed || current->artwork.byteSize == 0U) {
    return false;
  }
  return current->artworkWorker.draw(gfx, x, y, width, height);
}

void startArtworkDecode(ViewerState &current) {
  if (!current.artworkInitialized || current.artworkFailed ||
      current.artworkDecodeStarted || current.artworkTargetWidth <= 0 ||
      current.artworkTargetHeight <= 0) {
    return;
  }
  if (current.playing &&
      current.renderer.chunksWritten() < kArtworkPlaybackWarmupChunks) {
    return;
  }
  current.artworkDecodeStarted = true;
  const char *extension = artworkExtension(current.artwork.mime);
  if (extension[0] == '\0' ||
      !current.artworkWorker.begin(
          current.providerId, current.path, current.artwork.streamOffset,
          current.artwork.byteSize, extension, current.artworkTargetWidth,
          current.artworkTargetHeight, &current.artworkCache)) {
    current.artworkFailed = true;
  }
}

void copyText(char *out, size_t outSize, const char *text) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  strncpy(out, text != nullptr ? text : "", outSize - 1U);
  out[outSize - 1U] = '\0';
}

void closeState() {
  if (state == nullptr) {
    return;
  }
  state->renderer.stop();
  state->decodeQueue.stop();
  state->artworkWorker.stop();
  state->codec.close();
  if (state->file) {
    state->file.close();
  }
  psram::release(state->scratch);
  state->scratch = nullptr;
  state->~ViewerState();
  psram::release(state);
  state = nullptr;
}

uint32_t positionMs(const ViewerState &current) {
  return current.info.sampleRate == 0
             ? 0
             : static_cast<uint32_t>(
                   (static_cast<uint64_t>(current.framesPlayed.load()) *
                    1000ULL) /
                   current.info.sampleRate);
}

size_t decodePcm(void *user, int16_t *frames, size_t frameCapacity) {
  ViewerState *current = static_cast<ViewerState *>(user);
  return current != nullptr
             ? current->codec.readPcm16(frames, frameCapacity)
             : 0;
}

bool warmSeekCache(void *user, uint16_t frameBudget) {
  ViewerState *current = static_cast<ViewerState *>(user);
  return current != nullptr && current->codec.warmSeekCache(frameBudget);
}

size_t fillPcm(void *user, int16_t *stereoFrames, size_t frameCapacity) {
  ViewerState *current = static_cast<ViewerState *>(user);
  if (current == nullptr || stereoFrames == nullptr || frameCapacity == 0U) {
    return 0;
  }
  const size_t frames =
      current->decodeQueue.readStereoPcm16(stereoFrames, frameCapacity);
  if (frames == 0U) {
    current->reachedEnd = true;
    return 0;
  }
  current->framesPlayed.fetch_add(static_cast<uint32_t>(frames));
  return frames;
}

bool startPlayback(ViewerState &current) {
  // Seed the index before decoding so sequential playback records every frame,
  // even when no idle warmup ran between opening and pressing play.
  current.codec.warmSeekCache(1U);
  AudioDecodeStream decodeStream;
  decodeStream.user = &current;
  decodeStream.readPcm16 = decodePcm;
  decodeStream.maintain = warmSeekCache;
  decodeStream.channels = current.info.channels;
  decodeStream.chunkFrames = kScratchFrames;
  if (!current.decodeQueue.begin(decodeStream)) {
    current.playing = false;
    return false;
  }
  AudioPlaybackStream stream{&current, fillPcm};
  AudioPlaybackConfig config;
  config.sampleRate = current.info.sampleRate;
  config.chunkFrames = kScratchFrames;
  config.volumePercent = 100;
  char error[48] = {};
  if (!current.renderer.begin(stream, config, error, sizeof(error))) {
    current.decodeQueue.stop();
    current.playing = false;
    return false;
  }
  current.playing = true;
  current.reachedEnd = false;
  current.nextProgressRedrawMs = millis() + kProgressRedrawMs;
  if (!current.artworkInitialized) {
    // Playback owns startup priority; cover metadata and decoding are enabled
    // only after the renderer is active.
    current.artworkInitialized = true;
    if (current.codec.artwork(current.artwork)) {
      if (!current.artworkCache.setCapacity(1U)) {
        current.artworkFailed = true;
      }
    } else {
      current.artworkFailed = true;
    }
  }
  return true;
}

void stopPlayback(ViewerState &current) {
  current.renderer.stop();
  current.decodeQueue.stop();
  current.playing = false;
  current.nextProgressRedrawMs = 0;
}

void pausePlayback(ViewerState &current) {
  current.renderer.stop();
  current.playing = false;
  current.nextProgressRedrawMs = 0;
}

bool reopenAt(ViewerState &current, uint32_t targetFrame) {
  current.codec.close();
  if (current.file) {
    current.file.close();
  }
  current.file = openProviderPath(current.providerId, current.path);
  char error[48] = {};
  PinkAudioCodecInfo reopened;
  if (!current.file ||
      !current.codec.open(current.file, current.extension, reopened, error,
                          sizeof(error))) {
    return false;
  }
  current.info = reopened;
  uint32_t skipped = 0;
  while (skipped < targetFrame) {
    const size_t remaining = targetFrame - skipped;
    const size_t take = remaining < kScratchFrames ? remaining : kScratchFrames;
    const size_t got = current.codec.readPcm16(current.scratch, take);
    if (got == 0) {
      break;
    }
    skipped += static_cast<uint32_t>(got);
  }
  current.framesPlayed = skipped;
  current.reachedEnd = false;
  return true;
}

bool seekTo(ViewerState &current, uint32_t targetMs) {
  stopPlayback(current);
  const uint32_t targetFrame = static_cast<uint32_t>(
      (static_cast<uint64_t>(targetMs) * current.info.sampleRate) / 1000ULL);
  uint32_t actualFrame = targetFrame;
  if (current.codec.seekPcmFrame(targetFrame, &actualFrame)) {
    current.framesPlayed = actualFrame;
    current.reachedEnd = false;
    return true;
  }
  return reopenAt(current, targetFrame);
}

bool matchesCodec(const char *, const char *extension) {
  return pinkCodecSupportsExtension(extension);
}

FileViewerOpenResult openViewer(const FileViewerRequest &request) {
  closeState();
  void *memory = psram::allocate(sizeof(ViewerState));
  if (memory == nullptr) {
    return FileViewerOpenResult::Failed;
  }
  state = new (memory) ViewerState();
  copyText(state->providerId, sizeof(state->providerId), request.providerId);
  copyText(state->path, sizeof(state->path), request.path);
  copyText(state->extension, sizeof(state->extension), request.extension);
  state->file = openProviderPath(request.providerId, request.path);
  state->scratch = static_cast<int16_t *>(
      psram::allocate(kScratchFrames * 2U * sizeof(int16_t)));
  char error[48] = {};
  if (!state->file || state->file.isDirectory() || state->scratch == nullptr ||
      !state->codec.open(state->file, request.extension, state->info, error,
                         sizeof(error))) {
    closeState();
    // A matching but unusable external module must not block a firmware codec.
    return FileViewerOpenResult::Failed;
  }
#if defined(ENABLE_EXTENDED) && ENABLE_EXTENDED
  // Prefer linked decoders over stale SD-card builds for formats available in
  // the extended firmware. Otherwise an old module silently hides decoder
  // fixes compiled into the firmware.
  const bool linkedFormat = strcasecmp(request.extension, "mp3") == 0 ||
                            strcasecmp(request.extension, "aac") == 0 ||
                            strcasecmp(request.extension, "m4a") == 0 ||
                            strcasecmp(request.extension, "wav") == 0;
  const bool mp3CallbacksMissing =
      strcasecmp(request.extension, "mp3") == 0 &&
      (!state->codec.supportsSeekCacheWarmup() ||
       !state->codec.supportsArtwork());
  if ((linkedFormat && !state->codec.supportsCurrentApi()) ||
      mp3CallbacksMissing) {
    closeState();
    return FileViewerOpenResult::Failed;
  }
#endif
  return FileViewerOpenResult::Opened;
}

void drawViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  if (state == nullptr) {
    gfx.setCursor(54, 92);
    gfx.print("OPEN FAILED");
    return;
  }
  char subtitle[48];
  if (state->info.artist[0] != '\0') {
    copyText(subtitle, sizeof(subtitle), state->info.artist);
  } else {
    snprintf(subtitle, sizeof(subtitle), "%lu Hz  %u CH",
             static_cast<unsigned long>(state->info.sampleRate),
             static_cast<unsigned>(state->info.channels));
  }
  char detail[48];
  if (state->playing) {
    snprintf(detail, sizeof(detail), "%lu Hz  PCM %u",
             static_cast<unsigned long>(state->info.sampleRate),
             static_cast<unsigned>(state->renderer.lastPeak()));
  } else {
    snprintf(detail, sizeof(detail), "%lu KB",
             static_cast<unsigned long>(state->info.dataBytes / 1024U));
  }
  const DeviceControlSnapshot controls = deviceControlsSnapshot();
  AudioPlayerState view;
  view.title = state->info.title[0] != '\0' ? state->info.title : runtime.title;
  view.subtitle = subtitle;
  view.detail = detail;
  view.playing = state->playing;
  view.pressedAction = state->pressedAction;
  view.volumePercent = controls.volume;
  view.muted = controls.muted;
  view.positionMs = positionMs(*state);
  view.durationMs = state->info.durationMs;
  view.canPrevious = runtime.hasPreviousTrack;
  view.canNext = runtime.hasNextTrack;
  view.renderArtwork = renderArtwork;
  view.artworkUser = state;
  player().draw(gfx, view, runtime.fullscreen);
  state->playerDrawn = true;
}

void scrollViewer(FileViewerRuntime &, int8_t) {}

bool touchViewer(FileViewerRuntime &runtime, const TouchPoint &point) {
  if (state == nullptr) {
    return false;
  }
  const AudioPlayerEvent event =
      player().hitTest(point, runtime.fullscreen, state->info.durationMs);
  if (event.action == AudioPlayerAction::PreviousTrack ||
      event.action == AudioPlayerAction::NextTrack) {
    state->pressedAction = event.action;
    state->pressedUntil = millis() + kTrackFeedbackMs;
  }
  switch (event.action) {
  case AudioPlayerAction::TogglePlayPause:
    state->autoplayPending = false;
    if (state->playing) {
      // Retain prefetched PCM across pause; seek and close stop the worker.
      pausePlayback(*state);
    } else {
      if (state->reachedEnd) {
        seekTo(*state, 0);
      }
      startPlayback(*state);
    }
    return true;
  case AudioPlayerAction::PreviousTrack:
    if (runtime.hasPreviousTrack) {
      runtime.trackNavigation = -1;
      return true;
    }
    return false;
  case AudioPlayerAction::NextTrack:
    if (runtime.hasNextTrack) {
      runtime.trackNavigation = 1;
      return true;
    }
    return false;
  case AudioPlayerAction::VolumeDown:
    volumeDown();
    return true;
  case AudioPlayerAction::VolumeUp:
    volumeUp();
    return true;
  case AudioPlayerAction::Seek: {
    FileViewerScopedSleepGuard guard(runtime.activity);
    const bool wasPlaying = state->playing;
    const uint32_t targetMs = static_cast<uint32_t>(
        (static_cast<uint64_t>(state->info.durationMs) * event.seekPermille) /
        1000U);
    if (seekTo(*state, targetMs) && wasPlaying) {
      startPlayback(*state);
    }
    runtime.offset = positionMs(*state);
    runtime.size = state->info.durationMs;
    return true;
  }
  case AudioPlayerAction::None:
    return false;
  }
  return false;
}

bool updateViewer(FileViewerRuntime &runtime) {
  if (state == nullptr) {
    return false;
  }
  runtime.offset = positionMs(*state);
  runtime.size = state->info.durationMs;
  const unsigned long now = millis();
  if (state->autoplayPending && state->playerDrawn) {
    state->autoplayPending = false;
    const bool started = startPlayback(*state);
    if (started) {
      startArtworkDecode(*state);
    }
    return true;
  }
  startArtworkDecode(*state);
  if (state->artworkWorker.takeCompletion()) {
    return true;
  }
  if (state->pressedAction != AudioPlayerAction::None &&
      static_cast<long>(now - state->pressedUntil) >= 0) {
    state->pressedAction = AudioPlayerAction::None;
    return true;
  }
  if (!state->playing) {
    if (!state->decodeQueue.started()) {
      state->codec.warmSeekCache(2U);
    }
    return false;
  }
  fileViewerKeepAwake(runtime.activity);
  char error[48] = {};
  if (!state->renderer.pump(error, sizeof(error)) ||
      !state->renderer.active()) {
    stopPlayback(*state);
    state->reachedEnd = true;
    return true;
  }
  if (now >= state->nextProgressRedrawMs) {
    state->nextProgressRedrawMs = now + kProgressRedrawMs;
    return true;
  }
  return false;
}

uint32_t visibleBytes(const FileViewerRuntime &) {
  return state != nullptr ? state->info.durationMs : 0;
}

bool togglePlayback(FileViewerRuntime &runtime) {
  if (state == nullptr) {
    return false;
  }
  state->autoplayPending = false;
  if (state->playing) {
    pausePlayback(*state);
  } else {
    if (state->reachedEnd) {
      seekTo(*state, 0);
    }
    startPlayback(*state);
  }
  runtime.offset = positionMs(*state);
  runtime.size = state->info.durationMs;
  return true;
}

} // namespace

const FileViewerExtension PINK_AUDIO_FILE_VIEWER = {
    "pink-audio", "AUDIO", nullptr, 0, openViewer, drawViewer,
    scrollViewer, touchViewer, updateViewer, visibleBytes, matchesCodec,
    closeState, togglePlayback};
