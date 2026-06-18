#ifndef EPUB_FILE_VIEWER_H
#define EPUB_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"

extern const FileViewerExtension EPUB_FILE_VIEWER;

FileViewerOpenResult openEpubViewer(const FileViewerRequest &request);
void drawEpubViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime);
void scrollEpubViewer(FileViewerRuntime &runtime, int8_t lines);
uint32_t epubVisibleBytes(const FileViewerRuntime &runtime);
uint16_t epubViewerPageCount(const char *providerId, const char *path);
bool epubViewerLoading(const char *providerId, const char *path);
bool epubViewerContinueLoading(const char *providerId, const char *path,
                               uint32_t budgetUs);
uint8_t epubViewerProgress(const char *providerId, const char *path);
bool epubViewerImageLoading(const char *providerId, const char *path,
                            uint32_t page);
bool epubViewerContinueImage(const char *providerId, const char *path,
                             uint32_t page);
void epubViewerStatus(const FileViewerRuntime &runtime, char *out,
                      size_t outSize);

#endif
