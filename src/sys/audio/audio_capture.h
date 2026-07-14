#ifndef SYS_AUDIO_AUDIO_CAPTURE_H
#define SYS_AUDIO_AUDIO_CAPTURE_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/// Owned PCM recording buffer returned by AudioCapture.
struct AudioCaptureResult {
  /// PSRAM-backed buffer containing PCM bytes or a complete WAV file.
  uint8_t *data = nullptr;
  /// Number of valid bytes in data.
  size_t length = 0;
  /// Recording sample rate in Hertz.
  uint32_t sampleRate = 16000;
  /// True when data includes a RIFF/WAVE header.
  bool wav = false;
};

/// Captures microphone audio as signed 16-bit PCM.
class AudioCapture {
public:
  /// Starts a bounded PCM recording session.
  bool beginPcm16(AudioCaptureResult &out, uint32_t maxDurationMs,
                  char *error, int errorSize);
  /// Starts a bounded WAV recording with its header reserved in the same
  /// PSRAM allocation as the PCM payload.
  bool beginWav(AudioCaptureResult &out, uint32_t maxDurationMs,
                char *error, int errorSize);
  /// Pulls another PCM chunk into the active result buffer.
  bool pumpPcm16(AudioCaptureResult &out, char *error, int errorSize);
  /// Finalizes an active PCM recording without freeing its result.
  void finishPcm16(AudioCaptureResult &out);
  /// Records a complete fixed-duration PCM clip.
  bool recordPcm16(AudioCaptureResult &out, uint32_t durationMs,
                   char *error, int errorSize);
  /// Records a complete fixed-duration WAV clip.
  bool recordWav(AudioCaptureResult &out, uint32_t durationMs,
                 char *error, int errorSize);
  /// Releases the heap buffer owned by result.
  void release(AudioCaptureResult &result);

private:
  uint8_t *activeBuffer = nullptr;
  int16_t *dmaBuffer = nullptr;
  size_t activeCapacity = 0;
  std::atomic<size_t> activeLength{0};
  size_t activeDataOffset = 0;
  std::atomic<bool> active{false};
  bool activeWav = false;
  bool hardwareActive = false;
  void *taskHandle = nullptr;
  std::atomic<bool> taskRunning{false};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> captureFailed{false};

  bool begin(AudioCaptureResult &out, uint32_t maxDurationMs, bool wav,
             char *error, int errorSize);
  bool ensureCodec(char *error, int errorSize);
  bool ensureI2s(char *error, int errorSize);
  bool readPcmChunk(char *error, int errorSize);
  bool startCaptureTask(char *error, int errorSize);
  void stopCaptureTask();
  static void taskEntry(void *user);
  void taskLoop();
  void shutdownHardware();
  void stopI2s();
  void standbyCodec();
  bool readCodecRegister(uint8_t reg, uint8_t &value);
  bool writeCodecRegister(uint8_t reg, uint8_t value);
  void setError(char *error, int errorSize, const char *message);
};

#endif
