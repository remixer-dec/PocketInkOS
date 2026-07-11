#include "sys/services/screenshot.h"

#include "sys/app_display.h"
#include "sys/services/device_clock.h"
#include "sys/global.h"
#include "sys/services/sd_storage.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <cstdio>

namespace {

constexpr uint32_t kBmpHeaderBytes = 14;
constexpr uint32_t kDibHeaderBytes = 40;
constexpr uint32_t kPaletteBytes = 8;
static_assert(EPD_WIDTH % 8 == 0,
              "The monochrome BMP exporter requires byte-aligned rows");
constexpr uint32_t kPixelOffset =
    kBmpHeaderBytes + kDibHeaderBytes + kPaletteBytes;
constexpr uint32_t kSourceRowBytes = EPD_WIDTH / 8U;
constexpr uint32_t kBmpRowBytes = (kSourceRowBytes + 3U) & ~3U;
constexpr uint32_t kImageBytes = kBmpRowBytes * EPD_HEIGHT;
constexpr uint32_t kFileBytes = kPixelOffset + kImageBytes;

void writeLe16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xffU);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
}

void writeLe32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xffU);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xffU);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xffU);
}

bool writeBytes(File &file, const uint8_t *bytes, size_t count) {
  return bytes != nullptr && file.write(bytes, count) == count;
}

void civilFromUnixDays(int64_t days, int &year, unsigned &month,
                       unsigned &day) {
  // Civil-date conversion for a Unix-day count, independent of libc timezone.
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

void screenshotPath(char *out, size_t outSize) {
  int64_t localUnix = 0;
  uint16_t millisIntoSecond = 0;
  if (!deviceClock.snapshotLocalUnixMillis(localUnix, millisIntoSecond)) {
    snprintf(out, outSize, "/screenshots/uptime-%010lu.bmp",
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
           "/screenshots/%04d%02u%02u-%02u%02u%02u-%03u.bmp", year, month,
           day, hour, minute, second,
           static_cast<unsigned>(millisIntoSecond));
}

} // namespace

ScreenshotResult saveFramebufferScreenshot(const AppDisplay &display) {
  sdStorageRequestRefresh();
  sdStorageUpdate();
  if (!sdStorageMounted()) {
    return ScreenshotResult::NoSdCard;
  }

  const uint8_t *framebuffer = display.framebufferData();
  if (framebuffer == nullptr || display.framebufferSize() <
                                 static_cast<size_t>(kSourceRowBytes * EPD_HEIGHT)) {
    return ScreenshotResult::NoFramebuffer;
  }

  SD_MMC.mkdir("/screenshots");
  char path[48];
  screenshotPath(path, sizeof(path));
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return ScreenshotResult::WriteFailed;
  }

  uint8_t header[kPixelOffset] = {};
  header[0] = 'B';
  header[1] = 'M';
  writeLe32(header + 2, kFileBytes);
  writeLe32(header + 10, kPixelOffset);
  writeLe32(header + 14, kDibHeaderBytes);
  writeLe32(header + 18, EPD_WIDTH);
  writeLe32(header + 22, EPD_HEIGHT);
  writeLe16(header + 26, 1);
  writeLe16(header + 28, 1);
  writeLe32(header + 34, kImageBytes);
  writeLe32(header + 38, 2835);
  writeLe32(header + 42, 2835);
  writeLe32(header + 46, 2);
  // BMP palette index 0 is black and index 1 is white, matching framebuffer bits.
  header[58] = 0xff;
  header[59] = 0xff;
  header[60] = 0xff;

  bool ok = writeBytes(file, header, sizeof(header));
  const uint8_t padding[3] = {};
  for (int16_t y = EPD_HEIGHT - 1; ok && y >= 0; y--) {
    const uint8_t *row = framebuffer + static_cast<size_t>(y) * kSourceRowBytes;
    ok = writeBytes(file, row, kSourceRowBytes) &&
         writeBytes(file, padding, kBmpRowBytes - kSourceRowBytes);
  }
  file.close();
  return ok ? ScreenshotResult::Saved : ScreenshotResult::WriteFailed;
}

const char *screenshotResultMessage(ScreenshotResult result) {
  switch (result) {
  case ScreenshotResult::NoSdCard:
    return "NO SD CARD";
  case ScreenshotResult::NoFramebuffer:
    return "SCREENSHOT FAILED";
  case ScreenshotResult::WriteFailed:
    return "SCREENSHOT FAILED";
  case ScreenshotResult::Saved:
    return "";
  }
  return "SCREENSHOT FAILED";
}
