#include "sys/video/record.h"

#include "sys/app_display.h"
#include "sys/global.h"
#include "sys/psram_allocator.h"
#include "sys/services/device_clock.h"
#include "sys/services/sd_storage.h"
#include "sys/video/avi_encoder.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <cstdio>
#include <new>

namespace {

constexpr uint32_t kFramesPerSecond = 2;
constexpr uint32_t kFrameIntervalMs = 1000U / kFramesPerSecond;
constexpr uint32_t kMaxDurationMs = 5U * 60U * 1000U;
constexpr uint32_t kMaxFrames =
    kFramesPerSecond * (kMaxDurationMs / 1000U);
constexpr size_t kSourceStrideBytes = EPD_WIDTH / 8U;
constexpr size_t kFramebufferBytes = kSourceStrideBytes * EPD_HEIGHT;

static_assert(EPD_WIDTH % 8 == 0,
              "Video recording requires byte-aligned display rows");

struct RecordingState {
  File file;
  avi::Encoder encoder;
  uint32_t startedAt = 0;
  uint32_t nextFrameAt = 0;
  char path[48] = {};
};

// Only this pointer is resident while recording is idle. File, encoder, path,
// and timing state are constructed on demand and released again on stop.
RecordingState *recording = nullptr;

void civilFromUnixDays(int64_t days, int &year, unsigned &month,
                       unsigned &day) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = static_cast<int>(yoe) + static_cast<int>(era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp + (mp < 10 ? 3 : -9);
  year += month <= 2;
}

void recordingPath(char *out, size_t outSize) {
  int64_t localUnix = 0;
  uint16_t millisIntoSecond = 0;
  if (!deviceClock.snapshotLocalUnixMillis(localUnix, millisIntoSecond)) {
    snprintf(out, outSize, "/videos/uptime-%010lu.avi",
             static_cast<unsigned long>(millis()));
    return;
  }

  int64_t days = localUnix / 86400;
  int64_t seconds = localUnix % 86400;
  if (seconds < 0) {
    seconds += 86400;
    days--;
  }
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromUnixDays(days, year, month, day);
  const unsigned hour = static_cast<unsigned>(seconds / 3600);
  const unsigned minute = static_cast<unsigned>((seconds / 60) % 60);
  const unsigned second = static_cast<unsigned>(seconds % 60);
  snprintf(out, outSize,
           "/videos/%04d%02u%02u-%02u%02u%02u-%03u.avi", year, month,
           day, hour, minute, second,
           static_cast<unsigned>(millisIntoSecond));
}

void destroyRecording(bool removeFile) {
  RecordingState *current = recording;
  recording = nullptr;
  if (current == nullptr) {
    return;
  }
  current->file.close();
  if (removeFile) {
    SD_MMC.remove(current->path);
  }
  current->~RecordingState();
  psram::release(current);
}

VideoRecordResult failAndDiscard(VideoRecordResult result) {
  destroyRecording(true);
  return result;
}

VideoRecordResult startRecording(const AppDisplay &display) {
  sdStorageRequestRefresh();
  sdStorageUpdate();
  if (!sdStorageMounted()) {
    return VideoRecordResult::NoSdCard;
  }

  const uint8_t *framebuffer = display.framebufferData();
  if (framebuffer == nullptr || display.framebufferSize() < kFramebufferBytes) {
    return VideoRecordResult::NoFramebuffer;
  }

  void *storage = psram::allocateOrDefault(sizeof(RecordingState));
  if (storage == nullptr) {
    return VideoRecordResult::NoMemory;
  }
  recording = new (storage) RecordingState();
  recordingPath(recording->path, sizeof(recording->path));

  SD_MMC.mkdir("/videos");
  recording->file = SD_MMC.open(recording->path, FILE_WRITE);
  if (!recording->file) {
    destroyRecording(false);
    return VideoRecordResult::OpenFailed;
  }

  avi::Config config;
  config.width = EPD_WIDTH;
  config.height = EPD_HEIGHT;
  config.framesPerSecond = kFramesPerSecond;
  // Keep the experimental two-color codec local to the recording service.
  config.compression = avi::Compression::Msrle4;
  if (!recording->encoder.begin(recording->file, config)) {
    const VideoRecordResult result =
        recording->encoder.lastError() == avi::Error::NoMemory
            ? VideoRecordResult::NoMemory
            : VideoRecordResult::WriteFailed;
    return failAndDiscard(result);
  }
  if (!recording->encoder.writeFrame(recording->file, framebuffer,
                                      display.framebufferSize(),
                                      kSourceStrideBytes)) {
    return failAndDiscard(VideoRecordResult::WriteFailed);
  }

  const uint32_t now = millis();
  recording->startedAt = now;
  recording->nextFrameAt = now + kFrameIntervalMs;
  return VideoRecordResult::Started;
}

} // namespace

VideoRecordResult videoRecordToggle(const AppDisplay &display) {
  return recording != nullptr ? videoRecordStop() : startRecording(display);
}

VideoRecordResult videoRecordUpdate(const AppDisplay &display) {
  if (recording == nullptr) {
    return VideoRecordResult::None;
  }

  const uint32_t now = millis();
  if (now - recording->startedAt >= kMaxDurationMs) {
    return videoRecordStop();
  }
  if (recording->encoder.frameCount() >= kMaxFrames ||
      static_cast<int32_t>(now - recording->nextFrameAt) < 0) {
    return VideoRecordResult::None;
  }

  const uint8_t *framebuffer = display.framebufferData();
  if (framebuffer == nullptr || display.framebufferSize() < kFramebufferBytes) {
    return failAndDiscard(VideoRecordResult::NoFramebuffer);
  }
  if (!recording->encoder.writeFrame(recording->file, framebuffer,
                                      display.framebufferSize(),
                                      kSourceStrideBytes)) {
    return failAndDiscard(VideoRecordResult::WriteFailed);
  }

  // Advance from the prior deadline rather than from `now`; a delayed main
  // loop catches up one frame per pass without a long burst of SD writes.
  recording->nextFrameAt += kFrameIntervalMs;
  return VideoRecordResult::None;
}

VideoRecordResult videoRecordStop() {
  if (recording == nullptr) {
    return VideoRecordResult::None;
  }
  const bool saved = recording->encoder.finish(recording->file);
  destroyRecording(!saved);
  return saved ? VideoRecordResult::Saved : VideoRecordResult::WriteFailed;
}

bool videoRecording() { return recording != nullptr; }

const char *videoRecordResultMessage(VideoRecordResult result) {
  switch (result) {
  case VideoRecordResult::None:
    return "";
  case VideoRecordResult::Started:
    return "RECORDING STARTED";
  case VideoRecordResult::Saved:
    return "VIDEO SAVED";
  case VideoRecordResult::NoSdCard:
    return "NO SD CARD";
  case VideoRecordResult::NoFramebuffer:
    return "VIDEO CAPTURE FAILED";
  case VideoRecordResult::NoMemory:
    return "NO VIDEO MEMORY";
  case VideoRecordResult::OpenFailed:
    return "VIDEO START FAILED";
  case VideoRecordResult::WriteFailed:
    return "VIDEO WRITE FAILED";
  }
  return "VIDEO FAILED";
}
