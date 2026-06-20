#ifndef REMIDI_SYNTH_H
#define REMIDI_SYNTH_H

#include "fs/providers/resonatto/remidi.h"

#include <stddef.h>
#include <stdint.h>

class RemidiSynth {
public:
  bool begin(const remidi_sequence *sequence, uint32_t sampleRate);
  void reset();
  void seek(uint32_t positionMs);
  size_t render(int16_t *stereoFrames, size_t frameCount);

  uint32_t positionMs() const;
  bool finished() const;

private:
  struct Voice {
    bool active = false;
    bool percussion = false;
    uint8_t channel = 0;
    uint8_t note = 0;
    uint8_t kind = 0;
    float phase = 0.0f;
    float step = 0.0f;
    float toneStep = 0.0f;
    float currentGain = 0.0f;
    float targetGain = 0.0f;
    float lastNoise = 0.0f;
    uint32_t ageFrames = 0;
    uint32_t durationFrames = 0;
    uint32_t noiseState = 1;
    bool releasing = false;
  };

  static constexpr uint8_t kVoiceCount = 18;

  void applyEvent(const remidi_event &event);
  void silenceVoices();
  Voice *allocateVoice(uint8_t channel, uint8_t note);
  Voice *allocatePercussionVoice();
  Voice *findVoice(uint8_t channel, uint8_t note);
  float nextVoiceSample(Voice &voice);
  float nextPercussionSample(Voice &voice);

  const remidi_sequence *sequence = nullptr;
  uint32_t sampleRateHz = 16000;
  uint32_t nextEventIndex = 0;
  uint64_t positionFrames = 0;
  volatile uint32_t positionMsSnapshot = 0;
  Voice voices[kVoiceCount] = {};
};

size_t remidiSynthFillPcm16(void *user, int16_t *stereoFrames,
                             size_t frameCount);

#endif
