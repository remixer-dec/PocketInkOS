#define INKPDF_IMPLEMENTATION
#include "fs/providers/pdf_file_viewer.h"

#include "fs/file_provider.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define PDF_VIEWER_HAS_HEAP_CAPS 1
#else
#define PDF_VIEWER_HAS_HEAP_CAPS 0
#endif

namespace {

static const int16_t kPdfViewerTextY = 30;
static const int16_t kPdfViewerLineH = 10;
static const uint32_t kPdfIndexDrawBudgetUs = 0;

struct PdfDrawContext {
  Adafruit_GFX &gfx;
  uint8_t drawn = 0;
};

File openPdfProviderFile(const char *providerId, const char *path) {
  return openProviderPath(providerId, path);
}

void *allocPdfMemory(size_t bytes) {
#if PDF_VIEWER_HAS_HEAP_CAPS
  void *ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr != nullptr) {
    return ptr;
  }
#endif
  return malloc(bytes);
}

void freePdfMemory(void *ptr) {
#if PDF_VIEWER_HAS_HEAP_CAPS
  heap_caps_free(ptr);
#else
  free(ptr);
#endif
}

uint32_t pdfViewerMicros() { return micros(); }

void drawPdfStatus(Adafruit_GFX &gfx, const char *text) {
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

void ensureInkPdfHooks() {
  static bool installed = false;
  if (installed) {
    return;
  }

  InkPdfHooks hooks;
  hooks.openFile = openPdfProviderFile;
  hooks.alloc = allocPdfMemory;
  hooks.free = freePdfMemory;
  hooks.micros = pdfViewerMicros;
  hooks.normalizeText = true;
  inkPdfSetHooks(hooks);
  installed = true;
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

bool pageNumberLine(const char *line) {
  uint8_t digits = 0;
  uint8_t letters = 0;
  uint8_t other = 0;
  for (const char *p = line; p != nullptr && *p != '\0'; p++) {
    const char c = *p;
    if (c >= '0' && c <= '9') {
      digits++;
    } else if (strchr("ivxlcdmIVXLCDM", c) != nullptr) {
      letters++;
    } else if (c != ' ' && c != '-' && c != '.' && c != '/') {
      other++;
    }
  }
  return other == 0 && (digits > 0 || letters > 0) && strlen(line) <= 10;
}

void drawPdfIndexProgress(Adafruit_GFX &gfx, uint8_t percent) {
  if (percent == 0) {
    return;
  }
  const int16_t width = static_cast<int16_t>((percent * 192U) / 100U);
  gfx.drawLine(4, 196, 4 + width, 196, 1);
}

void drawPdfLine(const InkPdfTextLine &line, void *context) {
  PdfDrawContext *draw = static_cast<PdfDrawContext *>(context);
  if (draw == nullptr || draw->drawn >= kInkPdfTextRows ||
      pageNumberLine(line.text) || line.suppress) {
    return;
  }
  draw->gfx.setCursor(4, kPdfViewerTextY + draw->drawn * kPdfViewerLineH);
  draw->gfx.print(line.text);
  draw->drawn++;
}

} // namespace

FileViewerOpenResult openPdfViewer(const FileViewerRequest &request) {
  ensureInkPdfHooks();
  FileViewerScopedSleepGuard sleep(request.activity);
  const InkPdfResult result =
      inkPdfOpen(request.providerId, request.path, nullptr);
  if (result == InkPdfResult::Io) {
    return FileViewerOpenResult::Failed;
  }
  if (result == InkPdfResult::Format) {
    return FileViewerOpenResult::Unsupported;
  }
  if (result != InkPdfResult::Ok) {
    return FileViewerOpenResult::Failed;
  }
  return FileViewerOpenResult::Opened;
}

void drawPdfViewer(Adafruit_GFX &gfx, const FileViewerRuntime &runtime) {
  ensureInkPdfHooks();
  FileViewerScopedSleepGuard sleep(runtime.activity);
  const InkPdfResult indexResult =
      inkPdfReady(runtime.providerId, runtime.path)
          ? InkPdfResult::Ok
          : inkPdfContinueIndex(runtime.providerId, runtime.path,
                                kPdfIndexDrawBudgetUs);
  if (indexResult == InkPdfResult::Done ||
      pdfViewerLoading(runtime.providerId, runtime.path)) {
    drawPdfStatus(gfx, "LOADING");
    drawPdfIndexProgress(gfx, inkPdfProgress(runtime.providerId, runtime.path));
    return;
  }
  if (indexResult == InkPdfResult::Memory) {
    drawPdfStatus(gfx, "ALLOC FAILED");
    return;
  }
  if (indexResult != InkPdfResult::Ok) {
    drawPdfStatus(gfx, "DECODE FAILED");
    return;
  }
  if (inkPdfScreenCount(runtime.providerId, runtime.path) == 0) {
    drawPdfStatus(gfx, "NO TEXT");
    return;
  }

  InkPdfScreenInfo screenInfo;
  PdfDrawContext draw{gfx};
  const InkPdfResult result =
      inkPdfExtractScreenText(runtime.providerId, runtime.path, runtime.offset,
                              drawPdfLine, &draw, &screenInfo);
  if (result == InkPdfResult::Memory) {
    drawPdfStatus(gfx, "ALLOC FAILED");
    return;
  }
  if (result == InkPdfResult::Io) {
    drawPdfStatus(gfx, "OPEN FAILED");
    return;
  }
  if (result == InkPdfResult::Format) {
    drawPdfStatus(gfx, "NO TEXT");
    return;
  }
  if (result != InkPdfResult::Ok) {
    drawPdfStatus(gfx, "DECODE FAILED");
    return;
  }

  if (screenInfo.firstScreenInStream && screenInfo.streamIndex > 0) {
    gfx.drawLine(59, kPdfViewerTextY - 5, 141, kPdfViewerTextY - 5, 1);
  }
  if (screenInfo.lastScreenInStream && screenInfo.hasNextStream &&
      draw.drawn < kInkPdfTextRows) {
    const int16_t y = kPdfViewerTextY + draw.drawn * kPdfViewerLineH + 4;
    gfx.drawLine(59, y, 141, y, 1);
  }

  if (screenInfo.lineCount == 0) {
    drawPdfStatus(gfx, "NO TEXT");
  }
}

void scrollPdfViewer(FileViewerRuntime &runtime, int8_t lines) {
  const uint32_t count = pdfViewerPageCount(runtime.providerId, runtime.path);
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

uint32_t pdfVisibleBytes(const FileViewerRuntime &) { return 1; }

uint16_t pdfViewerPageCount(const char *providerId, const char *path) {
  ensureInkPdfHooks();
  const uint32_t count = inkPdfScreenCount(providerId, path);
  return count > 65535U ? 65535U : static_cast<uint16_t>(count);
}

bool pdfViewerLoading(const char *providerId, const char *path) {
  ensureInkPdfHooks();
  return inkPdfLoading(providerId, path);
}

bool pdfViewerContinueLoading(const char *providerId, const char *path,
                              uint32_t budgetUs) {
  ensureInkPdfHooks();
  const InkPdfResult result = inkPdfContinueIndex(providerId, path, budgetUs);
  return result == InkPdfResult::Ok || result == InkPdfResult::Memory ||
         result == InkPdfResult::Io || result == InkPdfResult::Format;
}

uint8_t pdfViewerProgress(const char *providerId, const char *path) {
  ensureInkPdfHooks();
  return inkPdfProgress(providerId, path);
}

void pdfViewerStatus(const FileViewerRuntime &runtime, char *out,
                     size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const uint16_t count = pdfViewerPageCount(runtime.providerId, runtime.path);
  if (count == 0) {
    copyViewerText(out, outSize,
                   pdfViewerLoading(runtime.providerId, runtime.path) ? "..."
                                                                       : "0/0");
    return;
  }
  const uint16_t page =
      runtime.offset < count ? static_cast<uint16_t>(runtime.offset + 1U)
                             : count;
  snprintf(out, outSize, "%u/%u", static_cast<unsigned>(page),
           static_cast<unsigned>(count));
}

const char *const PDF_VIEWER_EXTENSIONS[] = {"pdf"};

const FileViewerExtension PDF_FILE_VIEWER = {
    "pdf",
    "TEXT",
    PDF_VIEWER_EXTENSIONS,
    static_cast<uint8_t>(sizeof(PDF_VIEWER_EXTENSIONS) /
                         sizeof(PDF_VIEWER_EXTENSIONS[0])),
    openPdfViewer,
    drawPdfViewer,
    scrollPdfViewer,
    nullptr,
    nullptr,
    pdfVisibleBytes,
};
