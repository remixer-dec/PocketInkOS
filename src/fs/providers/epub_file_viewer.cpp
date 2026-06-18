#define INKEPUB_IMPLEMENTATION
#include "fs/providers/epub_file_viewer.h"

#include "fs/file_provider.h"
#include "fs/providers/image_file_viewer.h"
#include "fs/providers/inkreadable/inkepub.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define EPUB_VIEWER_HAS_HEAP_CAPS 1
#else
#define EPUB_VIEWER_HAS_HEAP_CAPS 0
#endif

namespace {

static const int16_t kEpubViewerTextY = 30;
static const int16_t kEpubViewerLineH = 10;
static const int16_t kEpubImageX = 0;
static const int16_t kEpubImageY = 24;
static const int16_t kEpubImageW = 200;
static const int16_t kEpubImageH = 176;
static const int16_t kFullscreenEpubImageY = 0;
static const int16_t kFullscreenEpubImageH = 200;
static const uint8_t kEpubColumns = 31;
static const uint8_t kEpubRows = 16;

struct EpubDrawContext {
  Adafruit_GFX &gfx;
  uint8_t drawn = 0;
};

enum class EpubImageCacheState : uint8_t {
  Empty,
  Loading,
  Ready,
  Error,
};

struct EpubImageCache {
  char providerId[12] = {};
  char path[288] = {};
  uint32_t page = 0;
  uint8_t *data = nullptr;
  size_t size = 0;
  InkEpubRasterKind kind = InkEpubRasterKind::Unknown;
  InkEpubResult result = InkEpubResult::Ok;
  EpubImageCacheState state = EpubImageCacheState::Empty;
};

EpubImageCache &imageCache() {
  static EpubImageCache cache;
  return cache;
}

File openEpubProviderFile(const char *providerId, const char *path) {
  return openProviderPath(providerId, path);
}

uint32_t epubViewerMicros() { return micros(); }

void *allocEpubMemory(size_t bytes) {
#if EPUB_VIEWER_HAS_HEAP_CAPS
  void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr != nullptr) {
    return ptr;
  }
#endif
  return malloc(bytes);
}

void freeEpubMemory(void *ptr) {
#if EPUB_VIEWER_HAS_HEAP_CAPS
  heap_caps_free(ptr);
#else
  free(ptr);
#endif
}

void ensureInkEpubHooks() {
  static bool installed = false;
  if (installed) {
    return;
  }

  InkEpubHooks hooks;
  hooks.openFile = openEpubProviderFile;
  hooks.alloc = allocEpubMemory;
  hooks.free = freeEpubMemory;
  hooks.micros = epubViewerMicros;
  inkEpubSetHooks(hooks);
  installed = true;
}

void drawEpubStatus(Adafruit_GFX &gfx, const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor((200 - static_cast<int16_t>(textW)) / 2 - textX,
                24 + (176 - static_cast<int16_t>(textH)) / 2 - textY);
  gfx.print(text);
}

void drawEpubProgress(Adafruit_GFX &gfx, uint8_t percent) {
  if (percent == 0) {
    return;
  }
  const int16_t width = static_cast<int16_t>((percent * 192U) / 100U);
  gfx.drawLine(4, 196, 4 + width, 196, 1);
}

void drawEpubLine(const InkEpubTextLine &line, void *context) {
  EpubDrawContext *draw = static_cast<EpubDrawContext *>(context);
  if (draw == nullptr || draw->drawn >= kEpubRows || line.text == nullptr) {
    return;
  }
  draw->gfx.setCursor(4, kEpubViewerTextY + draw->drawn * kEpubViewerLineH);
  draw->gfx.print(line.text);
  draw->drawn++;
}

const char *statusForResult(InkEpubResult result) {
  switch (result) {
  case InkEpubResult::Io:
    return "OPEN FAILED";
  case InkEpubResult::Memory:
    return "ALLOC FAILED";
  case InkEpubResult::Unsupported:
    return "UNSUPPORTED";
  case InkEpubResult::Format:
    return "BAD EPUB";
  default:
    return "DECODE FAILED";
  }
}

const char *extensionForRasterKind(InkEpubRasterKind kind) {
  switch (kind) {
  case InkEpubRasterKind::Png:
    return "png";
  case InkEpubRasterKind::Jpeg:
    return "jpg";
  case InkEpubRasterKind::Webp:
    return "webp";
  case InkEpubRasterKind::Wbmp:
    return "wbmp";
  default:
    return "";
  }
}

bool copyViewerText(char *dest, size_t destSize, const char *source) {
  const char *text = source != nullptr ? source : "";
  if (destSize == 0) {
    return text[0] == '\0';
  }
  const size_t length = strlen(text);
  const size_t copyLength = length < destSize ? length : destSize - 1;
  if (copyLength > 0) {
    memcpy(dest, text, copyLength);
  }
  dest[copyLength] = '\0';
  return text[copyLength] == '\0';
}

bool sameImageCache(const EpubImageCache &cache, const char *providerId,
                    const char *path, uint32_t page) {
  return cache.providerId[0] != '\0' && providerId != nullptr &&
         path != nullptr && strcmp(cache.providerId, providerId) == 0 &&
         strcmp(cache.path, path) == 0 && cache.page == page;
}

void clearImageCache() {
  EpubImageCache &cache = imageCache();
  if (cache.data != nullptr) {
    inkEpubFreeBuffer(cache.data);
  }
  cache = EpubImageCache();
}

void requestImageCache(const FileViewerRuntime &runtime) {
  EpubImageCache &cache = imageCache();
  if (sameImageCache(cache, runtime.providerId, runtime.path, runtime.offset)) {
    return;
  }
  clearImageCache();
  copyViewerText(cache.providerId, sizeof(cache.providerId), runtime.providerId);
  copyViewerText(cache.path, sizeof(cache.path), runtime.path);
  cache.page = runtime.offset;
  cache.state = EpubImageCacheState::Loading;
}

void drawEpubImagePage(Adafruit_GFX &gfx, const FileViewerRuntime &runtime,
                       const InkEpubPageInfo &pageInfo) {
  if (pageInfo.rasterKind == InkEpubRasterKind::Unknown) {
    drawEpubStatus(gfx, "UNSUPPORTED");
    return;
  }

  requestImageCache(runtime);
  EpubImageCache &cache = imageCache();
  if (cache.state == EpubImageCacheState::Loading) {
    drawEpubStatus(gfx, "LOADING");
    return;
  }
  if (cache.state == EpubImageCacheState::Error || cache.data == nullptr ||
      cache.size == 0) {
    drawEpubStatus(gfx, statusForResult(cache.result));
    return;
  }

  const int16_t imageY = runtime.fullscreen ? kFullscreenEpubImageY : kEpubImageY;
  const int16_t imageH = runtime.fullscreen ? kFullscreenEpubImageH : kEpubImageH;
  drawImageBuffer(gfx, cache.data, cache.size, extensionForRasterKind(cache.kind), kEpubImageX,
                  imageY, kEpubImageW, imageH, runtime.imageDither,
                  runtime.imageScaleToFit);
}

const char *const EPUB_VIEWER_EXTENSIONS[] = {"epub"};

} // namespace

FileViewerOpenResult openEpubViewer(const FileViewerRequest &request) {
  ensureInkEpubHooks();
  FileViewerScopedSleepGuard sleep(request.activity);
  InkEpubBookInfo info;
  const InkEpubResult result =
      inkEpubOpen(request.providerId, request.path, &info);
  if (result == InkEpubResult::Io) {
    return FileViewerOpenResult::Failed;
  }
  if (result == InkEpubResult::Format ||
      result == InkEpubResult::Unsupported) {
    return FileViewerOpenResult::Unsupported;
  }
  if (result != InkEpubResult::Ok || info.spineItems == 0) {
    return FileViewerOpenResult::Failed;
  }
  return FileViewerOpenResult::Opened;
}

void drawEpubViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  ensureInkEpubHooks();
  FileViewerScopedSleepGuard sleep(runtime.activity);

  if (epubViewerLoading(runtime.providerId, runtime.path)) {
    drawEpubStatus(gfx, "LOADING");
    drawEpubProgress(gfx, epubViewerProgress(runtime.providerId, runtime.path));
    return;
  }

  InkEpubPageInfo pageInfo;
  const InkEpubResult pageResult =
      inkEpubPageInfo(runtime.providerId, runtime.path, runtime.offset, &pageInfo);
  if (pageResult != InkEpubResult::Ok) {
    drawEpubStatus(gfx, statusForResult(pageResult));
    return;
  }
  if (pageInfo.kind == InkEpubPageKind::Image) {
    drawEpubImagePage(gfx, runtime, pageInfo);
    return;
  }
  clearImageCache();

  EpubDrawContext draw{gfx};
  InkEpubScreenInfo info;
  const InkEpubResult result = inkEpubExtractScreenText(
      runtime.providerId, runtime.path, runtime.offset, kEpubColumns, kEpubRows,
      drawEpubLine, &draw, &info);
  if (result != InkEpubResult::Ok) {
    drawEpubStatus(gfx, statusForResult(result));
    return;
  }
  if (info.lineCount == 0) {
    drawEpubStatus(gfx, runtime.offset == 0 ? "NO TEXT" : "END");
  }
}

void scrollEpubViewer(FileViewerRuntime &runtime, int8_t lines) {
  const uint16_t count = epubViewerPageCount(runtime.providerId, runtime.path);
  if (count == 0) {
    runtime.offset = 0;
    return;
  }
  if (lines < 0) {
    runtime.offset = runtime.offset > 0 ? runtime.offset - 1 : 0;
  } else if (lines > 0 && runtime.offset + 1 < count) {
    runtime.offset++;
  }
}

uint32_t epubVisibleBytes(const FileViewerRuntime &) { return 1; }

uint16_t epubViewerPageCount(const char *providerId, const char *path) {
  ensureInkEpubHooks();
  return inkEpubPageCount(providerId, path, kEpubColumns, kEpubRows);
}

bool epubViewerLoading(const char *providerId, const char *path) {
  ensureInkEpubHooks();
  return inkEpubLoading(providerId, path);
}

bool epubViewerContinueLoading(const char *providerId, const char *path,
                               uint32_t budgetUs) {
  ensureInkEpubHooks();
  const InkEpubResult result =
      inkEpubContinueIndex(providerId, path, kEpubColumns, kEpubRows, budgetUs);
  return result == InkEpubResult::Ok || result == InkEpubResult::Memory ||
         result == InkEpubResult::Io || result == InkEpubResult::Format;
}

uint8_t epubViewerProgress(const char *providerId, const char *path) {
  ensureInkEpubHooks();
  return inkEpubProgress(providerId, path);
}

bool epubViewerImageLoading(const char *providerId, const char *path,
                            uint32_t page) {
  EpubImageCache &cache = imageCache();
  return sameImageCache(cache, providerId, path, page) &&
         cache.state == EpubImageCacheState::Loading;
}

bool epubViewerContinueImage(const char *providerId, const char *path,
                             uint32_t page) {
  ensureInkEpubHooks();
  EpubImageCache &cache = imageCache();
  if (!sameImageCache(cache, providerId, path, page) ||
      cache.state != EpubImageCacheState::Loading) {
    return false;
  }
  cache.result = inkEpubLoadImagePage(providerId, path, page, &cache.data,
                                      &cache.size, &cache.kind);
  cache.state = cache.result == InkEpubResult::Ok ? EpubImageCacheState::Ready
                                                  : EpubImageCacheState::Error;
  return true;
}

void epubViewerStatus(const FileViewerRuntime &runtime, char *out,
                      size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const uint16_t count = epubViewerPageCount(runtime.providerId, runtime.path);
  if (count == 0) {
    copyViewerText(out, outSize,
                   epubViewerLoading(runtime.providerId, runtime.path) ? "..."
                                                                       : "0/0");
    return;
  }
  const uint16_t page =
      runtime.offset < count ? static_cast<uint16_t>(runtime.offset + 1U)
                             : count;
  snprintf(out, outSize, "%u/%u", static_cast<unsigned>(page),
           static_cast<unsigned>(count));
}

const FileViewerExtension EPUB_FILE_VIEWER = {
    "epub",
    "EPUB",
    EPUB_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(EPUB_VIEWER_EXTENSIONS) /
                         sizeof(EPUB_VIEWER_EXTENSIONS[0])),
    openEpubViewer,
    drawEpubViewer,
    scrollEpubViewer,
    epubVisibleBytes,
};
