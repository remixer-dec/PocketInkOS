#include "fs/providers/svg_file_viewer.h"
#include "fs/file_provider.h"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define SVG_VIEWER_HAS_HEAP_CAPS 1
#else
#define SVG_VIEWER_HAS_HEAP_CAPS 0
#endif

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define SVG_VIEWER_IS_ESP32 1
#else
#define SVG_VIEWER_IS_ESP32 0
#endif

#if SVG_VIEWER_HAS_HEAP_CAPS && SVG_VIEWER_IS_ESP32
#define SVG_VIEWER_HAS_ESP_HEAP_RUNTIME 1
#else
#define SVG_VIEWER_HAS_ESP_HEAP_RUNTIME 0
#endif

void *svgViewerAllocRaw(size_t bytes) {
#if SVG_VIEWER_HAS_HEAP_CAPS
  void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr != nullptr) {
    return ptr;
  }
#endif
  return malloc(bytes);
}

void svgViewerFreeRaw(void *ptr) {
#if SVG_VIEWER_HAS_HEAP_CAPS
  heap_caps_free(ptr);
#else
  free(ptr);
#endif
}

void *svgViewerReallocRaw(void *ptr, size_t bytes) {
#if SVG_VIEWER_HAS_ESP_HEAP_RUNTIME
  void *next =
      heap_caps_realloc(ptr, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (next != nullptr || bytes == 0) {
    return next;
  }
  return heap_caps_realloc(ptr, bytes, MALLOC_CAP_8BIT);
#else
  return realloc(ptr, bytes);
#endif
}

void *svgViewerIsvgMalloc(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
  if (bytes > static_cast<size_t>(-1) - sizeof(size_t)) {
    return nullptr;
  }
  const size_t total = bytes + sizeof(size_t);
  size_t *header = static_cast<size_t *>(svgViewerAllocRaw(total));
  if (header == nullptr) {
    return nullptr;
  }
  *header = bytes;
  return header + 1;
}

void *svgViewerIsvgRealloc(void *ptr, size_t bytes) {
  if (ptr == nullptr) {
    return svgViewerIsvgMalloc(bytes);
  }
  if (bytes == 0) {
    size_t *oldHeader = static_cast<size_t *>(ptr) - 1;
    svgViewerFreeRaw(oldHeader);
    return nullptr;
  }
  size_t *oldHeader = static_cast<size_t *>(ptr) - 1;
#if SVG_VIEWER_HAS_ESP_HEAP_RUNTIME
  if (bytes > static_cast<size_t>(-1) - sizeof(size_t)) {
    return nullptr;
  }
  const size_t total = bytes + sizeof(size_t);
  size_t *nextHeader = static_cast<size_t *>(svgViewerReallocRaw(oldHeader, total));
  if (nextHeader != nullptr) {
    *nextHeader = bytes;
    return nextHeader + 1;
  }
#endif
  const size_t oldBytes = *oldHeader;
  void *next = svgViewerIsvgMalloc(bytes);
  if (next == nullptr) {
    return nullptr;
  }
  memcpy(next, ptr, oldBytes < bytes ? oldBytes : bytes);
  svgViewerFreeRaw(oldHeader);
  return next;
}

void svgViewerIsvgFree(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  size_t *header = static_cast<size_t *>(ptr) - 1;
  svgViewerFreeRaw(header);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define ISVG_MALLOC svgViewerIsvgMalloc
#define ISVG_REALLOC svgViewerIsvgRealloc
#define ISVG_FREE svgViewerIsvgFree
#define INKSVG_IMPLEMENTATION
#include "fs/providers/inkreadable/inksvg.h"
#pragma GCC diagnostic pop

namespace {

static const int16_t kImageX = 0;
static const int16_t kImageY = 24;
static const int16_t kImageW = 200;
static const int16_t kImageH = 176;
static const int16_t kFullscreenImageX = 0;
static const int16_t kFullscreenImageY = 0;
static const int16_t kFullscreenImageW = 200;
static const int16_t kFullscreenImageH = 200;
static const size_t kRasterBytes =
    ((static_cast<size_t>(kFullscreenImageW) + 7U) / 8U) * kFullscreenImageH;
static const size_t kMaxSvgBytesPsram = 2048U * 1024U;

enum class SvgViewerError : uint8_t {
  None,
  Empty,
  TooLarge,
  AllocFailed,
  ReadShort,
};

size_t maxSvgBytes() {
  return kMaxSvgBytesPsram;
}

char *allocSvgText(size_t bytes) {
  return static_cast<char *>(svgViewerAllocRaw(bytes));
}

void freeSvgText(char *text) {
  svgViewerFreeRaw(text);
}

FileViewerOpenResult openSvgViewer(const FileViewerRequest &request) {
  File file = openProviderPath(request.providerId, request.path);
  if (!file || file.isDirectory()) {
    return FileViewerOpenResult::Failed;
  }
  const size_t size = file.size();
  const size_t limit = maxSvgBytes();
  if (size == 0 || size > limit) {
    return FileViewerOpenResult::Failed;
  }
  return FileViewerOpenResult::Opened;
}

bool readSvg(File &file, char **outText, SvgViewerError *outError) {
  if (outText == nullptr) {
    return false;
  }
  *outText = nullptr;
  if (outError != nullptr) {
    *outError = SvgViewerError::None;
  }

  const size_t size = file.size();
  if (size == 0 || size > maxSvgBytes()) {
    if (outError != nullptr) {
      *outError = size == 0 ? SvgViewerError::Empty : SvgViewerError::TooLarge;
    }
    return false;
  }

  char *text = allocSvgText(size + 1);
  if (text == nullptr) {
    if (outError != nullptr) {
      *outError = SvgViewerError::AllocFailed;
    }
    return false;
  }

  size_t totalRead = 0;
  uint8_t chunk[512];
  while (totalRead < size) {
    const size_t remaining = size - totalRead;
    const size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    const size_t got = file.read(chunk, want);
    if (got == 0) {
      break;
    }
    memcpy(text + totalRead, chunk, got);
    totalRead += got;
  }
  if (totalRead != size) {
    if (outError != nullptr) {
      *outError = SvgViewerError::ReadShort;
    }
    freeSvgText(text);
    return false;
  }
  text[size] = '\0';
  *outText = text;
  return true;
}

void drawRaster(Adafruit_GFX &gfx, const uint8_t *raster, int16_t imageX,
                int16_t imageY, int16_t imageW, int16_t imageH) {
  const int16_t stride = (imageW + 7) / 8;
  for (int16_t y = 0; y < imageH; y++) {
    const uint8_t *row = raster + static_cast<size_t>(y) * stride;
    for (int16_t x = 0; x < imageW; x++) {
      if ((row[x / 8] & (0x80 >> (x & 7))) != 0) {
        gfx.drawPixel(imageX + x, imageY + y, 1);
      }
    }
  }
}

void drawSvgViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  File file = openProviderPath(runtime.providerId, runtime.path);
  if (!file || file.isDirectory()) {
    gfx.setCursor(58, 92);
    gfx.print("OPEN FAILED");
    return;
  }
  const size_t fileSize = file.size();
  if (fileSize > maxSvgBytes()) {
    gfx.setCursor(72, 92);
    gfx.print("TOO LARGE");
    return;
  }

  char *svg = nullptr;
  SvgViewerError readError = SvgViewerError::None;
  if (!readSvg(file, &svg, &readError)) {
    gfx.setCursor(readError == SvgViewerError::AllocFailed ? 56 : 58, 92);
    gfx.print(readError == SvgViewerError::AllocFailed ? "ALLOC FAILED"
                                                       : "READ FAILED");
    return;
  }

  static uint8_t raster[kRasterBytes];
  memset(raster, 0, sizeof(raster));
  const int16_t imageX = runtime.fullscreen ? kFullscreenImageX : kImageX;
  const int16_t imageY = runtime.fullscreen ? kFullscreenImageY : kImageY;
  const int16_t imageW = runtime.fullscreen ? kFullscreenImageW : kImageW;
  const int16_t imageH = runtime.fullscreen ? kFullscreenImageH : kImageH;

  isvg_tape tape;
  isvg_init(&tape);
  const bool parsed = isvg_parse(&tape, svg);
  freeSvgText(svg);

  if (!parsed) {
    isvg_free(&tape);
    gfx.setCursor(58, 92);
    gfx.print("PARSE FAILED");
    return;
  }

  isvg_rasterize(&tape, raster, imageW, imageH);
  isvg_free(&tape);
  drawRaster(gfx, raster, imageX, imageY, imageW, imageH);
}

void scrollSvgViewer(FileViewerRuntime &, int8_t) {}

uint32_t svgVisibleBytes(const FileViewerRuntime &runtime) {
  return runtime.size;
}

const char *const SVG_VIEWER_EXTENSIONS[] = {"svg"};

} // namespace

const FileViewerExtension SVG_FILE_VIEWER = {
    "svg",
    "SVG",
    SVG_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(SVG_VIEWER_EXTENSIONS) /
                         sizeof(SVG_VIEWER_EXTENSIONS[0])),
    openSvgViewer,
    drawSvgViewer,
    scrollSvgViewer,
    nullptr,
    nullptr,
    svgVisibleBytes,
};
