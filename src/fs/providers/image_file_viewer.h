#ifndef IMAGE_FILE_VIEWER_H
#define IMAGE_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"

#include <stddef.h>

extern const FileViewerExtension IMAGE_FILE_VIEWER;

/// Row-major affine transform applied to decoded image pixels.
///
/// x' = a*x + c*y + tx
/// y' = b*x + d*y + ty
struct ImageTransform {
  float a = 1.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
  float tx = 0.0f;
  float ty = 0.0f;
};

bool imageTransformPoint(const ImageTransform &transform, int16_t x,
                         int16_t y, int16_t &outX, int16_t &outY);

void drawImageBuffer(Adafruit_GFX &gfx, const uint8_t *data, size_t size,
                     const char *extension, int16_t imageX, int16_t imageY,
                     int16_t imageW, int16_t imageH, uint8_t dither,
                     bool scaleToFit);

/// Draws an image buffer through an affine transform. The decoder first maps
/// the image into the supplied rectangle, then the matrix maps those pixels to
/// their final screen positions.
bool drawImageBufferTransformed(Adafruit_GFX &gfx, const uint8_t *data,
                                size_t size, const char *extension,
                                int16_t imageX, int16_t imageY, int16_t imageW,
                                int16_t imageH, uint8_t dither,
                                bool scaleToFit,
                                const ImageTransform &transform);

/// Streams an image from a provider path and applies the same transform as the
/// buffer-backed API.
bool drawImagePathTransformed(Adafruit_GFX &gfx, const char *providerId,
                              const char *path, int16_t imageX,
                              int16_t imageY, int16_t imageW, int16_t imageH,
                              uint8_t dither, bool scaleToFit,
                              const ImageTransform &transform);

#endif
