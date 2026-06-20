#include "fs/providers/resonatto/remidi_synth.h"

#include <cmath>

namespace {

constexpr float kBaseGain = 0.45f;
constexpr float kAttackStep = 0.02f;
constexpr float kReleaseStep = 0.0025f;

enum PercussionKind : uint8_t {
  PERCUSSION_KICK = 0,
  PERCUSSION_SNARE = 1,
  PERCUSSION_CLOSED_HAT = 2,
  PERCUSSION_OPEN_HAT = 3,
  PERCUSSION_TOM = 4,
  PERCUSSION_CRASH = 5,
  PERCUSSION_CLICK = 6,
};

float midiNoteFrequency(uint8_t note) {
  return 440.0f * powf(2.0f, (static_cast<int>(note) - 69) / 12.0f);
}

float triangle(float phase) {
  return phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
}

float square(float phase) {
  return phase < 0.5f ? 1.0f : -1.0f;
}

float clampUnit(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

uint32_t msToFrames(uint32_t ms, uint32_t sampleRate) {
  const uint64_t frames = (static_cast<uint64_t>(ms) * sampleRate) / 1000ULL;
  return frames > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(frames);
}

float nextNoise(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(static_cast<int32_t>(state & 0xffffU) - 32768) /
         32768.0f;
}

uint32_t framesToMs(uint64_t frames, uint32_t sampleRate) {
  if (sampleRate == 0) {
    return 0;
  }
  if (frames > UINT64_MAX / 1000ULL) {
    return UINT32_MAX;
  }
  const uint64_t ms = (frames * 1000ULL) / sampleRate;
  return ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ms);
}

} // namespace

bool RemidiSynth::begin(const remidi_sequence *newSequence,
                         uint32_t sampleRate) {
  sequence = newSequence != nullptr &&
                     (newSequence->eventCount == 0 || newSequence->events != nullptr)
                 ? newSequence
                 : nullptr;
  sampleRateHz = sampleRate == 0 ? 16000 : sampleRate;
  nextEventIndex = 0;
  positionFrames = 0;
  positionMsSnapshot = 0;
  silenceVoices();
  return sequence != nullptr;
}

void RemidiSynth::reset() {
  sequence = nullptr;
  sampleRateHz = 16000;
  nextEventIndex = 0;
  positionFrames = 0;
  positionMsSnapshot = 0;
  silenceVoices();
}

void RemidiSynth::seek(uint32_t positionMs) {
  nextEventIndex = 0;
  positionFrames =
      (static_cast<uint64_t>(positionMs) * sampleRateHz) / 1000ULL;
  positionMsSnapshot = positionMs;
  silenceVoices();
  if (sequence == nullptr) {
    return;
  }

  while (nextEventIndex < sequence->eventCount &&
         sequence->events[nextEventIndex].timeMs <= positionMs) {
    const remidi_event &event = sequence->events[nextEventIndex];
    if (!(event.channel == 9 && event.on &&
          event.timeMs + 800U < positionMs)) {
      applyEvent(event);
    }
    nextEventIndex++;
  }
}

size_t RemidiSynth::render(int16_t *stereoFrames, size_t frameCount) {
  if (sequence == nullptr || stereoFrames == nullptr || frameCount == 0) {
    return 0;
  }

  size_t rendered = 0;
  while (rendered < frameCount) {
    const uint32_t nowMs = framesToMs(positionFrames, sampleRateHz);
    while (nextEventIndex < sequence->eventCount &&
           sequence->events[nextEventIndex].timeMs <= nowMs) {
      applyEvent(sequence->events[nextEventIndex]);
      nextEventIndex++;
    }

    float mix = 0.0f;
    bool anyVoice = false;
    for (uint8_t i = 0; i < kVoiceCount; i++) {
      if (!voices[i].active) {
        continue;
      }
      anyVoice = true;
      mix += nextVoiceSample(voices[i]);
    }

    if (!anyVoice && nextEventIndex >= sequence->eventCount &&
        nowMs >= sequence->info.durationMs) {
      break;
    }

    if (mix > 1.0f) {
      mix = 1.0f;
    } else if (mix < -1.0f) {
      mix = -1.0f;
    }
    const int16_t sample = static_cast<int16_t>(mix * 32767.0f);
    stereoFrames[rendered * 2] = sample;
    stereoFrames[rendered * 2 + 1] = sample;
    rendered++;
    positionFrames++;
    positionMsSnapshot = framesToMs(positionFrames, sampleRateHz);
  }

  return rendered;
}

uint32_t RemidiSynth::positionMs() const {
  return positionMsSnapshot;
}

bool RemidiSynth::finished() const {
  if (sequence == nullptr) {
    return true;
  }
  if (nextEventIndex < sequence->eventCount) {
    return false;
  }
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (voices[i].active) {
      return false;
    }
  }
  return positionMs() >= sequence->info.durationMs;
}

void RemidiSynth::applyEvent(const remidi_event &event) {
  if (event.on) {
    const bool percussion = event.channel == 9;
    Voice *voice =
        percussion ? allocatePercussionVoice()
                   : allocateVoice(event.channel, event.note);
    if (voice == nullptr) {
      return;
    }

    const float velocity =
        clampUnit(static_cast<float>(event.velocity) / 127.0f);
    voice->active = true;
    voice->percussion = percussion;
    voice->channel = event.channel;
    voice->note = event.note;
    voice->phase = 0.0f;
    voice->step = midiNoteFrequency(event.note) / sampleRateHz;
    voice->toneStep = voice->step;
    voice->targetGain = velocity * kBaseGain;
    voice->currentGain = percussion ? voice->targetGain : 0.0f;
    voice->ageFrames = 0;
    voice->durationFrames = 0;
    voice->lastNoise = 0.0f;
    voice->noiseState =
        0x9e3779b9UL ^ (static_cast<uint32_t>(event.note) << 16) ^
        (static_cast<uint32_t>(event.velocity) << 8) ^
        static_cast<uint32_t>(positionFrames);
    voice->releasing = false;

    if (percussion) {
      switch (event.note) {
      case 35:
      case 36:
        voice->kind = PERCUSSION_KICK;
        voice->step = 92.0f / sampleRateHz;
        voice->durationFrames = msToFrames(220, sampleRateHz);
        break;
      case 38:
      case 40:
        voice->kind = PERCUSSION_SNARE;
        voice->step = 185.0f / sampleRateHz;
        voice->durationFrames = msToFrames(180, sampleRateHz);
        break;
      case 42:
      case 44:
        voice->kind = PERCUSSION_CLOSED_HAT;
        voice->durationFrames = msToFrames(65, sampleRateHz);
        break;
      case 46:
        voice->kind = PERCUSSION_OPEN_HAT;
        voice->durationFrames = msToFrames(260, sampleRateHz);
        break;
      case 45:
      case 47:
      case 48:
      case 50:
        voice->kind = PERCUSSION_TOM;
        voice->step = midiNoteFrequency(event.note + 12) / sampleRateHz;
        voice->durationFrames = msToFrames(260, sampleRateHz);
        break;
      case 49:
      case 51:
      case 57:
        voice->kind = PERCUSSION_CRASH;
        voice->durationFrames = msToFrames(700, sampleRateHz);
        break;
      default:
        voice->kind = PERCUSSION_CLICK;
        voice->durationFrames = msToFrames(95, sampleRateHz);
        break;
      }
    }
    return;
  }

  if (event.channel == 9) {
    return;
  }

  Voice *voice = findVoice(event.channel, event.note);
  if (voice != nullptr) {
    voice->releasing = true;
    voice->targetGain = 0.0f;
  }
}

void RemidiSynth::silenceVoices() {
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    voices[i] = Voice();
  }
}

RemidiSynth::Voice *RemidiSynth::allocateVoice(uint8_t channel, uint8_t note) {
  Voice *existing = findVoice(channel, note);
  if (existing != nullptr) {
    return existing;
  }
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (!voices[i].active) {
      return &voices[i];
    }
  }

  uint8_t quietest = 0;
  for (uint8_t i = 1; i < kVoiceCount; i++) {
    if (voices[i].releasing ||
        voices[i].currentGain < voices[quietest].currentGain) {
      quietest = i;
    }
  }
  return &voices[quietest];
}

RemidiSynth::Voice *RemidiSynth::allocatePercussionVoice() {
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (!voices[i].active) {
      return &voices[i];
    }
  }

  uint8_t oldestPercussion = 0;
  bool foundPercussion = false;
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (!voices[i].percussion) {
      continue;
    }
    if (!foundPercussion ||
        voices[i].ageFrames > voices[oldestPercussion].ageFrames) {
      oldestPercussion = i;
      foundPercussion = true;
    }
  }
  return foundPercussion ? &voices[oldestPercussion] : &voices[0];
}

RemidiSynth::Voice *RemidiSynth::findVoice(uint8_t channel, uint8_t note) {
  for (uint8_t i = 0; i < kVoiceCount; i++) {
    if (voices[i].active && voices[i].channel == channel &&
        voices[i].note == note) {
      return &voices[i];
    }
  }
  return nullptr;
}

float RemidiSynth::nextVoiceSample(Voice &voice) {
  if (!voice.active) {
    return 0.0f;
  }
  if (voice.percussion) {
    return nextPercussionSample(voice);
  }

  if (voice.releasing) {
    voice.currentGain -= kReleaseStep;
    if (voice.currentGain <= 0.0f) {
      voice = Voice();
      return 0.0f;
    }
  } else if (voice.currentGain < voice.targetGain) {
    voice.currentGain += kAttackStep;
    if (voice.currentGain > voice.targetGain) {
      voice.currentGain = voice.targetGain;
    }
  }

  const float body = triangle(voice.phase) * 0.75f + square(voice.phase) * 0.25f;
  const float sample = body * voice.currentGain;
  voice.phase += voice.step;
  if (voice.phase >= 1.0f) {
    voice.phase -= floorf(voice.phase);
  }
  return sample;
}

float RemidiSynth::nextPercussionSample(Voice &voice) {
  if (voice.durationFrames == 0 || voice.ageFrames >= voice.durationFrames) {
    voice = Voice();
    return 0.0f;
  }

  const float progress =
      static_cast<float>(voice.ageFrames) /
      static_cast<float>(voice.durationFrames);
  const float decay = (1.0f - progress) * (1.0f - progress);
  float noise = nextNoise(voice.noiseState);
  const float highNoise = (noise - voice.lastNoise) * 0.5f;
  voice.lastNoise = noise;
  float sample = 0.0f;

  switch (voice.kind) {
  case PERCUSSION_KICK:
    sample = triangle(voice.phase) * decay;
    if (voice.ageFrames < msToFrames(8, sampleRateHz)) {
      sample += noise * 0.18f;
    }
    voice.step *= 0.9995f;
    break;
  case PERCUSSION_SNARE:
    sample = noise * 0.75f * decay + triangle(voice.phase) * 0.25f * decay;
    break;
  case PERCUSSION_CLOSED_HAT:
    sample = highNoise * decay;
    break;
  case PERCUSSION_OPEN_HAT:
    sample = highNoise * decay * 0.8f;
    break;
  case PERCUSSION_TOM:
    sample = triangle(voice.phase) * decay;
    voice.step *= 0.9997f;
    break;
  case PERCUSSION_CRASH:
    sample = highNoise * decay * 0.7f + square(voice.phase) * decay * 0.12f;
    voice.step = 4200.0f / sampleRateHz;
    break;
  default:
    sample = noise * decay;
    break;
  }

  voice.phase += voice.step;
  if (voice.phase >= 1.0f) {
    voice.phase -= floorf(voice.phase);
  }
  voice.ageFrames++;
  return sample * voice.currentGain;
}

size_t remidiSynthFillPcm16(void *user, int16_t *stereoFrames,
                             size_t frameCount) {
  RemidiSynth *synth = static_cast<RemidiSynth *>(user);
  return synth != nullptr ? synth->render(stereoFrames, frameCount) : 0;
}
