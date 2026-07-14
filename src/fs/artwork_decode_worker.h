#ifndef ARTWORK_DECODE_WORKER_H
#define ARTWORK_DECODE_WORKER_H

#include "fs/image_render_cache.h"

#include <Adafruit_GFX.h>
#include <FS.h>
#include <atomic>
#include <stdint.h>

/// Builds one embedded-artwork render cache on the non-UI core.
class ArtworkDecodeWorker {
public:
  ~ArtworkDecodeWorker();

  bool begin(const char *providerId, const char *path, uint32_t streamOffset,
             uint32_t byteSize, const char *extension, int16_t width,
             int16_t height, ImageRenderCachePool *cache);
  bool draw(Adafruit_GFX &gfx, int16_t x, int16_t y, int16_t width,
            int16_t height) const;
  /// Returns true once when a background attempt finishes.
  bool takeCompletion();
  void stop();

private:
  enum class Phase : uint8_t { Idle, Loading, Ready, Failed };

  static void taskEntry(void *user);
  void run();

  const char *providerId = nullptr;
  const char *path = nullptr;
  const char *extension = nullptr;
  File file;
  uint32_t streamOffset = 0;
  uint32_t byteSize = 0;
  int16_t width = 0;
  int16_t height = 0;
  ImageRenderCachePool *cache = nullptr;
  const ImageRenderCache *renderCache = nullptr;
  void *taskHandle = nullptr;
  std::atomic<Phase> phase{Phase::Idle};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> taskRunning{false};
  std::atomic<bool> completionPending{false};
};

#endif
