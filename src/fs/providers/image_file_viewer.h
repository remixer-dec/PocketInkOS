#ifndef IMAGE_FILE_VIEWER_H
#define IMAGE_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"

#include <stddef.h>

extern const FileViewerExtension IMAGE_FILE_VIEWER;

void drawImageBuffer(Adafruit_GFX &gfx, const uint8_t *data, size_t size,
                     const char *extension, int16_t imageX, int16_t imageY,
                     int16_t imageW, int16_t imageH, uint8_t dither,
                     bool scaleToFit);

#endif
