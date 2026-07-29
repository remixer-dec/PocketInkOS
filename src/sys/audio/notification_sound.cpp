#include "sys/audio/notification_sound.h"

#include "fs/providers/resonatto/remidi_synth.h"
#include "sys/audio/audio_playback_renderer.h"
#include "sys/audio/audio_power.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr size_t kChunkFrames = 256;
constexpr unsigned long kPlaybackGraceMs = 1000;

struct CueEvent {
  uint16_t timeMs;
  uint8_t note;
  uint8_t velocity;
  uint8_t channel;
  bool on;
};

struct CueDefinition {
  const CueEvent *events;
  uint8_t eventCount;
  uint16_t durationMs;
};

constexpr CueEvent kChime[] = {
    {0, 72, 104, 0, true},    {110, 76, 108, 0, true},
    {180, 72, 0, 0, false},   {220, 79, 112, 0, true},
    {300, 76, 0, 0, false},   {440, 79, 0, 0, false},
};
constexpr CueEvent kDroplet[] = {
    {0, 91, 96, 0, true},    {70, 91, 0, 0, false},
    {90, 88, 100, 0, true},  {180, 88, 0, 0, false},
    {195, 84, 106, 0, true}, {340, 84, 0, 0, false},
};
constexpr CueEvent kPulse[] = {
    {0, 69, 108, 0, true},
    {80, 69, 0, 0, false},
    {145, 69, 108, 0, true},
    {225, 69, 0, 0, false},
};
constexpr CueEvent kAscend[] = {
    {0, 72, 98, 0, true},    {80, 72, 0, 0, false},
    {90, 74, 102, 0, true},  {170, 74, 0, 0, false},
    {180, 76, 106, 0, true}, {260, 76, 0, 0, false},
    {270, 79, 112, 0, true}, {400, 79, 0, 0, false},
};
constexpr CueEvent kKnock[] = {
    {0, 37, 112, 9, true},
    {130, 37, 118, 9, true},
};

template <size_t N>
constexpr CueDefinition cue(const CueEvent (&events)[N], uint16_t durationMs) {
  return {events, static_cast<uint8_t>(N), durationMs};
}

CueDefinition definitionFor(ConfigNotificationRingtone ringtone) {
  switch (ringtone) {
  case CONFIG_NOTIFICATION_DROPLET:
    return cue(kDroplet, 410);
  case CONFIG_NOTIFICATION_PULSE:
    return cue(kPulse, 300);
  case CONFIG_NOTIFICATION_ASCEND:
    return cue(kAscend, 470);
  case CONFIG_NOTIFICATION_KNOCK:
    return cue(kKnock, 300);
  case CONFIG_NOTIFICATION_CHIME:
    return cue(kChime, 520);
  }
  return cue(kChime, 520);
}

} // namespace

bool playNotificationSound(ConfigNotificationRingtone ringtone) {
  const CueDefinition cueDefinition = definitionFor(ringtone);
  remidi_event events[8] = {};
  if (cueDefinition.eventCount > sizeof(events) / sizeof(events[0])) {
    return false;
  }
  for (uint8_t i = 0; i < cueDefinition.eventCount; i++) {
    const CueEvent &source = cueDefinition.events[i];
    events[i].timeMs = source.timeMs;
    events[i].note = source.note;
    events[i].velocity = source.velocity;
    events[i].channel = source.channel;
    events[i].on = source.on;
  }

  remidi_sequence sequence = {};
  sequence.events = events;
  sequence.eventCount = cueDefinition.eventCount;
  sequence.info.durationMs = cueDefinition.durationMs;
  sequence.info.eventCount = cueDefinition.eventCount;
  for (uint8_t i = 0; i < cueDefinition.eventCount; i++) {
    if (cueDefinition.events[i].on) {
      sequence.info.noteOnCount++;
    }
  }

  RemidiSynth synth;
  if (!synth.begin(&sequence, kSampleRate)) {
    return false;
  }

  AudioPlaybackStream stream;
  stream.user = &synth;
  stream.fillPcm16 = remidiSynthFillPcm16;
  AudioPlaybackConfig config;
  config.sampleRate = kSampleRate;
  config.chunkFrames = kChunkFrames;
  config.volumePercent = 100;
  config.playWhenMutedAtMinimumVolume = true;

  audioPowerBegin();
  AudioPlaybackRenderer renderer;
  char error[32] = {};
  if (!renderer.begin(stream, config, error, sizeof(error))) {
    renderer.stop();
    return false;
  }

  const unsigned long deadline =
      millis() + cueDefinition.durationMs + kPlaybackGraceMs;
  bool healthy = true;
  while (renderer.active() &&
         static_cast<long>(millis() - deadline) < 0) {
    healthy = renderer.pump(error, sizeof(error));
    if (!healthy) {
      break;
    }
    delay(5);
  }
  const bool completed = healthy && renderer.finished();
  renderer.stop();
  return completed;
}
