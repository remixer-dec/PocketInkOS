#include "fs/artwork_decode_worker.h"

#include "fs/file_provider.h"
#include "fs/providers/image_file_viewer.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority = 1;
// Arduino UI and audio output run on core 1. Core 0 is the spare worker core;
// audio decoding there has higher priority and can preempt artwork work.
constexpr BaseType_t kTaskCore = 0;
constexpr unsigned long kStopTimeoutMs = 1500;

} // namespace

ArtworkDecodeWorker::~ArtworkDecodeWorker() { stop(); }

bool ArtworkDecodeWorker::begin(const char *nextProviderId,
                                const char *nextPath,
                                uint32_t nextStreamOffset,
                                uint32_t nextByteSize,
                                const char *nextExtension, int16_t nextWidth,
                                int16_t nextHeight,
                                ImageRenderCachePool *nextCache) {
  if (phase.load() != Phase::Idle || nextPath == nullptr ||
      nextPath[0] == '\0' || nextByteSize == 0U || nextExtension == nullptr ||
      nextExtension[0] == '\0' || nextWidth <= 0 || nextHeight <= 0 ||
      nextCache == nullptr) {
    return false;
  }

  providerId = nextProviderId;
  path = nextPath;
  streamOffset = nextStreamOffset;
  byteSize = nextByteSize;
  extension = nextExtension;
  width = nextWidth;
  height = nextHeight;
  cache = nextCache;
  renderCache = nullptr;
  completionPending = false;
  stopRequested = false;
  file = openProviderPath(nextProviderId, nextPath);
  if (!file || file.isDirectory()) {
    file.close();
    return false;
  }
  phase = Phase::Loading;
  taskRunning = true;

  TaskHandle_t created = nullptr;
  if (xTaskCreatePinnedToCore(taskEntry, "artwork", kTaskStackBytes, this,
                              kTaskPriority, &created, kTaskCore) != pdPASS) {
    taskRunning = false;
    phase = Phase::Failed;
    completionPending = true;
    file.close();
    return false;
  }
  taskHandle = created;
  return true;
}

bool ArtworkDecodeWorker::draw(Adafruit_GFX &gfx, int16_t x, int16_t y,
                               int16_t drawWidth, int16_t drawHeight) const {
  if (phase.load() != Phase::Ready || drawWidth != width ||
      drawHeight != height) {
    return false;
  }
  return renderCache != nullptr &&
         renderCache->draw(gfx, x, y, ImageTransform{});
}

bool ArtworkDecodeWorker::takeCompletion() {
  return completionPending.exchange(false);
}

void ArtworkDecodeWorker::stop() {
  stopRequested = true;
  const unsigned long startedAt = millis();
  while (taskRunning && millis() - startedAt < kStopTimeoutMs) {
    delay(5);
  }
  if (taskRunning && taskHandle != nullptr) {
    vTaskDelete(static_cast<TaskHandle_t>(taskHandle));
    taskRunning = false;
  }
  taskHandle = nullptr;
  if (file) {
    file.close();
  }
  phase = Phase::Idle;
  completionPending = false;
  providerId = nullptr;
  path = nullptr;
  extension = nullptr;
  streamOffset = 0;
  byteSize = 0;
  width = 0;
  height = 0;
  cache = nullptr;
  renderCache = nullptr;
}

void ArtworkDecodeWorker::taskEntry(void *user) {
  ArtworkDecodeWorker *worker = static_cast<ArtworkDecodeWorker *>(user);
  if (worker != nullptr) {
    worker->run();
  }
  vTaskDelete(nullptr);
}

void ArtworkDecodeWorker::run() {
  bool decoded = cacheImageFileSlice(
      file, providerId, path, streamOffset, byteSize, extension, width, height,
      1, true, cache, &stopRequested);
  file.close();
  renderCache = decoded ? cache->onlyEntry() : nullptr;
  decoded = renderCache != nullptr;
  phase = decoded ? Phase::Ready : Phase::Failed;
  completionPending = true;
  taskRunning = false;
}
