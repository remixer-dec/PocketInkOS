#ifndef PDF_FILE_VIEWER_H
#define PDF_FILE_VIEWER_H

#include "fs/file_viewer_registry.h"
#include "fs/providers/inkreadable/inkpdf.h"

extern const FileViewerExtension PDF_FILE_VIEWER;

FileViewerOpenResult openPdfViewer(const FileViewerRequest &request);
void drawPdfViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime);
void scrollPdfViewer(FileViewerRuntime &runtime, int8_t lines);
uint32_t pdfVisibleBytes(const FileViewerRuntime &runtime);

uint16_t pdfViewerPageCount(const char *providerId, const char *path);
bool pdfViewerLoading(const char *providerId, const char *path);
bool pdfViewerContinueLoading(const char *providerId, const char *path,
                              uint32_t budgetUs);
uint8_t pdfViewerProgress(const char *providerId, const char *path);
void pdfViewerStatus(const FileViewerRuntime &runtime, char *out,
                     size_t outSize);

#endif
