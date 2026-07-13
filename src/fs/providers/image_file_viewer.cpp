#include "fs/providers/image_file_viewer.h"
#include "fs/file_provider.h"
#include "sys/psram_allocator.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace {

void *imageViewerMalloc(size_t bytes) {
  return psram::allocateOrDefault(bytes);
}

void imageViewerFree(void *ptr) { psram::release(ptr); }

} // namespace

#define IPNG_MALLOC imageViewerMalloc
#define IPNG_FREE imageViewerFree
#define IJPG_MALLOC imageViewerMalloc
#define IJPG_FREE imageViewerFree
#define IWEBP_MALLOC imageViewerMalloc
#define IWEBP_FREE imageViewerFree
#define IWBMP_MALLOC imageViewerMalloc
#define IWBMP_FREE imageViewerFree
#include "fs/providers/inkreadable/inkjpeg.h"
#include "fs/providers/inkreadable/inkpng.h"
#include "fs/providers/inkreadable/inkwbmp.h"
#include "fs/providers/inkreadable/inkwebp.h"

namespace {

static const int16_t kImageX = 0;
static const int16_t kImageY = 24;
static const int16_t kImageW = 200;
static const int16_t kImageH = 176;
static const int16_t kFullscreenImageX = 0;
static const int16_t kFullscreenImageY = 0;
static const int16_t kFullscreenImageW = 200;
static const int16_t kFullscreenImageH = 200;
static const uint32_t kMaxPngRowBytes = 8192;
static const size_t kMaxWebpBytes = 4UL * 1024UL * 1024UL;
static const uint32_t kMaxWebpVp8lPixels = 2UL * 1024UL * 1024UL;

ImageRenderCachePool imageViewerCache;

enum class RasterKind : uint8_t {
  Png,
  Jpeg,
  Webp,
  Wbmp,
  Unknown,
};

struct RasterFileReader {
  File *file;
};

struct RasterMemoryReader {
  const uint8_t *data = nullptr;
  size_t size = 0;
  size_t offset = 0;
};

struct RasterDrawTarget {
  Adafruit_GFX *gfx;
  const ImageTransform *transform = nullptr;
  ImageRenderCache *cache = nullptr;
};

int readRasterByte(void *user) {
  RasterFileReader *reader = static_cast<RasterFileReader *>(user);
  return reader != nullptr && reader->file != nullptr ? reader->file->read()
                                                      : -1;
}

int readMemoryRasterByte(void *user) {
  RasterMemoryReader *reader = static_cast<RasterMemoryReader *>(user);
  if (reader == nullptr || reader->data == nullptr ||
      reader->offset >= reader->size) {
    return -1;
  }
  return reader->data[reader->offset++];
}

void drawRasterPixel(void *user, int16_t x, int16_t y) {
  RasterDrawTarget *target = static_cast<RasterDrawTarget *>(user);
  if (target != nullptr && target->gfx != nullptr) {
    if (target->cache != nullptr) {
      target->cache->setBlackPixel(x, y);
      return;
    }
    if (target->transform == nullptr) {
      target->gfx->drawPixel(x, y, 1);
      return;
    }
    int16_t transformedX = 0;
    int16_t transformedY = 0;
    if (imageTransformPoint(*target->transform, x, y, transformedX,
                            transformedY)) {
      target->gfx->drawPixel(transformedX, transformedY, 1);
    }
  }
}

char lowerAscii(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a')
                                      : value;
}

bool equalsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    if (lowerAscii(*left++) != lowerAscii(*right++)) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

void drawCenteredStatus(Adafruit_GFX &gfx, const char *text, int16_t x,
                        int16_t y, int16_t w, int16_t h) {
  if (text == nullptr || text[0] == '\0' || w <= 0 || h <= 0) {
    return;
  }
  int16_t textX = 0;
  int16_t textY = 0;
  uint16_t textW = 0;
  uint16_t textH = 0;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  gfx.setCursor(x + (w - static_cast<int16_t>(textW)) / 2 - textX,
                y + (h - static_cast<int16_t>(textH)) / 2 - textY);
  gfx.print(text);
}

const char *extensionForPath(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return "";
  }
  const char *name = strrchr(path, '/');
  name = name != nullptr ? name + 1 : path;
  const char *dot = strrchr(name, '.');
  if (dot == nullptr || dot == name || dot[1] == '\0') {
    return "";
  }
  return dot + 1;
}

RasterKind rasterKindForExtension(const char *extension) {
  if (equalsIgnoreCase(extension, "jpg") ||
      equalsIgnoreCase(extension, "jpeg") ||
      equalsIgnoreCase(extension, "jfif")) {
    return RasterKind::Jpeg;
  }
  if (equalsIgnoreCase(extension, "webp")) {
    return RasterKind::Webp;
  }
  if (equalsIgnoreCase(extension, "wbmp")) {
    return RasterKind::Wbmp;
  }
  return equalsIgnoreCase(extension, "png") ? RasterKind::Png
                                             : RasterKind::Unknown;
}

uint64_t mixCacheKey(uint64_t key, uint64_t value) {
  key ^= value + 0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
  return key;
}

uint64_t textCacheKey(uint64_t key, const char *text) {
  if (text == nullptr) {
    return mixCacheKey(key, 0);
  }
  while (*text != '\0') {
    key = mixCacheKey(key, static_cast<uint8_t>(*text++));
  }
  return mixCacheKey(key, 0xffU);
}

uint64_t pathSourceKey(const char *providerId, const char *path,
                       size_t fileSize) {
  uint64_t key = textCacheKey(0x70617468ULL, providerId);
  key = textCacheKey(key, path);
  return mixCacheKey(key, fileSize);
}

uint64_t bufferSourceKey(const uint8_t *data, size_t size) {
  uint64_t key = mixCacheKey(0x62756666ULL,
                             reinterpret_cast<uintptr_t>(data));
  key = mixCacheKey(key, size);
  // Encoded buffers are expected to remain immutable while their cache is in
  // use. A few spread samples cheaply guard against accidental pointer reuse.
  const size_t samples = size < 16U ? size : 16U;
  for (size_t i = 0; i < samples; i++) {
    const size_t offset = samples == 1U ? 0U : (i * (size - 1U)) / (samples - 1U);
    key = mixCacheKey(key, data[offset]);
  }
  return key;
}

uint64_t renderCacheKey(uint64_t source, RasterKind kind, int16_t width,
                        int16_t height, uint8_t dither, bool scaleToFit) {
  uint64_t key = mixCacheKey(source, static_cast<uint8_t>(kind));
  key = mixCacheKey(key, static_cast<uint16_t>(width));
  key = mixCacheKey(key, static_cast<uint16_t>(height));
  key = mixCacheKey(key, dither);
  return mixCacheKey(key, scaleToFit ? 1U : 0U);
}

enum class RasterCacheState : uint8_t {
  Direct,
  Building,
  Hit,
};

RasterCacheState prepareRasterCache(Adafruit_GFX &gfx,
                                    RasterDrawTarget &target,
                                    ImageRenderCachePool *cache, uint64_t key,
                                    int16_t imageX, int16_t imageY,
                                    int16_t imageW, int16_t imageH,
                                    const ImageTransform &transform) {
  if (cache == nullptr) {
    return RasterCacheState::Direct;
  }
  const uint16_t width = static_cast<uint16_t>(imageW);
  const uint16_t height = static_cast<uint16_t>(imageH);
  ImageRenderCache *entry = cache->find(width, height, key);
  if (entry != nullptr) {
    if (entry->draw(gfx, imageX, imageY, transform)) {
      return RasterCacheState::Hit;
    }
    entry->clear();
  }
  entry = cache->acquire();
  if (entry == nullptr || !entry->begin(width, height, key)) {
    return RasterCacheState::Direct;
  }
  target.cache = entry;
  return RasterCacheState::Building;
}

bool finishRasterCache(RasterCacheState state, bool decoded,
                       ImageRenderCache *cache, Adafruit_GFX &gfx,
                       int16_t imageX, int16_t imageY,
                       const ImageTransform &transform) {
  if (state != RasterCacheState::Building) {
    return decoded;
  }
  if (!decoded || cache == nullptr || !cache->finish() ||
      !cache->draw(gfx, imageX, imageY, transform)) {
    if (cache != nullptr) {
      cache->clear();
    }
    return false;
  }
  return true;
}

bool validTransform(const ImageTransform &transform) {
  return std::isfinite(transform.a) && std::isfinite(transform.b) &&
         std::isfinite(transform.c) && std::isfinite(transform.d) &&
         std::isfinite(transform.tx) && std::isfinite(transform.ty);
}

RasterKind rasterKindForFile(File &file, const char *extension) {
  uint8_t header[12] = {};
  const size_t read = file.read(header, sizeof(header));
  file.seek(0);
  static const uint8_t pngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a,
                                    '\n'};
  if (read >= sizeof(pngSig) && memcmp(header, pngSig, sizeof(pngSig)) == 0) {
    return RasterKind::Png;
  }
  if (read >= 2 && header[0] == 0xff && header[1] == 0xd8) {
    return RasterKind::Jpeg;
  }
  if (read >= 12 && memcmp(header, "RIFF", 4) == 0 &&
      memcmp(header + 8, "WEBP", 4) == 0) {
    return RasterKind::Webp;
  }
  return rasterKindForExtension(extension);
}

uint8_t *readWholeFile(File &file, size_t maxBytes, size_t &outSize) {
  outSize = file.size();
  if (outSize == 0 || outSize > maxBytes || !file.seek(0)) {
    return nullptr;
  }
  uint8_t *buffer = static_cast<uint8_t *>(imageViewerMalloc(outSize));
  if (buffer == nullptr) {
    return nullptr;
  }
  const size_t got = file.read(buffer, outSize);
  if (got != outSize) {
    imageViewerFree(buffer);
    return nullptr;
  }
  return buffer;
}

uint32_t pngRowBytes(const ipng_info &info) {
  static const uint8_t channels[7] = {1, 0, 3, 1, 2, 0, 4};
  const uint8_t c = info.color_type < 7 ? channels[info.color_type] : 0;
  uint32_t bytes = 0;
  return ipng_row_bytes_checked(info.width, c, info.bit_depth, &bytes) ? bytes
                                                                       : 0;
}

FileViewerOpenResult openRasterPng(File &file) {
  RasterFileReader source = {&file};
  ipng_reader reader = {&source, readRasterByte};
  ipng_info info;
  const ipng_result result = ipng_read_info(&reader, &info);
  const uint32_t rowBytes = pngRowBytes(info);
  if (result != IPNG_OK || rowBytes == 0 || rowBytes > kMaxPngRowBytes) {
    return FileViewerOpenResult::Failed;
  }
  return FileViewerOpenResult::Opened;
}

FileViewerOpenResult openRasterJpeg(File &file) {
  RasterFileReader source = {&file};
  ijpg_reader reader = {&source, readRasterByte};
  ijpg_info info;
  const ijpg_result result = ijpg_read_info(&reader, &info);
  return result == IJPG_OK ? FileViewerOpenResult::Opened
                           : FileViewerOpenResult::Failed;
}

FileViewerOpenResult openRasterWebp(File &file) {
  size_t size = 0;
  uint8_t *data = readWholeFile(file, kMaxWebpBytes, size);
  if (data == nullptr) {
    return FileViewerOpenResult::Failed;
  }
  iwebp_info info;
  const iwebp_result result = iwebp_read_info(data, size, &info);
  imageViewerFree(data);
  return result == IWEBP_OK ? FileViewerOpenResult::Opened
                            : (result == IWEBP_ERR_UNSUPPORTED_ANIMATION ||
                                       result == IWEBP_ERR_UNSUPPORTED_CODEC
                                   ? FileViewerOpenResult::Unsupported
                                   : FileViewerOpenResult::Failed);
}

FileViewerOpenResult openRasterWbmp(File &file);

FileViewerOpenResult openImageViewer(const FileViewerRequest &request) {
  File file = openProviderPath(request.providerId, request.path);
  if (!file || file.isDirectory()) {
    return FileViewerOpenResult::Failed;
  }

  FileViewerScopedSleepGuard sleep(request.activity);
  const RasterKind kind = rasterKindForFile(file, request.extension);
  if (kind == RasterKind::Unknown) {
    return FileViewerOpenResult::Unsupported;
  }
  if (kind == RasterKind::Jpeg) {
    return openRasterJpeg(file);
  }
  if (kind == RasterKind::Webp) {
    return openRasterWebp(file);
  }
  if (kind == RasterKind::Wbmp) {
    return openRasterWbmp(file);
  }
  return openRasterPng(file);
}

bool drawRasterPng(Adafruit_GFX &gfx, File &file, RasterFileReader &source,
                   RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                   int16_t imageW, int16_t imageH,
                   const FileViewerRuntime &runtime) {
  ipng_reader infoReader = {&source, readRasterByte};
  ipng_info info;
  const ipng_result infoResult = ipng_read_info(&infoReader, &info);
  if (infoResult != IPNG_OK) {
    drawCenteredStatus(gfx, infoResult == IPNG_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                               : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  const uint32_t rowBytes = pngRowBytes(info);
  if (rowBytes == 0 || rowBytes > kMaxPngRowBytes) {
    drawCenteredStatus(gfx, "TOO LARGE", imageX, imageY, imageW, imageH);
    return false;
  }
  if (!file.seek(0)) {
    drawCenteredStatus(gfx, "OPEN FAILED", imageX, imageY, imageW, imageH);
    return false;
  }

  ipng_reader reader = {&source, readRasterByte};
  ipng_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.dither = runtime.imageDither;
  render.scale = runtime.imageScaleToFit ? IPNG_SCALE_FIT : IPNG_SCALE_NONE;

  const ipng_result result = ipng_decode(&reader, &render, &info);
  if (result == IPNG_OK) {
    return true;
  }

  if (result == IPNG_ERR_MEMORY) {
    drawCenteredStatus(gfx, "ALLOC FAILED", imageX, imageY, imageW, imageH);
  } else if (result == IPNG_ERR_UNSUPPORTED) {
    drawCenteredStatus(gfx, "UNSUPPORTED", imageX, imageY, imageW, imageH);
  } else {
    drawCenteredStatus(gfx, "DECODE FAILED", imageX, imageY, imageW, imageH);
  }
  return false;
}

FileViewerOpenResult openRasterWbmp(File &file) {
  RasterFileReader source = {&file};
  iwbmp_reader reader = {&source, readRasterByte};
  iwbmp_info info;
  const iwbmp_result result = iwbmp_read_info(&reader, &info);
  return result == IWBMP_OK ? FileViewerOpenResult::Opened
                            : (result == IWBMP_ERR_UNSUPPORTED
                                   ? FileViewerOpenResult::Unsupported
                                   : FileViewerOpenResult::Failed);
}

bool drawRasterJpeg(Adafruit_GFX &gfx, File &file, RasterFileReader &source,
                    RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                    int16_t imageW, int16_t imageH,
                    const FileViewerRuntime &runtime) {
  ijpg_reader infoReader = {&source, readRasterByte};
  ijpg_info info;
  const ijpg_result infoResult = ijpg_read_info(&infoReader, &info);
  if (infoResult != IJPG_OK) {
    drawCenteredStatus(gfx, infoResult == IJPG_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                               : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  if (!file.seek(0)) {
    drawCenteredStatus(gfx, "OPEN FAILED", imageX, imageY, imageW, imageH);
    return false;
  }

  ijpg_reader reader = {&source, readRasterByte};
  ijpg_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.dither = runtime.imageDither;
  render.scale = runtime.imageScaleToFit ? IJPG_SCALE_FIT : IJPG_SCALE_NONE;

  const ijpg_result result = ijpg_decode(&reader, &render, &info);
  if (result == IJPG_OK) {
    return true;
  }

  if (result == IJPG_ERR_MEMORY) {
    drawCenteredStatus(gfx, "ALLOC FAILED", imageX, imageY, imageW, imageH);
  } else if (result == IJPG_ERR_UNSUPPORTED) {
    drawCenteredStatus(gfx, "UNSUPPORTED", imageX, imageY, imageW, imageH);
  } else {
    drawCenteredStatus(gfx, "DECODE FAILED", imageX, imageY, imageW, imageH);
  }
  return false;
}

bool drawRasterWebp(Adafruit_GFX &gfx, File &file, RasterDrawTarget &target,
                    int16_t imageX, int16_t imageY, int16_t imageW,
                    int16_t imageH, const FileViewerRuntime &runtime) {
  size_t size = 0;
  uint8_t *data = readWholeFile(file, kMaxWebpBytes, size);
  if (data == nullptr) {
    drawCenteredStatus(gfx, file.size() > kMaxWebpBytes ? "TOO LARGE"
                                                        : "OPEN FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }

  iwebp_info info;
  const iwebp_result infoResult = iwebp_read_info(data, size, &info);
  if (infoResult != IWEBP_OK) {
    imageViewerFree(data);
    drawCenteredStatus(gfx, infoResult == IWEBP_ERR_UNSUPPORTED_ANIMATION ||
                                    infoResult == IWEBP_ERR_UNSUPPORTED_CODEC
                                ? "UNSUPPORTED"
                                : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }

  iwebp_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.dither = runtime.imageDither;
  render.scale = runtime.imageScaleToFit ? IWEBP_SCALE_FIT : IWEBP_SCALE_NONE;
  render.threshold = 128;
  render.transparent_black = 0;
  render.max_vp8l_pixels = kMaxWebpVp8lPixels;

  const iwebp_result result = iwebp_decode(data, size, &render, &info);
  imageViewerFree(data);
  if (result == IWEBP_OK) {
    return true;
  }

  if (result == IWEBP_ERR_MEMORY) {
    drawCenteredStatus(gfx, "ALLOC FAILED", imageX, imageY, imageW, imageH);
  } else if (result == IWEBP_ERR_UNSUPPORTED_ANIMATION ||
             result == IWEBP_ERR_UNSUPPORTED_CODEC) {
    drawCenteredStatus(gfx, "UNSUPPORTED", imageX, imageY, imageW, imageH);
  } else {
    drawCenteredStatus(gfx, "DECODE FAILED", imageX, imageY, imageW, imageH);
  }
  return false;
}

bool drawRasterWbmp(Adafruit_GFX &gfx, File &file, RasterFileReader &source,
                    RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                    int16_t imageW, int16_t imageH,
                    const FileViewerRuntime &runtime) {
  iwbmp_reader infoReader = {&source, readRasterByte};
  iwbmp_info info;
  const iwbmp_result infoResult = iwbmp_read_info(&infoReader, &info);
  if (infoResult != IWBMP_OK) {
    drawCenteredStatus(gfx,
                       infoResult == IWBMP_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                           : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  if (!file.seek(0)) {
    drawCenteredStatus(gfx, "OPEN FAILED", imageX, imageY, imageW, imageH);
    return false;
  }

  iwbmp_reader reader = {&source, readRasterByte};
  iwbmp_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.scale = runtime.imageScaleToFit ? IWBMP_SCALE_FIT : IWBMP_SCALE_NONE;

  const iwbmp_result result = iwbmp_decode(&reader, &render, &info);
  if (result == IWBMP_OK) {
    return true;
  }

  if (result == IWBMP_ERR_MEMORY) {
    drawCenteredStatus(gfx, "ALLOC FAILED", imageX, imageY, imageW, imageH);
  } else if (result == IWBMP_ERR_UNSUPPORTED) {
    drawCenteredStatus(gfx, "UNSUPPORTED", imageX, imageY, imageW, imageH);
  } else {
    drawCenteredStatus(gfx, "DECODE FAILED", imageX, imageY, imageW, imageH);
  }
  return false;
}

bool drawMemoryPng(Adafruit_GFX &gfx, RasterMemoryReader &source,
                   RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                   int16_t imageW, int16_t imageH, uint8_t dither,
                   bool scaleToFit) {
  ipng_reader infoReader = {&source, readMemoryRasterByte};
  ipng_info info;
  const ipng_result infoResult = ipng_read_info(&infoReader, &info);
  if (infoResult != IPNG_OK) {
    drawCenteredStatus(gfx, infoResult == IPNG_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                               : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  const uint32_t rowBytes = pngRowBytes(info);
  if (rowBytes == 0 || rowBytes > kMaxPngRowBytes) {
    drawCenteredStatus(gfx, "TOO LARGE", imageX, imageY, imageW, imageH);
    return false;
  }
  source.offset = 0;
  ipng_reader reader = {&source, readMemoryRasterByte};
  ipng_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.dither = dither;
  render.scale = scaleToFit ? IPNG_SCALE_FIT : IPNG_SCALE_NONE;
  const ipng_result result = ipng_decode(&reader, &render, &info);
  if (result == IPNG_OK) {
    return true;
  }
  drawCenteredStatus(gfx,
                     result == IPNG_ERR_MEMORY
                         ? "ALLOC FAILED"
                         : (result == IPNG_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                           : "DECODE FAILED"),
                     imageX, imageY, imageW, imageH);
  return false;
}

bool drawMemoryJpeg(Adafruit_GFX &gfx, RasterMemoryReader &source,
                    RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                    int16_t imageW, int16_t imageH, uint8_t dither,
                    bool scaleToFit) {
  ijpg_reader infoReader = {&source, readMemoryRasterByte};
  ijpg_info info;
  const ijpg_result infoResult = ijpg_read_info(&infoReader, &info);
  if (infoResult != IJPG_OK) {
    drawCenteredStatus(gfx, infoResult == IJPG_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                               : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  source.offset = 0;
  ijpg_reader reader = {&source, readMemoryRasterByte};
  ijpg_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.dither = dither;
  render.scale = scaleToFit ? IJPG_SCALE_FIT : IJPG_SCALE_NONE;
  const ijpg_result result = ijpg_decode(&reader, &render, &info);
  if (result == IJPG_OK) {
    return true;
  }
  drawCenteredStatus(gfx,
                     result == IJPG_ERR_MEMORY
                         ? "ALLOC FAILED"
                         : (result == IJPG_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                           : "DECODE FAILED"),
                     imageX, imageY, imageW, imageH);
  return false;
}

bool drawMemoryWebp(Adafruit_GFX &gfx, const uint8_t *data, size_t size,
                    RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                    int16_t imageW, int16_t imageH, uint8_t dither,
                    bool scaleToFit) {
  iwebp_info info;
  const iwebp_result infoResult = iwebp_read_info(data, size, &info);
  if (infoResult != IWEBP_OK) {
    drawCenteredStatus(gfx, infoResult == IWEBP_ERR_UNSUPPORTED_ANIMATION ||
                                    infoResult == IWEBP_ERR_UNSUPPORTED_CODEC
                                ? "UNSUPPORTED"
                                : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  iwebp_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.dither = dither;
  render.scale = scaleToFit ? IWEBP_SCALE_FIT : IWEBP_SCALE_NONE;
  render.threshold = 128;
  render.transparent_black = 0;
  render.max_vp8l_pixels = kMaxWebpVp8lPixels;
  const iwebp_result result = iwebp_decode(data, size, &render, &info);
  if (result == IWEBP_OK) {
    return true;
  }
  drawCenteredStatus(gfx,
                     result == IWEBP_ERR_MEMORY
                         ? "ALLOC FAILED"
                         : (result == IWEBP_ERR_UNSUPPORTED_ANIMATION ||
                                    result == IWEBP_ERR_UNSUPPORTED_CODEC
                                ? "UNSUPPORTED"
                                : "DECODE FAILED"),
                     imageX, imageY, imageW, imageH);
  return false;
}

bool drawMemoryWbmp(Adafruit_GFX &gfx, RasterMemoryReader &source,
                    RasterDrawTarget &target, int16_t imageX, int16_t imageY,
                    int16_t imageW, int16_t imageH, bool scaleToFit) {
  iwbmp_reader infoReader = {&source, readMemoryRasterByte};
  iwbmp_info info;
  const iwbmp_result infoResult = iwbmp_read_info(&infoReader, &info);
  if (infoResult != IWBMP_OK) {
    drawCenteredStatus(gfx,
                       infoResult == IWBMP_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                           : "DECODE FAILED",
                       imageX, imageY, imageW, imageH);
    return false;
  }
  source.offset = 0;
  iwbmp_reader reader = {&source, readMemoryRasterByte};
  iwbmp_render render = {};
  render.user = &target;
  render.pixel = drawRasterPixel;
  render.x = target.cache != nullptr ? 0 : imageX;
  render.y = target.cache != nullptr ? 0 : imageY;
  render.width = imageW;
  render.height = imageH;
  render.scale = scaleToFit ? IWBMP_SCALE_FIT : IWBMP_SCALE_NONE;
  const iwbmp_result result = iwbmp_decode(&reader, &render, &info);
  if (result == IWBMP_OK) {
    return true;
  }
  drawCenteredStatus(gfx,
                     result == IWBMP_ERR_MEMORY
                         ? "ALLOC FAILED"
                         : (result == IWBMP_ERR_UNSUPPORTED ? "UNSUPPORTED"
                                                            : "DECODE FAILED"),
                     imageX, imageY, imageW, imageH);
  return false;
}

void drawImageViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  File file = openProviderPath(runtime.providerId, runtime.path);
  const int16_t imageX = runtime.fullscreen ? kFullscreenImageX : kImageX;
  const int16_t imageY = runtime.fullscreen ? kFullscreenImageY : kImageY;
  const int16_t imageW = runtime.fullscreen ? kFullscreenImageW : kImageW;
  const int16_t imageH = runtime.fullscreen ? kFullscreenImageH : kImageH;

  if (!file || file.isDirectory()) {
    drawCenteredStatus(gfx, "OPEN FAILED", imageX, imageY, imageW, imageH);
    return;
  }

  FileViewerScopedSleepGuard sleep(runtime.activity);
  RasterFileReader source = {&file};
  RasterDrawTarget target = {&gfx};
  const RasterKind kind = rasterKindForFile(file, extensionForPath(runtime.path));
  if (kind == RasterKind::Unknown) {
    drawCenteredStatus(gfx, "UNSUPPORTED", imageX, imageY, imageW, imageH);
    return;
  }
  const ImageTransform transform;
  ImageRenderCachePool *cache =
      runtime.imageCacheEntries != 0U ? &imageViewerCache : nullptr;
  if (cache != nullptr && !cache->setCapacity(runtime.imageCacheEntries)) {
    cache = nullptr;
  }
  const uint64_t key = renderCacheKey(
      pathSourceKey(runtime.providerId, runtime.path, file.size()), kind, imageW,
      imageH, runtime.imageDither, runtime.imageScaleToFit);
  const RasterCacheState cacheState = prepareRasterCache(
      gfx, target, cache, key, imageX, imageY, imageW, imageH, transform);
  if (cacheState == RasterCacheState::Hit) {
    return;
  }
  bool decoded = false;
  if (kind == RasterKind::Jpeg) {
    decoded = drawRasterJpeg(gfx, file, source, target, imageX, imageY, imageW,
                             imageH, runtime);
  } else if (kind == RasterKind::Png) {
    decoded = drawRasterPng(gfx, file, source, target, imageX, imageY, imageW,
                            imageH, runtime);
  } else if (kind == RasterKind::Webp) {
    decoded = drawRasterWebp(gfx, file, target, imageX, imageY, imageW, imageH,
                             runtime);
  } else if (kind == RasterKind::Wbmp) {
    decoded = drawRasterWbmp(gfx, file, source, target, imageX, imageY, imageW,
                             imageH, runtime);
  }
  (void)finishRasterCache(cacheState, decoded, target.cache, gfx, imageX,
                          imageY, transform);
}

void scrollImageViewer(FileViewerRuntime &, int8_t) {}

uint32_t imageVisibleBytes(const FileViewerRuntime &runtime) {
  return runtime.size;
}

void closeImageViewer() { imageViewerCache.clear(); }

const char *const IMAGE_VIEWER_EXTENSIONS[] = {"png", "jpg", "jpeg", "jfif",
                                               "webp", "wbmp"};

} // namespace

bool imageTransformPoint(const ImageTransform &transform, int16_t x,
                         int16_t y, int16_t &outX, int16_t &outY) {
  const float transformedX =
      transform.a * x + transform.c * y + transform.tx;
  const float transformedY =
      transform.b * x + transform.d * y + transform.ty;
  if (!std::isfinite(transformedX) || !std::isfinite(transformedY) ||
      transformedX < INT16_MIN || transformedX > INT16_MAX ||
      transformedY < INT16_MIN || transformedY > INT16_MAX) {
    return false;
  }
  outX = static_cast<int16_t>(lroundf(transformedX));
  outY = static_cast<int16_t>(lroundf(transformedY));
  return true;
}

void drawImageBuffer(Adafruit_GFX &gfx, const uint8_t *data, size_t size,
                     const char *extension, int16_t imageX, int16_t imageY,
                     int16_t imageW, int16_t imageH, uint8_t dither,
                     bool scaleToFit, ImageRenderCachePool *cache) {
  if (data == nullptr || size == 0 || imageW <= 0 || imageH <= 0) {
    drawCenteredStatus(gfx, "OPEN FAILED", imageX, imageY, imageW, imageH);
    return;
  }
  const RasterKind kind = rasterKindForExtension(extension);
  if (kind == RasterKind::Unknown) {
    drawCenteredStatus(gfx, "UNSUPPORTED", imageX, imageY, imageW, imageH);
    return;
  }
  RasterMemoryReader source;
  source.data = data;
  source.size = size;
  RasterDrawTarget target = {&gfx};
  const ImageTransform transform;
  const uint64_t key = renderCacheKey(bufferSourceKey(data, size), kind, imageW,
                                      imageH, dither, scaleToFit);
  const RasterCacheState cacheState = prepareRasterCache(
      gfx, target, cache, key, imageX, imageY, imageW, imageH, transform);
  if (cacheState == RasterCacheState::Hit) {
    return;
  }
  bool decoded = false;
  if (kind == RasterKind::Jpeg) {
    decoded = drawMemoryJpeg(gfx, source, target, imageX, imageY, imageW,
                             imageH, dither, scaleToFit);
  } else if (kind == RasterKind::Png) {
    decoded = drawMemoryPng(gfx, source, target, imageX, imageY, imageW,
                            imageH, dither, scaleToFit);
  } else if (kind == RasterKind::Webp) {
    decoded = drawMemoryWebp(gfx, data, size, target, imageX, imageY, imageW,
                             imageH, dither, scaleToFit);
  } else if (kind == RasterKind::Wbmp) {
    decoded = drawMemoryWbmp(gfx, source, target, imageX, imageY, imageW,
                             imageH, scaleToFit);
  }
  (void)finishRasterCache(cacheState, decoded, target.cache, gfx, imageX,
                          imageY, transform);
}

bool drawImageBufferTransformed(Adafruit_GFX &gfx, const uint8_t *data,
                                size_t size, const char *extension,
                                int16_t imageX, int16_t imageY, int16_t imageW,
                                int16_t imageH, uint8_t dither,
                                bool scaleToFit,
                                const ImageTransform &transform,
                                ImageRenderCachePool *cache) {
  if (data == nullptr || size == 0 || extension == nullptr ||
      extension[0] == '\0' || imageW <= 0 || imageH <= 0 ||
      !validTransform(transform)) {
    return false;
  }
  RasterMemoryReader source;
  source.data = data;
  source.size = size;
  RasterDrawTarget target = {&gfx, &transform};
  const RasterKind kind = rasterKindForExtension(extension);
  if (kind == RasterKind::Unknown) {
    return false;
  }
  const uint64_t key = renderCacheKey(bufferSourceKey(data, size), kind, imageW,
                                      imageH, dither, scaleToFit);
  const RasterCacheState cacheState = prepareRasterCache(
      gfx, target, cache, key, imageX, imageY, imageW, imageH, transform);
  if (cacheState == RasterCacheState::Hit) {
    return true;
  }
  bool decoded = false;
  if (kind == RasterKind::Jpeg) {
    decoded = drawMemoryJpeg(gfx, source, target, imageX, imageY, imageW,
                             imageH, dither, scaleToFit);
  } else if (kind == RasterKind::Png) {
    decoded = drawMemoryPng(gfx, source, target, imageX, imageY, imageW,
                            imageH, dither, scaleToFit);
  } else if (kind == RasterKind::Webp) {
    decoded = drawMemoryWebp(gfx, data, size, target, imageX, imageY, imageW,
                             imageH, dither, scaleToFit);
  } else if (kind == RasterKind::Wbmp) {
    decoded = drawMemoryWbmp(gfx, source, target, imageX, imageY, imageW,
                             imageH, scaleToFit);
  }
  return finishRasterCache(cacheState, decoded, target.cache, gfx, imageX,
                           imageY, transform);
}

bool drawImagePathTransformed(Adafruit_GFX &gfx, const char *providerId,
                              const char *path, int16_t imageX,
                              int16_t imageY, int16_t imageW, int16_t imageH,
                              uint8_t dither, bool scaleToFit,
                              const ImageTransform &transform,
                              ImageRenderCachePool *cache) {
  if (path == nullptr || path[0] == '\0' || imageW <= 0 || imageH <= 0 ||
      !validTransform(transform)) {
    return false;
  }
  File file = openProviderPath(providerId != nullptr ? providerId : "sd", path);
  if (!file || file.isDirectory()) {
    return false;
  }
  RasterFileReader source = {&file};
  RasterDrawTarget target = {&gfx, &transform};
  const RasterKind kind = rasterKindForFile(file, extensionForPath(path));
  if (kind == RasterKind::Unknown) {
    return false;
  }
  FileViewerRuntime runtime;
  runtime.imageDither = dither;
  runtime.imageScaleToFit = scaleToFit;
  const uint64_t key = renderCacheKey(
      pathSourceKey(providerId, path, file.size()), kind, imageW, imageH,
      dither, scaleToFit);
  const RasterCacheState cacheState = prepareRasterCache(
      gfx, target, cache, key, imageX, imageY, imageW, imageH, transform);
  if (cacheState == RasterCacheState::Hit) {
    return true;
  }
  bool decoded = false;
  if (kind == RasterKind::Jpeg) {
    decoded = drawRasterJpeg(gfx, file, source, target, imageX, imageY, imageW,
                             imageH, runtime);
  } else if (kind == RasterKind::Png) {
    decoded = drawRasterPng(gfx, file, source, target, imageX, imageY, imageW,
                            imageH, runtime);
  } else if (kind == RasterKind::Webp) {
    decoded = drawRasterWebp(gfx, file, target, imageX, imageY, imageW, imageH,
                             runtime);
  } else if (kind == RasterKind::Wbmp) {
    decoded = drawRasterWbmp(gfx, file, source, target, imageX, imageY, imageW,
                             imageH, runtime);
  }
  return finishRasterCache(cacheState, decoded, target.cache, gfx, imageX,
                           imageY, transform);
}

const FileViewerExtension IMAGE_FILE_VIEWER = {
    "raster",
    "IMAGE",
    IMAGE_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(IMAGE_VIEWER_EXTENSIONS) /
                         sizeof(IMAGE_VIEWER_EXTENSIONS[0])),
    openImageViewer,
    drawImageViewer,
    scrollImageViewer,
    nullptr,
    nullptr,
    imageVisibleBytes,
    nullptr,
    closeImageViewer,
};
