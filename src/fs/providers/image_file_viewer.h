#ifndef IMAGE_FILE_VIEWER_H
#define IMAGE_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"
#include "fs/image_render_cache.h"

#include <FS.h>
#include <atomic>
#include <stddef.h>

extern const FileViewerExtension IMAGE_FILE_VIEWER;

bool imageTransformPoint(const ImageTransform &transform, int16_t x,
                         int16_t y, int16_t &outX, int16_t &outY);

void drawImageBuffer(Adafruit_GFX &gfx, const uint8_t *data, size_t size,
                     const char *extension, int16_t imageX, int16_t imageY,
                     int16_t imageW, int16_t imageH, uint8_t dither,
                     bool scaleToFit, ImageRenderCachePool *cache = nullptr);

/// Draws an image buffer through an affine transform. The decoder first maps
/// the image into the supplied rectangle, then the matrix maps those pixels to
/// their final screen positions.
bool drawImageBufferTransformed(Adafruit_GFX &gfx, const uint8_t *data,
                                size_t size, const char *extension,
                                int16_t imageX, int16_t imageY, int16_t imageW,
                                int16_t imageH, uint8_t dither,
                                bool scaleToFit,
                                const ImageTransform &transform,
                                ImageRenderCachePool *cache = nullptr);

/// Streams an image from a provider path and applies the same transform as the
/// buffer-backed API.
bool drawImagePathTransformed(Adafruit_GFX &gfx, const char *providerId,
                              const char *path, int16_t imageX,
                              int16_t imageY, int16_t imageW, int16_t imageH,
                              uint8_t dither, bool scaleToFit,
                              const ImageTransform &transform,
                              ImageRenderCachePool *cache = nullptr);

/// Decodes one encoded-image slice from a larger provider file. The encoded
/// bytes are transient; a supplied cache retains only the rendered 1-bit image.
/// Embedded callers may suppress decoder status text in their own UI.
bool drawImagePathSliceTransformed(
    Adafruit_GFX &gfx, const char *providerId, const char *path,
    uint32_t streamOffset, uint32_t byteSize, const char *extension,
    int16_t imageX, int16_t imageY, int16_t imageW, int16_t imageH,
    uint8_t dither, bool scaleToFit, const ImageTransform &transform,
    ImageRenderCachePool *cache = nullptr, bool showErrors = true);

/// Decodes an embedded image into a PSRAM render cache without touching the
/// destination framebuffer. Intended for background workers.
bool cacheImagePathSlice(const char *providerId, const char *path,
                         uint32_t streamOffset, uint32_t byteSize,
                         const char *extension, int16_t imageW,
                         int16_t imageH, uint8_t dither, bool scaleToFit,
                         ImageRenderCachePool *cache);

/// File-owning workers use this form so their handle remains closable even if
/// the decode task must be stopped before the image decoder returns.
bool cacheImageFileSlice(File &file, const char *providerId, const char *path,
                         uint32_t streamOffset, uint32_t byteSize,
                         const char *extension, int16_t imageW,
                         int16_t imageH, uint8_t dither, bool scaleToFit,
                         ImageRenderCachePool *cache,
                         const std::atomic<bool> *cancelRequested = nullptr);

#endif
