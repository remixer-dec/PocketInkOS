#include "fs/providers/midi_file_viewer.h"

#include "fs/file_provider.h"
#include "fs/providers/resonatto/remidi.h"
#include "fs/providers/resonatto/remidi_synth.h"
#include "sys/audio_playback_renderer.h"
#include "ui/components/audio_player_component.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

static const uint8_t kVolumeStep = 5;
static const uint8_t kDefaultVolume = 40;
static const uint16_t kPathCapacity = 288;
static const uint32_t kPlaybackSampleRate = 16000;
static const unsigned long kProgressRedrawMs = 5000;

struct MidiViewerState {
  char providerId[12] = {};
  char path[kPathCapacity] = {};
  remidi_sequence sequence = {};
  bool loaded = false;
  bool playing = false;
  uint8_t volumePercent = kDefaultVolume;
  uint32_t positionMs = 0;
  unsigned long nextProgressRedrawMs = 0;
  RemidiSynth synth;
  AudioPlaybackRenderer renderer;
};

MidiViewerState &midiState() {
  static MidiViewerState state;
  return state;
}

AudioPlayerComponent &playerComponent() {
  static AudioPlayerComponent component;
  return component;
}

bool copyText(char *dest, size_t destSize, const char *source) {
  if (dest == nullptr || destSize == 0) {
    return false;
  }
  const char *text = source != nullptr ? source : "";
  const size_t length = strlen(text);
  const size_t copyLength = length < destSize ? length : destSize - 1;
  if (copyLength > 0) {
    memcpy(dest, text, copyLength);
  }
  dest[copyLength] = '\0';
  return length < destSize;
}

bool sameFile(const MidiViewerState &state, const char *providerId,
              const char *path) {
  return state.loaded && providerId != nullptr && path != nullptr &&
         strcmp(state.providerId, providerId) == 0 &&
         strcmp(state.path, path) == 0;
}

int readFileByte(void *user) {
  File *file = static_cast<File *>(user);
  return file != nullptr ? file->read() : -1;
}

uint8_t rendererVolumeForUi(uint8_t uiVolume) {
  if (uiVolume == 0) {
    return 0;
  }
  if (uiVolume >= 100) {
    return 100;
  }

  const float normalized = static_cast<float>(uiVolume) / 100.0f;
  const int mapped =
      static_cast<int>(powf(normalized, 2.5129f) * 100.0f + 0.5f);
  return static_cast<uint8_t>(mapped < 1 ? 1 : mapped);
}

void stopPlayback(MidiViewerState &state) {
  state.renderer.stop();
  state.playing = false;
  state.nextProgressRedrawMs = 0;
}

void resetState(MidiViewerState &state) {
  stopPlayback(state);
  remidi_free_sequence(&state.sequence);
  state.synth.reset();
  state = MidiViewerState();
}

bool loadMidiSequence(const char *providerId, const char *path,
                      remidi_sequence &sequence) {
  File file = openProviderPath(providerId, path);
  if (!file || file.isDirectory()) {
    return false;
  }
  remidi_reader reader;
  reader.user = &file;
  reader.read = readFileByte;
  return remidi_read_sequence(&reader, &sequence) == REMIDI_OK;
}

bool startPlayback(MidiViewerState &state) {
  if (!state.loaded || state.sequence.info.durationMs == 0) {
    return false;
  }

  if (!state.synth.begin(&state.sequence, kPlaybackSampleRate)) {
    state.playing = false;
    return false;
  }
  state.synth.seek(state.positionMs);

  AudioPlaybackStream stream;
  stream.user = &state.synth;
  stream.fillPcm16 = remidiSynthFillPcm16;

  AudioPlaybackConfig config;
  config.sampleRate = kPlaybackSampleRate;
  config.chunkFrames = 256;
  config.volumePercent = rendererVolumeForUi(state.volumePercent);

  char error[32];
  if (!state.renderer.begin(stream, config, error, sizeof(error))) {
    state.playing = false;
    return false;
  }

  state.playing = true;
  state.nextProgressRedrawMs = millis() + kProgressRedrawMs;
  return true;
}

bool togglePlayback(FileViewerRuntime &runtime, MidiViewerState &state) {
  if (state.sequence.info.durationMs == 0) {
    return true;
  }
  if (state.playing) {
    state.positionMs = state.synth.positionMs();
    stopPlayback(state);
  } else {
    if (state.positionMs >= state.sequence.info.durationMs) {
      state.positionMs = 0;
    }
    startPlayback(state);
  }
  runtime.offset = state.positionMs;
  runtime.size = state.sequence.info.durationMs;
  return true;
}

void syncPosition(MidiViewerState &state) {
  if (state.playing) {
    state.positionMs = state.synth.positionMs();
    if (!state.renderer.active() || state.synth.finished()) {
      state.positionMs = state.sequence.info.durationMs;
      stopPlayback(state);
    }
  }
  if (state.positionMs > state.sequence.info.durationMs) {
    state.positionMs = state.sequence.info.durationMs;
  }
}

bool ensureState(const char *providerId, const char *path) {
  MidiViewerState &state = midiState();
  if (sameFile(state, providerId, path)) {
    return true;
  }

  remidi_sequence sequence;
  if (!loadMidiSequence(providerId, path, sequence)) {
    return false;
  }

  resetState(state);
  copyText(state.providerId, sizeof(state.providerId), providerId);
  copyText(state.path, sizeof(state.path), path);
  state.sequence = sequence;
  state.loaded = true;
  state.volumePercent = kDefaultVolume;
  return true;
}

void formatSubtitle(const remidi_info &info, char *out, size_t outSize) {
  snprintf(out, outSize, "FMT %u  TRK %u  TPQ %u",
           static_cast<unsigned>(info.format),
           static_cast<unsigned>(info.trackCount),
           static_cast<unsigned>(info.division));
}

void formatDetail(const remidi_info &info, char *out, size_t outSize) {
  snprintf(out, outSize, "NOTES %lu  EVENTS %lu  TEMPO %u",
           static_cast<unsigned long>(info.noteOnCount),
           static_cast<unsigned long>(info.eventCount),
           static_cast<unsigned>(info.tempoChangeCount));
}

FileViewerOpenResult openMidiViewer(const FileViewerRequest &request) {
  return ensureState(request.providerId, request.path)
             ? FileViewerOpenResult::Opened
             : FileViewerOpenResult::Failed;
}

void drawMidiViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  if (!ensureState(runtime.providerId, runtime.path)) {
    gfx.setCursor(54, 92);
    gfx.print("OPEN FAILED");
    return;
  }

  MidiViewerState &state = midiState();
  syncPosition(state);

  char subtitle[40];
  char detail[44];
  formatSubtitle(state.sequence.info, subtitle, sizeof(subtitle));
  if (state.playing) {
    snprintf(detail, sizeof(detail), "NOTES %lu  PCM %u",
             static_cast<unsigned long>(state.sequence.info.noteOnCount),
             static_cast<unsigned>(state.renderer.lastPeak()));
  } else {
    formatDetail(state.sequence.info, detail, sizeof(detail));
  }

  AudioPlayerState playerState;
  playerState.title = runtime.title;
  playerState.subtitle = subtitle;
  playerState.detail = detail;
  playerState.playing = state.playing;
  playerState.volumePercent = state.volumePercent;
  playerState.positionMs = state.positionMs;
  playerState.durationMs = state.sequence.info.durationMs;
  playerComponent().draw(gfx, playerState, runtime.fullscreen);
}

void scrollMidiViewer(FileViewerRuntime &, int8_t) {}

bool touchMidiViewer(FileViewerRuntime &runtime, const TouchPoint &point) {
  if (!ensureState(runtime.providerId, runtime.path)) {
    return false;
  }

  MidiViewerState &state = midiState();
  syncPosition(state);
  const AudioPlayerEvent event =
      playerComponent().hitTest(point, runtime.fullscreen,
                                state.sequence.info.durationMs);
  switch (event.action) {
  case AudioPlayerAction::TogglePlayPause:
    return togglePlayback(runtime, state);
  case AudioPlayerAction::VolumeDown:
    state.volumePercent =
        state.volumePercent > kVolumeStep ? state.volumePercent - kVolumeStep : 0;
    state.renderer.setVolumePercent(rendererVolumeForUi(state.volumePercent));
    return true;
  case AudioPlayerAction::VolumeUp:
    state.volumePercent = state.volumePercent + kVolumeStep < 100
                              ? state.volumePercent + kVolumeStep
                              : 100;
    state.renderer.setVolumePercent(rendererVolumeForUi(state.volumePercent));
    return true;
  case AudioPlayerAction::Seek: {
    state.positionMs = static_cast<uint32_t>((static_cast<uint64_t>(
                                                  state.sequence.info.durationMs) *
                                              event.seekPermille) /
                                             1000U);
    const bool wasPlaying = state.playing;
    stopPlayback(state);
    if (!state.synth.begin(&state.sequence, kPlaybackSampleRate)) {
      state.playing = false;
      runtime.offset = state.positionMs;
      runtime.size = state.sequence.info.durationMs;
      return true;
    }
    state.synth.seek(state.positionMs);
    if (wasPlaying) {
      startPlayback(state);
    }
    runtime.offset = state.positionMs;
    runtime.size = state.sequence.info.durationMs;
    return true;
  }
  case AudioPlayerAction::None:
    return false;
  }
  return false;
}

bool updateMidiViewer(FileViewerRuntime &runtime) {
  if (!ensureState(runtime.providerId, runtime.path)) {
    return false;
  }

  MidiViewerState &state = midiState();
  runtime.size = state.sequence.info.durationMs;
  runtime.offset = state.positionMs;
  if (!state.playing) {
    return false;
  }

  fileViewerKeepAwake(runtime.activity);
  char error[32];
  if (!state.renderer.pump(error, sizeof(error))) {
    stopPlayback(state);
    runtime.offset = state.positionMs;
    return true;
  }
  const bool wasPlaying = state.playing;
  syncPosition(state);
  runtime.offset = state.positionMs;
  runtime.size = state.sequence.info.durationMs;
  if (wasPlaying && !state.playing) {
    return true;
  }

  const unsigned long now = millis();
  if (state.nextProgressRedrawMs == 0 || now >= state.nextProgressRedrawMs) {
    state.nextProgressRedrawMs = now + kProgressRedrawMs;
    return true;
  }
  return false;
}

uint32_t midiVisibleBytes(const FileViewerRuntime &runtime) {
  return runtime.size;
}

const char *const MIDI_VIEWER_EXTENSIONS[] = {"mid", "midi", "kar"};

} // namespace

uint32_t midiViewerDurationMs(const char *providerId, const char *path) {
  if (!ensureState(providerId, path)) {
    return 0;
  }
  return midiState().sequence.info.durationMs;
}

bool midiViewerTogglePlayback(FileViewerRuntime &runtime) {
  if (!ensureState(runtime.providerId, runtime.path)) {
    return false;
  }
  MidiViewerState &state = midiState();
  syncPosition(state);
  return togglePlayback(runtime, state);
}

void midiViewerClose() {
  resetState(midiState());
}

const FileViewerExtension MIDI_FILE_VIEWER = {
    "midi",
    "MIDI",
    MIDI_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(MIDI_VIEWER_EXTENSIONS) /
                         sizeof(MIDI_VIEWER_EXTENSIONS[0])),
    openMidiViewer,
    drawMidiViewer,
    scrollMidiViewer,
    touchMidiViewer,
    updateMidiViewer,
    midiVisibleBytes,
};
