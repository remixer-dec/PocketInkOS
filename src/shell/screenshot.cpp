#include "shell/screenshot.h"

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
constexpr uint32_t kPixelOffset =
    kBmpHeaderBytes + kDibHeaderBytes + kPaletteBytes;

static_assert(EPD_WIDTH % 8 == 0,
              "The monochrome BMP exporter requires byte-aligned rows");
constexpr uint32_t kSourceRowBytes = EPD_WIDTH / 8U;

void writeLe16(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
}

void writeLe32(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8U);
  out[2] = static_cast<uint8_t>(value >> 16U);
  out[3] = static_cast<uint8_t>(value >> 24U);
}

uint16_t readLe16(const uint8_t *in) {
  return static_cast<uint16_t>(in[0]) |
         (static_cast<uint16_t>(in[1]) << 8U);
}

uint32_t readLe32(const uint8_t *in) {
  return static_cast<uint32_t>(in[0]) |
         (static_cast<uint32_t>(in[1]) << 8U) |
         (static_cast<uint32_t>(in[2]) << 16U) |
         (static_cast<uint32_t>(in[3]) << 24U);
}

bool writeBytes(File &file, const uint8_t *bytes, size_t count) {
  return count == 0 || (bytes != nullptr && file.write(bytes, count) == count);
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

bool writeMonochromeBmp(File &file, const uint8_t *framebuffer,
                        uint16_t width, uint16_t height,
                        size_t sourceRowBytes) {
  if (!file || framebuffer == nullptr || width == 0 || height == 0 ||
      (width & 7U) != 0 || sourceRowBytes < width / 8U) {
    return false;
  }
  const uint32_t packedRowBytes = width / 8U;
  const uint32_t bmpRowBytes = (packedRowBytes + 3U) & ~3U;
  const uint32_t imageBytes = bmpRowBytes * height;
  const uint32_t fileBytes = kPixelOffset + imageBytes;

  uint8_t header[kPixelOffset] = {};
  header[0] = 'B';
  header[1] = 'M';
  writeLe32(header + 2, fileBytes);
  writeLe32(header + 10, kPixelOffset);
  writeLe32(header + 14, kDibHeaderBytes);
  writeLe32(header + 18, width);
  writeLe32(header + 22, height);
  writeLe16(header + 26, 1);
  writeLe16(header + 28, 1);
  writeLe32(header + 34, imageBytes);
  writeLe32(header + 38, 2835);
  writeLe32(header + 42, 2835);
  writeLe32(header + 46, 2);
  // Index 0 is black and index 1 is white.
  header[58] = 0xff;
  header[59] = 0xff;
  header[60] = 0xff;

  bool ok = writeBytes(file, header, sizeof(header));
  const uint8_t padding[3] = {};
  for (int32_t row = height - 1; ok && row >= 0; row--) {
    const uint8_t *source =
        framebuffer + static_cast<size_t>(row) * sourceRowBytes;
    ok = writeBytes(file, source, packedRowBytes) &&
         writeBytes(file, padding, bmpRowBytes - packedRowBytes);
  }
  return ok;
}

bool drawMonochromeBmp(Adafruit_GFX &gfx, File &file, int16_t x, int16_t y) {
  uint8_t header[kPixelOffset] = {};
  if (!file || !file.seek(0) || file.read(header, sizeof(header)) !=
                                     static_cast<int>(sizeof(header)) ||
      header[0] != 'B' || header[1] != 'M' ||
      readLe32(header + 14) < kDibHeaderBytes ||
      readLe16(header + 26) != 1 || readLe16(header + 28) != 1 ||
      readLe32(header + 30) != 0) {
    return false;
  }
  const uint32_t width = readLe32(header + 18);
  const int32_t signedHeight = static_cast<int32_t>(readLe32(header + 22));
  if (width == 0 || width > 4096 || signedHeight == 0 ||
      signedHeight == INT32_MIN) {
    return false;
  }
  const bool topDown = signedHeight < 0;
  const uint32_t height =
      static_cast<uint32_t>(topDown ? -signedHeight : signedHeight);
  if (height > 4096) {
    return false;
  }
  const uint32_t rowBytes = ((width + 31U) / 32U) * 4U;
  const uint32_t pixelOffset = readLe32(header + 10);
  if (!file.seek(pixelOffset)) {
    return false;
  }

  uint8_t packed[512];
  if (rowBytes > sizeof(packed)) {
    return false;
  }
  for (uint32_t storedRow = 0; storedRow < height; storedRow++) {
    if (file.read(packed, rowBytes) != rowBytes) {
      return false;
    }
    const uint32_t drawY = topDown ? storedRow : height - 1U - storedRow;
    for (uint32_t drawX = 0; drawX < width; drawX++) {
      const bool white =
          (packed[drawX >> 3U] & (0x80U >> (drawX & 7U))) != 0;
      if (!white) {
        gfx.drawPixel(x + static_cast<int16_t>(drawX),
                      y + static_cast<int16_t>(drawY), 1);
      }
    }
  }
  return true;
}

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

  const bool ok = writeMonochromeBmp(file, framebuffer, EPD_WIDTH, EPD_HEIGHT,
                                     kSourceRowBytes);
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
