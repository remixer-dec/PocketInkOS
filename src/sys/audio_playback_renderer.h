#ifndef AUDIO_PLAYBACK_RENDERER_H
#define AUDIO_PLAYBACK_RENDERER_H

#include <stddef.h>
#include <stdint.h>

struct AudioPlaybackStream {
  void *user = nullptr;
  size_t (*fillPcm16)(void *user, int16_t *stereoFrames,
                      size_t frameCount) = nullptr;
};

struct AudioPlaybackConfig {
  uint32_t sampleRate = 16000;
  size_t chunkFrames = 512;
  uint8_t volumePercent = 80;
};

class AudioPlaybackRenderer {
public:
  bool begin(const AudioPlaybackStream &stream, const AudioPlaybackConfig &config,
             char *error, int errorSize);
  bool pump(char *error, int errorSize);
  void stop();

  void setVolumePercent(uint8_t percent);
  uint8_t volumePercent() const { return playbackConfig.volumePercent; }

  bool active() const { return playbackActive; }
  bool finished() const { return playbackFinished; }
  uint32_t sampleRate() const { return playbackConfig.sampleRate; }
  uint16_t lastPeak() const { return playbackLastPeak; }
  uint32_t chunksWritten() const { return playbackChunksWritten; }

private:
  static void taskEntry(void *user);
  void taskLoop();
  bool writeChunk(char *error, int errorSize);
  bool ensureCodec(char *error, int errorSize);
  bool ensureI2s(char *error, int errorSize);
  bool configureCodecClock();
  bool configureCodecFormat();
  bool startCodecDac();
  bool readCodecRegister(uint8_t reg, uint8_t &value);
  bool writeCodecRegister(uint8_t reg, uint8_t value);
  void standbyCodec();
  void stopI2s();
  void shutdownHardware();
  void setError(char *error, int errorSize, const char *message);

  AudioPlaybackStream playbackStream = {};
  AudioPlaybackConfig playbackConfig = {};
  int16_t *dmaBuffer = nullptr;
  void *taskHandle = nullptr;
  volatile bool playbackActive = false;
  volatile bool playbackFinished = false;
  volatile bool stopRequested = false;
  volatile bool taskRunning = false;
  volatile uint16_t playbackLastPeak = 0;
  volatile uint32_t playbackChunksWritten = 0;
};

#endif
