#ifndef AUDIO_PLAYBACK_RENDERER_H
#define AUDIO_PLAYBACK_RENDERER_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/// Pull-based PCM source consumed by AudioPlaybackRenderer.
struct AudioPlaybackStream {
  /// Caller-owned state passed back to fillPcm16.
  void *user = nullptr;
  /// Fills stereoFrames with signed 16-bit interleaved stereo PCM frames.
  size_t (*fillPcm16)(void *user, int16_t *stereoFrames,
                      size_t frameCount) = nullptr;
};

/// Runtime audio playback settings.
struct AudioPlaybackConfig {
  /// Output sample rate in Hertz.
  uint32_t sampleRate = 16000;
  /// Number of stereo frames written per renderer chunk.
  size_t chunkFrames = 512;
  /// Source gain from 0 to 100 before device output volume and mute are applied.
  uint8_t volumePercent = 80;
  /// Lets alarm-like streams remain audible at minimum gain while muted.
  bool playWhenMutedAtMinimumVolume = false;
};

/// Streams signed 16-bit stereo PCM to the codec and I2S output path.
class AudioPlaybackRenderer {
public:
  /// Stops the renderer that currently owns the single physical codec output.
  static void stopActive();
  /// Starts playback from stream using config.
  bool begin(const AudioPlaybackStream &stream, const AudioPlaybackConfig &config,
             char *error, int errorSize);
  /// Advances playback setup and error handling; the audio task pulls PCM chunks.
  bool pump(char *error, int errorSize);
  /// Stops playback and releases audio hardware.
  void stop();

  /// Updates output gain for future chunks.
  void setVolumePercent(uint8_t percent);
  /// Returns the current output gain.
  uint8_t volumePercent() const { return playbackVolumePercent.load(); }

  /// Returns true while playback is active.
  bool active() const { return playbackActive.load(); }
  /// Returns true after the stream has produced no more PCM.
  bool finished() const { return playbackFinished.load(); }
  /// Returns the configured output sample rate.
  uint32_t sampleRate() const { return playbackConfig.sampleRate; }
  /// Returns the latest peak absolute PCM sample written to I2S.
  uint16_t lastPeak() const { return playbackLastPeak.load(); }
  /// Returns the number of chunks written since begin().
  uint32_t chunksWritten() const { return playbackChunksWritten.load(); }

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
  std::atomic<uint8_t> playbackVolumePercent{80};
  int16_t *dmaBuffer = nullptr;
  void *taskHandle = nullptr;
  std::atomic<bool> playbackActive{false};
  std::atomic<bool> playbackFinished{false};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> taskRunning{false};
  std::atomic<uint16_t> playbackLastPeak{0};
  std::atomic<uint32_t> playbackChunksWritten{0};
};

#endif
