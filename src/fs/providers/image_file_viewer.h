#ifndef IMAGE_FILE_VIEWER_H
#define IMAGE_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"
#include "fs/providers/image_render_cache.h"

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

#endif
