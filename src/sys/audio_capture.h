#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

struct AudioCaptureResult {
  uint8_t *data = nullptr;
  size_t length = 0;
  uint32_t sampleRate = 16000;
};

class AudioCapture {
public:
  bool beginPcm16(AudioCaptureResult &out, uint32_t maxDurationMs,
                  char *error, int errorSize);
  bool pumpPcm16(AudioCaptureResult &out, char *error, int errorSize);
  void finishPcm16(AudioCaptureResult &out);
  bool recordPcm16(AudioCaptureResult &out, uint32_t durationMs,
                   char *error, int errorSize);
  void release(AudioCaptureResult &result);

private:
  uint8_t *activeBuffer = nullptr;
  int16_t *dmaBuffer = nullptr;
  size_t activeCapacity = 0;
  size_t activeLength = 0;
  bool active = false;

  bool ensureCodec(char *error, int errorSize);
  bool ensureI2s(char *error, int errorSize);
  bool readPcmChunk(char *error, int errorSize);
  void shutdownHardware();
  void stopI2s();
  void standbyCodec();
  bool writeCodecRegister(uint8_t reg, uint8_t value);
  void setError(char *error, int errorSize, const char *message);
};

#endif
