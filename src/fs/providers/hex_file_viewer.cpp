#include "fs/providers/hex_file_viewer.h"
#include "fs/file_provider.h"

#include <Arduino.h>
#include <cstdio>

namespace {

static const int16_t kViewerTextY = 30;
static const int16_t kViewerLineH = 10;
static const uint8_t kHexRows = 16;
static const uint8_t kHexBytesPerRow = 8;

uint32_t clampOffset(uint32_t offset, uint32_t size) {
  return offset > size ? size : offset;
}

uint8_t hexNibble(uint8_t value) {
  value &= 0x0f;
  return value < 10 ? static_cast<uint8_t>('0' + value)
                    : static_cast<uint8_t>('A' + value - 10);
}

void byteHex(uint8_t value, char *out) {
  out[0] = static_cast<char>(hexNibble(value >> 4));
  out[1] = static_cast<char>(hexNibble(value));
}

FileViewerOpenResult openHexViewer(const FileViewerRequest &request) {
  File file = openProviderPath(request.providerId, request.path);
  if (!file || file.isDirectory()) {
    return FileViewerOpenResult::Failed;
  }
  return FileViewerOpenResult::Opened;
}

void drawHexViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  if (runtime.size == 0) {
    gfx.setCursor(84, 92);
    gfx.print("EMPTY");
    return;
  }
  if (runtime.offset >= runtime.size) {
    gfx.setCursor(90, 92);
    gfx.print("END");
    return;
  }

  File file = openProviderPath(runtime.providerId, runtime.path);
  if (!file || file.isDirectory() ||
      !file.seek(clampOffset(runtime.offset, runtime.size))) {
    gfx.setCursor(58, 92);
    gfx.print("OPEN FAILED");
    return;
  }

  for (uint8_t row = 0; row < kHexRows; row++) {
    char line[kHexBytesPerRow * 3] = {};
    char address[9] = {};
    uint8_t len = 0;
    const uint32_t rowOffset =
        runtime.offset + static_cast<uint32_t>(row) * kHexBytesPerRow;
    for (uint8_t i = 0; i < kHexBytesPerRow; i++) {
      const int raw = file.read();
      if (raw < 0) {
        break;
      }
      byteHex(static_cast<uint8_t>(raw), line + len);
      len += 2;
      if (i + 1 < kHexBytesPerRow) {
        line[len++] = ' ';
      }
    }
    if (len == 0 && row == 0) {
      gfx.setCursor(84, 92);
      gfx.print("EMPTY");
      return;
    }
    if (len == 0) {
      return;
    }
    line[len] = '\0';
    snprintf(address, sizeof(address), "%04lX",
             static_cast<unsigned long>(rowOffset & 0xffffUL));
    gfx.setCursor(4, kViewerTextY + row * kViewerLineH);
    gfx.print(address);
    gfx.setCursor(40, kViewerTextY + row * kViewerLineH);
    gfx.print(line);
  }
}

void scrollHexViewer(FileViewerRuntime &runtime, int8_t lines) {
  const int32_t delta =
      static_cast<int32_t>(lines) * static_cast<int32_t>(kHexBytesPerRow);
  if (delta < 0 && runtime.offset < static_cast<uint32_t>(-delta)) {
    runtime.offset = 0;
  } else {
    const int64_t next = static_cast<int64_t>(runtime.offset) + delta;
    runtime.offset =
        clampOffset(next < 0 ? 0 : static_cast<uint32_t>(next), runtime.size);
  }
  runtime.offset -= runtime.offset % kHexBytesPerRow;
}

uint32_t hexVisibleBytes(const FileViewerRuntime &) {
  return static_cast<uint32_t>(kHexRows) * kHexBytesPerRow;
}

const char *const HEX_VIEWER_EXTENSIONS[] = {"*"};

} // namespace

const FileViewerExtension HEX_FILE_VIEWER = {
    "hex",
    "HEX",
    HEX_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(HEX_VIEWER_EXTENSIONS) /
                         sizeof(HEX_VIEWER_EXTENSIONS[0])),
    openHexViewer,
    drawHexViewer,
    scrollHexViewer,
    hexVisibleBytes,
};
