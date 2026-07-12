#ifndef SVG_FILE_VIEWER_H
#define SVG_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"
#include "fs/providers/image_file_viewer.h"

#include <stddef.h>

extern const FileViewerExtension SVG_FILE_VIEWER;

/// Rasterizes an SVG file into the requested rectangle and applies an affine
/// transform to the resulting pixels.
bool drawVectorPathTransformed(Adafruit_GFX &gfx, const char *providerId,
                               const char *path, int16_t x, int16_t y,
                               int16_t width, int16_t height,
                               const ImageTransform &transform);

/// Rasterizes SVG text. The caller's string is never modified.
bool drawVectorStringTransformed(Adafruit_GFX &gfx, const char *svg,
                                 size_t size, int16_t x, int16_t y,
                                 int16_t width, int16_t height,
                                 const ImageTransform &transform);

#endif
