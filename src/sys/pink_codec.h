#ifndef SYS_PINK_CODEC_H
#define SYS_PINK_CODEC_H

#include "sys/pink_library.h"

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

struct PinkAudioCodecInfo {
  uint32_t sampleRate = 0;
  uint32_t durationMs = 0;
  uint32_t dataBytes = 0;
  uint8_t channels = 0;
  uint8_t bitsPerSample = 0;
  char title[64] = {};
  char artist[64] = {};
};

/// Re-scans `/bin/codecs` for typed .pink libraries.
void pinkCodecRefresh();
/// Reports whether an installed audio codec declares this extension.
bool pinkCodecSupportsExtension(const char *extension);

/// Loaded audio decoder backed by one external .pink library.
class PinkAudioCodec {
public:
  PinkAudioCodec() = default;
  ~PinkAudioCodec();

  PinkAudioCodec(const PinkAudioCodec &) = delete;
  PinkAudioCodec &operator=(const PinkAudioCodec &) = delete;

  bool open(File &file, const char *extension, PinkAudioCodecInfo &info,
            char *error, size_t errorSize);
  size_t readPcm16(int16_t *frames, size_t frameCapacity);
  bool seekPcmFrame(uint32_t targetFrame, uint32_t *actualFrame = nullptr);
  void close();
  bool loaded() const { return decoder != nullptr; }

private:
  PinkLoadedLibrary library;
  const void *api = nullptr;
  void *decoder = nullptr;
};

#endif
