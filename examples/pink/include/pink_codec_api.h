#ifndef PINK_CODEC_API_H
#define PINK_CODEC_API_H

#include "pink_library_api.h"

static const uint16_t PINK_CODEC_ABI_VERSION = 1;

enum PinkCodecStatus : int8_t {
  PINK_CODEC_ERROR = -1,
  PINK_CODEC_UNSUPPORTED = -2,
  PINK_CODEC_END = 0,
  PINK_CODEC_OK = 1,
};

enum PinkCodecCapability : uint32_t {
  PINK_CODEC_CAP_SEEK = 1U << 0U,
  PINK_CODEC_CAP_METADATA = 1U << 1U,
};

struct PinkCodecSource {
  void *user;
  size_t (*read)(void *user, uint8_t *out, size_t count);
  bool (*seek)(void *user, uint32_t offset);
  uint32_t (*tell)(void *user);
  uint32_t (*size)(void *user);
};

struct PinkCodecHost {
  uint16_t abiVersion;
  uint16_t reserved;
  size_t hostSize;
  void *(*psramAlloc)(size_t bytes);
  void (*psramFree)(void *memory);
  void (*log)(const char *message);
  /// Optional low-latency workspace allocation. Check hostSize before use.
  void *(*internalAlloc)(size_t bytes);
  void (*internalFree)(void *memory);
  /// Optional in-place interleaved complex FP32 FFT. Inverse output is scaled.
  bool (*complexFft)(float *realImag, size_t complexCount, bool inverse);
};

struct PinkCodecInfo {
  uint32_t sampleRate;
  uint32_t durationMs;
  uint32_t dataBytes;
  uint8_t channels;
  uint8_t bitsPerSample;
  uint8_t reserved[2];
  char title[64];
  char artist[64];
};

struct PinkCodecArtwork {
  uint32_t streamOffset;
  uint32_t byteSize;
  char mime[32];
};

struct PinkCodecApi {
  uint16_t abiVersion;
  uint16_t reserved;
  size_t apiSize;
  int8_t (*open)(const PinkCodecHost *host, const PinkCodecSource *source,
                 const char *extension, void **decoder, PinkCodecInfo *info);
  size_t (*readPcm16)(void *decoder, int16_t *frames, size_t frameCapacity);
  bool (*seekPcmFrame)(void *decoder, uint32_t targetFrame,
                       uint32_t *actualFrame);
  void (*close)(void *decoder);
  /// Optional bounded seek-cache work. Returns true while more work remains.
  bool (*warmSeekCache)(void *decoder, uint16_t frameBudget);
  /// Optional encoded artwork location within the original source stream.
  bool (*getArtwork)(void *decoder, PinkCodecArtwork *artwork);
};

typedef const PinkCodecApi *(*PinkCodecEntryPoint)();

#define PINK_AUDIO_CODEC_MANIFEST(codecId, extensionList, capabilityFlags)     \
  extern "C" __attribute__((section(".pink_library"), used)) const          \
      PinkLibraryManifest pinkLibraryManifest = {                            \
          PINK_LIBRARY_MAGIC, PINK_LIBRARY_MANIFEST_VERSION,                  \
          sizeof(PinkLibraryManifest), PINK_LIBRARY_TYPE_AUDIO_CODEC,         \
          capabilityFlags, codecId, extensionList}

#endif
