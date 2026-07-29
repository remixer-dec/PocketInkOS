#include "apps/files_app.h"
#include "fs/file_provider.h"
#include "fs/providers/epub_file_viewer.h"
#include "fs/providers/midi_file_viewer.h"
#include "fs/providers/pdf_file_viewer.h"
#include "shell/wallpaper.h"
#include "sys/app_display.h"
#include "sys/services/lockscreen_guard.h"
#include "sys/services/device_controls.h"
#include "sys/psram_allocator.h"
#include "ui/components/audio_player_component.h"
#include "ui/components/list_component.h"
#include "ui/icon_ascii_font.h"

#include <Arduino.h>
#include <FS.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define FILES_APP_HAS_ROM_PRINTF 1
#else
#define FILES_APP_HAS_ROM_PRINTF 0
#endif

#ifndef FILES_APP_TIMING_LOG
#define FILES_APP_TIMING_LOG 1
#endif

namespace {

#if FILES_APP_TIMING_LOG
struct TimingAccumulator {
  uint32_t totalUs = 0;
  uint32_t maxUs = 0;
  uint16_t calls = 0;

  void add(uint32_t elapsedUs) {
    totalUs += elapsedUs;
    if (elapsedUs > maxUs) {
      maxUs = elapsedUs;
    }
    calls++;
  }

  void reset() {
    totalUs = 0;
    maxUs = 0;
    calls = 0;
  }
};

struct FilesTimingStats {
  TimingAccumulator nameShorten;
  TimingAccumulator sort;
  TimingAccumulator cache;
  TimingAccumulator memmove;
  TimingAccumulator size;
  TimingAccumulator osRead;
  TimingAccumulator name;
};

FilesTimingStats filesTiming;

uint32_t elapsedMicros(uint32_t start) {
  return static_cast<uint32_t>(micros() - start);
}

void filesLogPrintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
#if FILES_APP_HAS_ROM_PRINTF
  esp_rom_vprintf(format, args);
#else
  char line[192];
  vsnprintf(line, sizeof(line), format, args);
  Serial.print(line);
#endif
  va_end(args);
}

class ScopedTiming {
public:
  explicit ScopedTiming(TimingAccumulator &target)
      : target(target), startedAt(micros()) {}
  ~ScopedTiming() { target.add(elapsedMicros(startedAt)); }

private:
  TimingAccumulator &target;
  uint32_t startedAt;
};

void logTimingAccumulator(const char *label, const TimingAccumulator &timing) {
  filesLogPrintf("%s=%luus/%uc max=%luus ", label,
                 static_cast<unsigned long>(timing.totalUs),
                 static_cast<unsigned>(timing.calls),
                 static_cast<unsigned long>(timing.maxUs));
}

void finishFilesLogLine() {
  filesLogPrintf("\n");
  Serial.flush();
}
#endif

void filesViewerKeepAwake(void *) { inactivitySleepKeepAwake(); }

void filesViewerSleepGuard(void *, bool active) {
  if (active) {
    lockscreenGuardAcquire();
  } else {
    lockscreenGuardRelease();
  }
}

void *filesAppAlloc(size_t bytes) {
  return psram::allocateOrDefault(bytes);
}

void filesAppFree(void *ptr) { psram::release(ptr); }

constexpr uint8_t kFilesContextFormat = 1;
struct FilesContextSnapshot {
  uint8_t format = kFilesContextFormat;
  char path[FILES_PATH_CAPACITY] = "/";
  char providerId[12] = {};
  uint16_t scrollOffset = 0;
  int16_t selectedIndex = -1;
  uint8_t storageSelected = 0;
};

static const int16_t kHeaderY = 5;
static const int16_t kHeaderLineY = 18;
static const int16_t kListY = 24;
static const int16_t kRowH = 22;
static const int16_t kListBottomY = 199;
static const int16_t kNameX = 28;
static const int16_t kIconX = 2;
static const int16_t kScrollSplitY = 110;
static const int16_t kListSplitX = 148;
static const unsigned long kStatusMs = 2000;
static const unsigned long kPdfProgressRedrawMs = 2000;
static const uint32_t kPdfBackgroundIndexBudgetUs = 180000;
static const uint32_t kEpubBackgroundIndexBudgetUs = 180000;
static const int8_t kViewerScrollLines = 4;
static const int16_t kScrollbarX = 198;
static const int16_t kScrollbarW = 2;
static const int16_t kViewerOptionsX = 20;
static const int16_t kViewerOptionsY = 24;
static const int16_t kViewerOptionsW = 160;
static const int16_t kViewerOptionsH = 152;
static const int16_t kViewerOptionsRowH = 28;

bool equalsIgnoreCase(const char *left, const char *right);

void drawRightText(Adafruit_GFX &gfx, const char *text, int16_t y) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, y, &textX, &textY, &textW, &textH);
  gfx.setCursor(196 - static_cast<int16_t>(textW) - textX, y);
  gfx.print(text);
}

void drawIcon(Adafruit_GFX &gfx, char icon, int16_t x, int16_t y) {
  char text[2] = {icon, '\0'};
  gfx.setFont(&iconASCII12pt7b);
  gfx.setTextSize(1);
  gfx.setCursor(x, y);
  gfx.print(text);
  gfx.setFont();
}

bool viewerIsPdf(const FileViewerExtension *viewer) {
  return viewer != nullptr && viewer->id != nullptr &&
         equalsIgnoreCase(viewer->id, "pdf");
}

bool viewerIsEpub(const FileViewerExtension *viewer) {
  return viewer != nullptr && viewer->id != nullptr &&
         equalsIgnoreCase(viewer->id, "epub");
}

bool viewerSupportsPageJump(const FileViewerExtension *viewer) {
  return viewerIsPdf(viewer) || viewerIsEpub(viewer);
}

uint16_t viewerPageCount(const FileViewerExtension *viewer,
                         const char *providerId, const char *path) {
  if (viewerIsPdf(viewer)) {
    return pdfViewerPageCount(providerId, path);
  }
  if (viewerIsEpub(viewer)) {
    return epubViewerPageCount(providerId, path);
  }
  return 0;
}

void viewerPageStatus(const FileViewerExtension *viewer,
                      const FileViewerRuntime &runtime, char *out,
                      size_t outSize) {
  if (viewerIsPdf(viewer)) {
    pdfViewerStatus(runtime, out, outSize);
  } else if (viewerIsEpub(viewer)) {
    epubViewerStatus(runtime, out, outSize);
  } else if (out != nullptr && outSize > 0) {
    out[0] = '\0';
  }
}

bool copyText(char *dest, size_t destSize, const char *source) {
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
  return length < destSize;
}

void copyDisplayText(char *dest, size_t destSize, const char *source) {
  if (destSize == 0) {
    return;
  }

  size_t out = 0;
  const unsigned char *cursor =
      reinterpret_cast<const unsigned char *>(source != nullptr ? source : "");
  while (*cursor != '\0' && out + 1 < destSize) {
    const unsigned char c = *cursor++;
    if (c < 0x80) {
      dest[out++] = (c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : '_';
      continue;
    }

    dest[out++] = '?';
    while ((*cursor & 0xc0) == 0x80) {
      cursor++;
    }
  }
  dest[out] = '\0';
}

bool joinPath(char *dest, size_t destSize, const char *parent,
              const char *child) {
  if (destSize == 0) {
    return parent == nullptr || parent[0] == '\0';
  }
  if (parent == nullptr || parent[0] == '\0') {
    return copyText(dest, destSize, child);
  }
  if (child == nullptr || child[0] == '\0') {
    return copyText(dest, destSize, parent);
  }
  int written = 0;
  if (parent[0] == '/' && parent[1] == '\0') {
    written = snprintf(dest, destSize, "/%s", child);
  } else {
    written = snprintf(dest, destSize, "%s/%s", parent, child);
  }
  return written >= 0 && static_cast<size_t>(written) < destSize;
}

bool isRootPath(const char *path) {
  return path != nullptr && path[0] == '/' && path[1] == '\0';
}

bool providerIsMounted(const FileProvider *provider) {
  return provider != nullptr && provider->mounted != nullptr &&
         provider->mounted();
}

bool anyProviderMounted() {
  const size_t count = fileProviderCount();
  for (size_t i = 0; i < count; i++) {
    if (providerIsMounted(fileProviderAt(i))) {
      return true;
    }
  }
  return false;
}

char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

bool equalsIgnoreCase(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    if (asciiLower(*left++) != asciiLower(*right++)) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

bool extensionIs(const char *extension, const char *value) {
  return extension != nullptr && equalsIgnoreCase(extension, value);
}

bool pathHasExtension(const char *path, const char *extension) {
  if (path == nullptr || extension == nullptr) {
    return false;
  }
  const char *name = strrchr(path, '/');
  name = name != nullptr ? name + 1 : path;
  const char *dot = strrchr(name, '.');
  return dot != nullptr && dot[1] != '\0' &&
         equalsIgnoreCase(dot + 1, extension);
}

const char *imageDitherName(uint8_t mode) {
  static const char *const names[] = {"THR", "ATK", "S2R", "S2D"};
  return mode < 4 ? names[mode] : names[0];
}

bool isMetadataName(const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return true;
  }
  if (name[0] == '.') {
    return true;
  }
  return equalsIgnoreCase(name, "thumbs.db") ||
         equalsIgnoreCase(name, "desktop.ini") ||
         equalsIgnoreCase(name, "ehthumbs.db") ||
         equalsIgnoreCase(name, "__macosx");
}

char iconForEntry(const char *name, bool directory) {
  if (directory) {
    return 'O';
  }

  if (name == nullptr) {
    return 'I';
  }
  const char *dot = strrchr(name, '.');
  const char *extension = dot != nullptr ? dot + 1 : nullptr;
  if (extensionIs(extension, "mp3") || extensionIs(extension, "wav") ||
      extensionIs(extension, "flac") || extensionIs(extension, "aac") ||
      extensionIs(extension, "ogg") || extensionIs(extension, "m4a") ||
      extensionIs(extension, "opus") || extensionIs(extension, "mid") ||
      extensionIs(extension, "midi") || extensionIs(extension, "kar")) {
    return '$';
  }
  if (extensionIs(extension, "epub") || extensionIs(extension, "mobi") ||
      extensionIs(extension, "azw") || extensionIs(extension, "azw3") ||
      extensionIs(extension, "pdf") || extensionIs(extension, "cbz") ||
      extensionIs(extension, "cbr")) {
    return '1';
  }
  if (extensionIs(extension, "jpg") || extensionIs(extension, "jpeg") ||
      extensionIs(extension, "png") || extensionIs(extension, "gif") ||
      extensionIs(extension, "bmp") || extensionIs(extension, "webp") ||
      extensionIs(extension, "wbmp") ||
      extensionIs(extension, "svg")) {
    return '~';
  }
  if (extensionIs(extension, "bin") || extensionIs(extension, "so") ||
      extensionIs(extension, "pink") || extensionIs(extension, "js")) {
    return '2';
  }
  if (extensionIs(extension, "ini") || extensionIs(extension, "json") ||
      extensionIs(extension, "yaml") || extensionIs(extension, "yml") ||
      extensionIs(extension, "cfg") || extensionIs(extension, "toml")) {
    return 'h';
  }
  if (extensionIs(extension, "txt") || extensionIs(extension, "md")) {
    return 'J';
  }
  return 'I';
}

void formatSize(uint32_t size, char *out, size_t outSize) {
  if (outSize == 0) {
    return;
  }
  if (size == 0) {
    out[0] = '\0';
    return;
  }
  if (size >= 1024UL * 1024UL) {
    snprintf(out, outSize, "%luM",
             static_cast<unsigned long>((size + 1024UL * 1024UL - 1) /
                                        (1024UL * 1024UL)));
    return;
  }
  snprintf(out, outSize, "%luK",
           static_cast<unsigned long>((size + 1023UL) / 1024UL));
}

void formatProviderFree(const FileProvider *provider, char *out,
                        size_t outSize) {
  if (outSize == 0) {
    return;
  }
  if (provider == nullptr || provider->capacity == nullptr) {
    copyText(out, outSize, "MISSING");
    return;
  }
  const FileProviderCapacity capacity = provider->capacity();
  if (!capacity.mounted || capacity.totalGb == 0) {
    copyText(out, outSize, "MISSING");
    return;
  }
  snprintf(out, outSize, "%lu/%luG",
           static_cast<unsigned long>(capacity.freeGb),
           static_cast<unsigned long>(capacity.totalGb));
}

void formatViewerStatus(const FileViewerExtension *viewer,
                        FileViewerOpenResult result, char *out,
                        size_t outSize) {
  if (outSize == 0) {
    return;
  }
  if (result == FileViewerOpenResult::Unsupported || viewer == nullptr ||
      viewer->label == nullptr) {
    copyText(out, outSize, "NOT SUPPORTED");
    return;
  }
  if (result == FileViewerOpenResult::NotImplemented) {
    snprintf(out, outSize, "%s TODO", viewer->label);
    return;
  }
  if (result == FileViewerOpenResult::Failed) {
    snprintf(out, outSize, "%s FAILED", viewer->label);
    return;
  }
  out[0] = '\0';
}

uint32_t boundedFileSize(File &file) {
  const size_t size = file.size();
  if (size > 0xffffffffUL) {
    return 0xffffffffUL;
  }
  return static_cast<uint32_t>(size);
}

void drawVerticalScrollbar(Adafruit_GFX &gfx, uint32_t offset,
                           uint32_t visible, uint32_t total, int16_t y,
                           int16_t h) {
  if (total <= visible || h <= 0) {
    return;
  }

  int16_t thumbH =
      static_cast<int16_t>((static_cast<uint64_t>(visible) * h) / total);
  if (thumbH < 6) {
    thumbH = 6;
  }
  if (thumbH > h) {
    thumbH = h;
  }

  const uint32_t maxOffset = total - visible;
  const int16_t travel = h - thumbH;
  const int16_t thumbY =
      y + static_cast<int16_t>((static_cast<uint64_t>(offset) * travel) /
                               maxOffset);
  gfx.fillRect(kScrollbarX, thumbY, kScrollbarW, thumbH, 1);
}

uint16_t textWidth(Adafruit_GFX &gfx, const char *text) {
  int16_t textX;
  int16_t textY;
  uint16_t textW;
  uint16_t textH;
  gfx.getTextBounds(text, 0, 0, &textX, &textY, &textW, &textH);
  return textW;
}

void fitMiddleEllipsis(Adafruit_GFX &gfx, const char *source, int16_t maxWidth,
                       char *out, size_t outSize) {
#if FILES_APP_TIMING_LOG
  ScopedTiming timing(filesTiming.nameShorten);
#endif
  const char *text = source != nullptr ? source : "";
  if (outSize == 0) {
    return;
  }
  if (maxWidth <= 0) {
    out[0] = '\0';
    return;
  }
  copyText(out, outSize, text);
  if (textWidth(gfx, out) <= maxWidth || outSize < 8) {
    return;
  }

  const size_t sourceLen = strlen(text);
  const char *dot = strrchr(text, '.');
  size_t minSuffix = (dot != nullptr && dot > text) ? strlen(dot) + 1 : 5;
  if (minSuffix > sourceLen) {
    minSuffix = sourceLen;
  }

  size_t suffix = minSuffix;
  while (suffix > 0) {
    if (suffix + 4 < outSize) {
      size_t low = 0;
      size_t high = sourceLen > suffix ? sourceLen - suffix : 0;
      if (high + 3 + suffix >= outSize) {
        high = outSize - suffix - 4;
      }
      size_t best = 0;
      while (low <= high) {
        const size_t mid = low + (high - low) / 2;
        memcpy(out, text, mid);
        memcpy(out + mid, "...", 3);
        memcpy(out + mid + 3, text + sourceLen - suffix, suffix);
        out[mid + 3 + suffix] = '\0';
        if (textWidth(gfx, out) <= maxWidth) {
          best = mid;
          low = mid + 1;
        } else if (mid == 0) {
          break;
        } else {
          high = mid - 1;
        }
      }
      if (best > 0) {
        memcpy(out, text, best);
        memcpy(out + best, "...", 3);
        memcpy(out + best + 3, text + sourceLen - suffix, suffix);
        out[best + 3 + suffix] = '\0';
        return;
      }
    }
    suffix--;
  }

  suffix = minSuffix;
  while (suffix > 0) {
    if (suffix + 4 < outSize) {
      snprintf(out, outSize, "...%s", text + sourceLen - suffix);
      if (textWidth(gfx, out) <= maxWidth) {
        return;
      }
    }
    suffix--;
  }
  copyText(out, outSize, "...");
}

} // namespace

void FilesApp::reset() {
  closeViewer();
  releaseDirectoryCache();
  copyText(currentPath, sizeof(currentPath), "/");
  scrollOffset = 0;
  selectedIndex = NO_SELECTION;
  statusUntil = 0;
  storageSelected = false;
  setStatus("");
  loaded = false;
  directoryLoading = false;
  directoryScanPass = DirectoryScanPass::Folders;
  directoryScanRoot = File();
  folderCount = 0;
  providerId[0] = '\0';
  pendingPinkPath[0] = '\0';
  pendingPinkLaunch = false;
  pendingJsLaunch = false;
  pickerMode = false;
  pickerHasSelection = false;
  pickerWasCancelled = false;
  pickerSelectedPath[0] = '\0';
  pickerFilter[0] = '\0';
  viewerPath[0] = '\0';
  viewerTitle[0] = '\0';
  viewerOptionsOpen = false;
  viewerPageJumpOpen = false;
  viewerPageInput = "";
  hasDirtyRegion = false;
}

void FilesApp::release() {
  closeViewer();
  directoryScanRoot = File();
  directoryLoading = false;
  releaseDirectoryCache();
  // Keep the path/selection for a return from a launched .js/.pink app, but
  // force the directory to be loaded again because its cache was just freed.
  loaded = false;
  viewerPageInput = "";
  pickerMode = false;
  pickerHasSelection = false;
  pickerWasCancelled = false;
}

bool FilesApp::beginPicker(const char *initialPath,
                           const char *extensionFilter) {
  reset();
  if (!copyText(pickerFilter, sizeof(pickerFilter), extensionFilter)) {
    return false;
  }
  pickerMode = true;
  const FileProvider *provider = defaultFileProvider();
  if (provider == nullptr || !providerIsMounted(provider)) {
    pickerMode = false;
    return false;
  }
  copyText(providerId, sizeof(providerId), provider->id);
  openDirectory(initialPath != nullptr && initialPath[0] == '/'
                    ? initialPath
                    : "/");
  return loaded;
}

bool FilesApp::takePickedPath(char *path, size_t pathSize) {
  if (!pickerHasSelection || !copyText(path, pathSize, pickerSelectedPath)) {
    return false;
  }
  pickerHasSelection = false;
  pickerMode = false;
  return true;
}

void FilesApp::cancelPicker() {
  if (pickerMode) {
    pickerMode = false;
    pickerWasCancelled = true;
  }
}

bool FilesApp::pickerActive() const { return pickerMode; }

bool FilesApp::pickerSelectionReady() const { return pickerHasSelection; }

bool FilesApp::pickerCancelled() const { return pickerWasCancelled; }

bool FilesApp::ensureDirectoryCache() {
  if (entries != nullptr && entryNamePool != nullptr &&
      drawTextScratch != nullptr) {
    return true;
  }

  if (entries == nullptr) {
    entries = static_cast<FileEntry *>(
        filesAppAlloc(sizeof(FileEntry) * MAX_CACHED_ENTRIES));
  }
  if (entryNamePool == nullptr) {
    entryNamePool = static_cast<char *>(filesAppAlloc(FILES_NAME_POOL_CAPACITY));
  }
  if (drawTextScratch == nullptr) {
    drawTextScratch = static_cast<char *>(
        psram::allocate(2U * FILES_PATH_CAPACITY));
  }
  if (entries == nullptr || entryNamePool == nullptr ||
      drawTextScratch == nullptr) {
    releaseDirectoryCache();
    return false;
  }
  return true;
}

void FilesApp::releaseDirectoryCache() {
  filesAppFree(entries);
  filesAppFree(entryNamePool);
  psram::release(drawTextScratch);
  entries = nullptr;
  entryNamePool = nullptr;
  drawTextScratch = nullptr;
  entryCount = 0;
  folderCount = 0;
  entryNamePoolUsed = 0;
}

bool FilesApp::hasActiveSession() const {
  return (viewerIsPdf(activeViewer) && pdfViewerLoading(providerId, viewerPath)) ||
         (viewerIsEpub(activeViewer) &&
          (epubViewerLoading(providerId, viewerPath) ||
           epubViewerImageLoading(providerId, viewerPath, viewerOffset)));
}

bool FilesApp::consumeDirtyRegion(int16_t *x, int16_t *y, int16_t *w,
                                  int16_t *h) {
  if (!hasDirtyRegion || x == nullptr || y == nullptr || w == nullptr ||
      h == nullptr) {
    return false;
  }
  *x = dirtyX;
  *y = dirtyY;
  *w = dirtyW;
  *h = dirtyH;
  hasDirtyRegion = false;
  return true;
}

size_t FilesApp::saveContext(uint8_t *buffer, size_t capacity) const {
  if (buffer == nullptr || capacity < sizeof(FilesContextSnapshot)) {
    return 0;
  }
  FilesContextSnapshot snapshot;
  copyText(snapshot.path, sizeof(snapshot.path), currentPath);
  copyText(snapshot.providerId, sizeof(snapshot.providerId), providerId);
  snapshot.scrollOffset = scrollOffset;
  snapshot.selectedIndex = selectedIndex;
  snapshot.storageSelected = storageSelected ? 1 : 0;
  memcpy(buffer, &snapshot, sizeof(snapshot));
  return sizeof(snapshot);
}

void FilesApp::restoreContext(const uint8_t *buffer, size_t length) {
  if (buffer == nullptr || length != sizeof(FilesContextSnapshot)) {
    return;
  }

  FilesContextSnapshot snapshot;
  memcpy(&snapshot, buffer, sizeof(snapshot));
  if (snapshot.format != kFilesContextFormat) {
    return;
  }
  snapshot.path[sizeof(snapshot.path) - 1] = '\0';
  snapshot.providerId[sizeof(snapshot.providerId) - 1] = '\0';
  if (snapshot.path[0] != '/') {
    return;
  }
  storageSelected = snapshot.storageSelected != 0;
  copyText(providerId, sizeof(providerId), snapshot.providerId);
  if (!storageSelected) {
    copyText(currentPath, sizeof(currentPath), "/");
    providerId[0] = '\0';
    loaded = false;
    selectedIndex = NO_SELECTION;
    return;
  }

  copyText(currentPath, sizeof(currentPath), snapshot.path);
  loadDirectory();
  scrollOffset = snapshot.scrollOffset;
  selectedIndex = snapshot.selectedIndex;
  clampView();
}

const char *FilesApp::displayName(const char *path) const {
  if (path == nullptr || path[0] == '\0') {
    return "";
  }
  const char *slash = strrchr(path, '/');
  if (slash == nullptr || slash[1] == '\0') {
    return path;
  }
  return slash + 1;
}

bool FilesApp::pickerAllows(const char *name) const {
  if (!pickerMode || pickerFilter[0] == '\0') {
    return true;
  }
  const char *dot = name != nullptr ? strrchr(name, '.') : nullptr;
  if (dot == nullptr || dot[1] == '\0') {
    return false;
  }
  const char *cursor = pickerFilter;
  while (*cursor != '\0') {
    while (*cursor == ',' || *cursor == ' ') {
      cursor++;
    }
    const char *end = cursor;
    while (*end != '\0' && *end != ',') {
      end++;
    }
    size_t length = static_cast<size_t>(end - cursor);
    while (length > 0 && cursor[length - 1] == ' ') {
      length--;
    }
    if (length == strlen(dot + 1) &&
        strncasecmp(cursor, dot + 1, length) == 0) {
      return true;
    }
    cursor = end;
  }
  return false;
}

void FilesApp::setStatus(const char *text) {
  copyText(statusText, sizeof(statusText), text);
  statusUntil = 0;
}

void FilesApp::setTransientStatus(const char *text) {
  copyText(statusText, sizeof(statusText), text);
  statusUntil = millis() + kStatusMs;
}

bool FilesApp::clearExpiredStatus() {
  if (statusUntil != 0 && static_cast<long>(millis() - statusUntil) >= 0) {
    setStatus(truncatedDirectory ? "LIMITED" : "");
    return true;
  }
  return false;
}

void FilesApp::loadDirectory() {
  loaded = true;
  entryCount = 0;
  folderCount = 0;
  entryNamePoolUsed = 0;
  if (!ensureDirectoryCache()) {
    truncatedDirectory = false;
    directoryLoading = false;
    directoryScanRoot = File();
    setStatus("ALLOC FAILED");
    clampView();
    return;
  }
  entryNamePool[0] = '\0';
  truncatedDirectory = false;
  directoryLoading = false;
  directoryScanPass = DirectoryScanPass::Folders;
  directoryScanRoot = File();

  const FileProvider *provider = fileProviderById(providerId);
  cardMounted = providerIsMounted(provider);
  if (!cardMounted) {
    setStatus("NO SD");
    clampView();
    return;
  }

  if (loadDirectoryFast(provider)) {
    return;
  }

#if FILES_APP_TIMING_LOG
  uint32_t opStartedAt = micros();
#endif
  directoryScanRoot = openProviderPath(provider, currentPath);
#if FILES_APP_TIMING_LOG
  filesLogPrintf("[files] open dir path=%s open=%luus\n", currentPath,
                 static_cast<unsigned long>(elapsedMicros(opStartedAt)));
  Serial.flush();
#endif
  if (!directoryScanRoot || !directoryScanRoot.isDirectory()) {
    directoryScanRoot = File();
    copyText(currentPath, sizeof(currentPath), "/");
#if FILES_APP_TIMING_LOG
    opStartedAt = micros();
#endif
    directoryScanRoot = openProviderPath(provider, currentPath);
#if FILES_APP_TIMING_LOG
    filesLogPrintf("[files] open dir fallback=/ open=%luus\n",
                   static_cast<unsigned long>(elapsedMicros(opStartedAt)));
    Serial.flush();
#endif
  }

  if (!directoryScanRoot || !directoryScanRoot.isDirectory()) {
    setStatus("OPEN FAILED");
    clampView();
    return;
  }

  directoryLoading = true;
  directoryScanPass = DirectoryScanPass::Folders;
  setStatus("LOADING");
  continueDirectoryLoad(8);
  clampView();
}

bool FilesApp::loadDirectoryFast(const FileProvider *provider) {
  if (provider == nullptr || provider->listDirectory == nullptr) {
    return false;
  }

#if FILES_APP_TIMING_LOG
  filesTiming.cache.reset();
  filesTiming.sort.reset();
  filesTiming.memmove.reset();
  const uint32_t startedAt = micros();
#endif

  directoryLoading = false;
  bool ok = listProviderDirectory(provider, currentPath, cacheProviderFolderEntry,
                                  this);
  if (ok && !truncatedDirectory) {
    ok = listProviderDirectory(provider, currentPath, cacheProviderFileEntry,
                               this);
  }
  if (!ok && !truncatedDirectory) {
    entryCount = 0;
    folderCount = 0;
    entryNamePoolUsed = 0;
    if (entryNamePool != nullptr) {
      entryNamePool[0] = '\0';
    }
    truncatedDirectory = false;
    return false;
  }

#if FILES_APP_TIMING_LOG
  Serial.printf("[files] fast list path=%s total=%u folders=%u elapsed=%luus ",
                currentPath, static_cast<unsigned>(entryCount),
                static_cast<unsigned>(folderCount),
                static_cast<unsigned long>(elapsedMicros(startedAt)));
  logTimingAccumulator("sort", filesTiming.sort);
  logTimingAccumulator("memmove", filesTiming.memmove);
  logTimingAccumulator("cache", filesTiming.cache);
  Serial.println("");
#endif

  finishDirectoryLoad();
  return true;
}

void FilesApp::finishDirectoryLoad() {
  directoryLoading = false;
  directoryScanPass = DirectoryScanPass::Folders;
  directoryScanRoot = File();
  if (entryCount == 0) {
    setStatus("empty");
  } else if (truncatedDirectory) {
    setStatus("LIMITED");
  } else {
    setStatus("");
  }
  clampView();
}

bool FilesApp::continueDirectoryLoad(uint8_t budget) {
  if (!directoryLoading) {
    return false;
  }

#if FILES_APP_TIMING_LOG
  filesTiming.osRead.reset();
  filesTiming.name.reset();
  filesTiming.size.reset();
  filesTiming.cache.reset();
  filesTiming.sort.reset();
  filesTiming.memmove.reset();
  const uint32_t chunkStartedAt = micros();
  const uint16_t beforeCount = entryCount;
#endif
  bool changed = false;
  while (budget-- > 0) {
#if FILES_APP_TIMING_LOG
    uint32_t opStartedAt = micros();
#endif
    File file = directoryScanRoot.openNextFile();
#if FILES_APP_TIMING_LOG
    filesTiming.osRead.add(elapsedMicros(opStartedAt));
#endif
    if (!file) {
      if (directoryScanPass == DirectoryScanPass::Folders) {
        const FileProvider *provider = fileProviderById(providerId);
        directoryScanRoot = openProviderPath(provider, currentPath);
        if (!directoryScanRoot || !directoryScanRoot.isDirectory()) {
#if FILES_APP_TIMING_LOG
          filesLogPrintf("[files] scan reopen failed after folders total=%u\n",
                         static_cast<unsigned>(entryCount));
          Serial.flush();
#endif
          finishDirectoryLoad();
          return true;
        }
        directoryScanPass = DirectoryScanPass::Files;
#if FILES_APP_TIMING_LOG
        filesLogPrintf(
            "[files] scan folders done total=%u chunk=%luus entries=%u\n",
            static_cast<unsigned>(entryCount),
            static_cast<unsigned long>(elapsedMicros(chunkStartedAt)),
            static_cast<unsigned>(entryCount - beforeCount));
        Serial.flush();
#endif
        continue;
      }
#if FILES_APP_TIMING_LOG
      filesLogPrintf("[files] scan done total=%u chunk=%luus entries=%u\n",
                     static_cast<unsigned>(entryCount),
                     static_cast<unsigned long>(elapsedMicros(chunkStartedAt)),
                     static_cast<unsigned>(entryCount - beforeCount));
      Serial.flush();
#endif
      finishDirectoryLoad();
      return true;
    }
    const bool isDirectory = file.isDirectory();
#if FILES_APP_TIMING_LOG
    opStartedAt = micros();
#endif
    const char *name = displayName(file.name());
#if FILES_APP_TIMING_LOG
    filesTiming.name.add(elapsedMicros(opStartedAt));
#endif
    const bool wantsDirectory =
        directoryScanPass == DirectoryScanPass::Folders;
    if (!isMetadataName(name) && isDirectory == wantsDirectory &&
        (isDirectory || pickerAllows(name))) {
#if FILES_APP_TIMING_LOG
      opStartedAt = micros();
#endif
      const uint32_t size = static_cast<uint32_t>(file.size());
#if FILES_APP_TIMING_LOG
      filesTiming.size.add(elapsedMicros(opStartedAt));
#endif
      if (!cacheDirectoryEntry(name, isDirectory,
                               size)) {
#if FILES_APP_TIMING_LOG
        filesLogPrintf("[files] scan limited total=%u chunk=%luus entries=%u ",
                       static_cast<unsigned>(entryCount),
                       static_cast<unsigned long>(elapsedMicros(chunkStartedAt)),
                       static_cast<unsigned>(entryCount - beforeCount));
        logTimingAccumulator("os_read", filesTiming.osRead);
        logTimingAccumulator("name", filesTiming.name);
        logTimingAccumulator("size", filesTiming.size);
        logTimingAccumulator("sort", filesTiming.sort);
        logTimingAccumulator("cache", filesTiming.cache);
        finishFilesLogLine();
#endif
        finishDirectoryLoad();
        return true;
      }
      changed = true;
    }
  }
#if FILES_APP_TIMING_LOG
  filesLogPrintf("[files] scan chunk total=%u chunk=%luus entries=%u loading=%u ",
                 static_cast<unsigned>(entryCount),
                 static_cast<unsigned long>(elapsedMicros(chunkStartedAt)),
                 static_cast<unsigned>(entryCount - beforeCount),
                 directoryLoading ? 1 : 0);
  logTimingAccumulator("os_read", filesTiming.osRead);
  logTimingAccumulator("name", filesTiming.name);
  logTimingAccumulator("size", filesTiming.size);
  logTimingAccumulator("sort", filesTiming.sort);
  logTimingAccumulator("memmove", filesTiming.memmove);
  logTimingAccumulator("cache", filesTiming.cache);
  finishFilesLogLine();
#endif
  return changed;
}

bool FilesApp::cacheDirectoryEntry(const char *name, bool directory,
                                   uint32_t size) {
#if FILES_APP_TIMING_LOG
  ScopedTiming cacheTiming(filesTiming.cache);
#endif
  if (entryCount >= MAX_CACHED_ENTRIES) {
    truncatedDirectory = true;
    return false;
  }

  const char *source = name != nullptr ? name : "";
  const size_t sourceLength = strlen(source);
  const size_t storedLength =
      sourceLength < FILES_NAME_CAPACITY ? sourceLength : FILES_NAME_CAPACITY - 1;
  const size_t needed = storedLength + 1;
  if (!ensureDirectoryCache() ||
      needed > static_cast<size_t>(FILES_NAME_POOL_CAPACITY - entryNamePoolUsed)) {
    truncatedDirectory = true;
    return false;
  }

  const uint16_t offset = entryNamePoolUsed;
  if (storedLength > 0) {
    memcpy(entryNamePool + entryNamePoolUsed, source, storedLength);
  }
  entryNamePool[entryNamePoolUsed + storedLength] = '\0';
  entryNamePoolUsed += static_cast<uint16_t>(needed);

  FileEntry entry;
  entry.nameOffset = offset;
  entry.directory = directory;
  entry.size = size;
  entry.nameComplete = sourceLength < FILES_NAME_CAPACITY;
  entry.icon = iconForEntry(entryName(entry), entry.directory);

#if FILES_APP_TIMING_LOG
  uint32_t opStartedAt = micros();
#endif
  const uint16_t insertIndex = sortedInsertIndex(entryName(entry), directory);
#if FILES_APP_TIMING_LOG
  filesTiming.sort.add(elapsedMicros(opStartedAt));
#endif
  if (insertIndex < entryCount) {
#if FILES_APP_TIMING_LOG
    opStartedAt = micros();
#endif
    memmove(entries + insertIndex + 1, entries + insertIndex,
            (entryCount - insertIndex) * sizeof(entries[0]));
#if FILES_APP_TIMING_LOG
    filesTiming.memmove.add(elapsedMicros(opStartedAt));
#endif
  }
  entries[insertIndex] = entry;
  entryCount++;
  if (directory) {
    folderCount++;
  }
  return true;
}

bool FilesApp::cacheProviderFolderEntry(const FileProviderEntry &entry,
                                        void *context) {
  FilesApp *app = static_cast<FilesApp *>(context);
  if (app == nullptr) {
    return false;
  }
  if (isMetadataName(entry.name) || !entry.directory) {
    return true;
  }
  return app->cacheDirectoryEntry(entry.name, entry.directory, entry.size);
}

bool FilesApp::cacheProviderFileEntry(const FileProviderEntry &entry,
                                      void *context) {
  FilesApp *app = static_cast<FilesApp *>(context);
  if (app == nullptr) {
    return false;
  }
  if (isMetadataName(entry.name) || entry.directory) {
    return true;
  }
  if (!app->pickerAllows(entry.name)) {
    return true;
  }
  return app->cacheDirectoryEntry(entry.name, entry.directory, entry.size);
}

int FilesApp::compareEntryNames(const char *left, const char *right) const {
  const unsigned char *l =
      reinterpret_cast<const unsigned char *>(left != nullptr ? left : "");
  const unsigned char *r =
      reinterpret_cast<const unsigned char *>(right != nullptr ? right : "");
  while (*l != '\0' && *r != '\0') {
    const char lc = asciiLower(static_cast<char>(*l));
    const char rc = asciiLower(static_cast<char>(*r));
    if (lc != rc) {
      return lc < rc ? -1 : 1;
    }
    l++;
    r++;
  }
  if (*l == *r) {
    return 0;
  }
  return *l == '\0' ? -1 : 1;
}

uint16_t FilesApp::sortedInsertIndex(const char *name, bool directory) const {
  if (entries == nullptr) {
    return 0;
  }
  uint16_t low = directory ? 0 : folderCount;
  uint16_t high = directory ? folderCount : entryCount;
  while (low < high) {
    const uint16_t mid = low + (high - low) / 2;
    if (compareEntryNames(entryName(entries[mid]), name) <= 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

const char *FilesApp::entryName(const FileEntry &entry) const {
  if (entryNamePool == nullptr || entry.nameOffset >= entryNamePoolUsed) {
    return "";
  }
  return entryNamePool + entry.nameOffset;
}

void FilesApp::openDirectory(const char *path) {
  storageSelected = true;
  if (providerId[0] == '\0') {
    const FileProvider *provider = defaultFileProvider();
    copyText(providerId, sizeof(providerId),
             provider != nullptr ? provider->id : "");
  }
  if (!copyText(currentPath, sizeof(currentPath), path)) {
    setTransientStatus("PATH TOO LONG");
    return;
  }
  scrollOffset = 0;
  selectedIndex = NO_SELECTION;
  hasDirtyRegion = false;
  setStatus("");
  loadDirectory();
}

void FilesApp::goUp() {
  if (isRootPath(currentPath)) {
    return;
  }
  char parent[sizeof(currentPath)];
  copyText(parent, sizeof(parent), currentPath);
  char *slash = strrchr(parent, '/');
  if (slash == nullptr || slash == parent) {
    copyText(parent, sizeof(parent), "/");
  } else {
    *slash = '\0';
  }
  openDirectory(parent);
}

void FilesApp::clampView() {
  const uint16_t maxOffset =
      entryCount > MAX_VISIBLE_ENTRIES ? entryCount - MAX_VISIBLE_ENTRIES : 0;
  if (scrollOffset > maxOffset) {
    scrollOffset = maxOffset;
  }
  if (entryCount == 0 || selectedIndex == NO_SELECTION) {
    selectedIndex = NO_SELECTION;
    return;
  }
  if (selectedIndex >= entryCount) {
    selectedIndex = NO_SELECTION;
    return;
  }
  if (selectedIndex < scrollOffset) {
    selectedIndex = NO_SELECTION;
    return;
  }
  const uint16_t lastVisible = scrollOffset + MAX_VISIBLE_ENTRIES - 1;
  if (selectedIndex > lastVisible) {
    selectedIndex = NO_SELECTION;
  }
}

bool FilesApp::activateIndex(uint16_t index) {
  if (index >= entryCount) {
    return false;
  }
  if (entries == nullptr) {
    return false;
  }
  const FileEntry &entry = entries[index];
  const char *name = entryName(entry);
  if (!entry.nameComplete) {
    setTransientStatus("NAME TOO LONG");
    return true;
  }
  if (entry.directory) {
    char path[sizeof(currentPath)];
    if (!joinPath(path, sizeof(path), currentPath, name)) {
      setTransientStatus("PATH TOO LONG");
      return true;
    }
    openDirectory(path);
    return true;
  }

  char path[sizeof(currentPath)];
  if (!joinPath(path, sizeof(path), currentPath, name)) {
    setTransientStatus("PATH TOO LONG");
    return true;
  }
  if (pickerMode) {
    copyText(pickerSelectedPath, sizeof(pickerSelectedPath), path);
    pickerHasSelection = true;
    return true;
  }
  if (equalsIgnoreCase(providerId, "sd") && pathHasExtension(path, "pink")) {
    return requestPinkLaunch(path);
  }
  if (equalsIgnoreCase(providerId, "sd") && pathHasExtension(path, "js")) {
    return requestJsLaunch(path);
  }

  const FileViewerExtension *viewer = nullptr;
  FileViewerActivity activity = viewerActivity();
  const FileViewerOpenResult result =
      openFileWithViewer(providerId, path, &viewer, &activity);
  if (result == FileViewerOpenResult::Opened) {
    char title[FILES_LABEL_CAPACITY];
    copyDisplayText(title, sizeof(title), name);
    return openViewer(path, title, viewer);
  }

  char status[sizeof(statusText)];
  formatViewerStatus(viewer, result, status, sizeof(status));
  setTransientStatus(status);
  return true;
}

bool FilesApp::activateStorage() {
  if (selectedIndex < 0) {
    return false;
  }
  const FileProvider *provider =
      fileProviderAt(static_cast<size_t>(selectedIndex));
  if (provider == nullptr) {
    return false;
  }
  if (!providerIsMounted(provider)) {
    setTransientStatus("MISSING");
    return true;
  }
  copyText(providerId, sizeof(providerId), provider->id);
  openDirectory("/");
  return true;
}

bool FilesApp::requestPinkLaunch(const char *path) {
  if (!copyText(pendingPinkPath, sizeof(pendingPinkPath), path)) {
    setTransientStatus("PATH TOO LONG");
    return true;
  }
  pendingPinkLaunch = true;
  pendingJsLaunch = false;
  setTransientStatus("LAUNCHING");
  return true;
}

bool FilesApp::requestJsLaunch(const char *path) {
  if (!copyText(pendingPinkPath, sizeof(pendingPinkPath), path)) {
    setTransientStatus("PATH TOO LONG");
    return true;
  }
  pendingPinkLaunch = false;
  pendingJsLaunch = true;
  setTransientStatus("LAUNCHING JS");
  return true;
}

bool FilesApp::consumePinkLaunch(char *path, size_t pathSize) {
  if (!pendingPinkLaunch) {
    return false;
  }
  pendingPinkLaunch = false;
  copyText(path, pathSize, pendingPinkPath);
  pendingPinkPath[0] = '\0';
  return true;
}

bool FilesApp::consumeJsLaunch(char *path, size_t pathSize) {
  if (!pendingJsLaunch) {
    return false;
  }
  pendingJsLaunch = false;
  copyText(path, pathSize, pendingPinkPath);
  pendingPinkPath[0] = '\0';
  return true;
}

bool FilesApp::openViewer(const char *path, const char *title,
                          const FileViewerExtension *viewer) {
  const FileProvider *provider = fileProviderById(providerId);
  File file = openProviderPath(provider, path);
  if (!file || file.isDirectory()) {
    setTransientStatus("OPEN FAILED");
    return true;
  }

  copyText(viewerPath, sizeof(viewerPath), path);
  copyText(viewerTitle, sizeof(viewerTitle), title);
  viewerOffset = 0;
  viewerSize = boundedFileSize(file);
  viewerFullscreen = false;
  viewerOptionsOpen = false;
  pendingWallpaper = PendingWallpaper::None;
  viewerPageJumpOpen = false;
  viewerPreviousAvailable = false;
  viewerNextAvailable = false;
  viewerScaleToFit = true;
  viewerDither = 1;
  viewerPageInput = "";
  nextPdfProgressRedrawMs = 0;

  activeViewer = nullptr;
  if (viewer == nullptr || viewer->draw == nullptr) {
    setTransientStatus("NOT SUPPORTED");
    return true;
  }

  activeViewer = viewer;
  if (activeViewer != nullptr && activeViewer->id != nullptr &&
      equalsIgnoreCase(activeViewer->id, "midi")) {
    viewerSize = midiViewerDurationMs(providerId, viewerPath);
  }
  if (viewerSupportsPageJump(activeViewer)) {
    viewerSize = viewerPageCount(activeViewer, providerId, viewerPath);
  }
  if (activeViewer->togglePlayback != nullptr) {
    viewerPreviousAvailable = adjacentTrackIndex(-1) >= 0;
    viewerNextAvailable = adjacentTrackIndex(1) >= 0;
  }
  return true;
}

int16_t FilesApp::adjacentTrackIndex(int8_t direction) const {
  if (entries == nullptr || entryCount == 0 || direction == 0 ||
      viewerPath[0] == '\0') {
    return -1;
  }
  int32_t current = -1;
  char path[sizeof(currentPath)];
  for (uint16_t i = 0; i < entryCount; i++) {
    if (entries[i].directory || !entries[i].nameComplete ||
        !joinPath(path, sizeof(path), currentPath, entryName(entries[i]))) {
      continue;
    }
    if (strcmp(path, viewerPath) == 0) {
      current = i;
      break;
    }
  }
  if (current < 0) {
    return -1;
  }

  for (int32_t i = current + (direction < 0 ? -1 : 1);
       i >= 0 && i < entryCount; i += direction < 0 ? -1 : 1) {
    const FileEntry &entry = entries[i];
    if (entry.directory || !entry.nameComplete ||
        !joinPath(path, sizeof(path), currentPath, entryName(entry))) {
      continue;
    }
    const FileViewerExtension *viewer = findFileViewerForPath(path);
    if (viewer != nullptr && viewer->togglePlayback != nullptr) {
      return static_cast<int16_t>(i);
    }
  }
  if (direction > 0) {
    // The playlist is circular in the forward direction: after its last
    // playable entry, Next opens the first one (or restarts a single track).
    for (int32_t i = 0; i <= current; i++) {
      const FileEntry &entry = entries[i];
      if (entry.directory || !entry.nameComplete ||
          !joinPath(path, sizeof(path), currentPath, entryName(entry))) {
        continue;
      }
      const FileViewerExtension *viewer = findFileViewerForPath(path);
      if (viewer != nullptr && viewer->togglePlayback != nullptr) {
        return static_cast<int16_t>(i);
      }
    }
  }
  return -1;
}

bool FilesApp::openAdjacentTrack(int8_t direction) {
  const int16_t index = adjacentTrackIndex(direction);
  if (index < 0) {
    return false;
  }
  const char *name = entryName(entries[index]);
  char path[sizeof(viewerPath)];
  char title[sizeof(viewerTitle)];
  if (!joinPath(path, sizeof(path), currentPath, name)) {
    return false;
  }
  copyDisplayText(title, sizeof(title), name);
  const bool fullscreen = viewerFullscreen;
  closeViewer();

  const FileViewerExtension *viewer = nullptr;
  FileViewerActivity activity = viewerActivity();
  const FileViewerOpenResult result =
      openFileWithViewer(providerId, path, &viewer, &activity);
  if (result != FileViewerOpenResult::Opened) {
    char status[sizeof(statusText)];
    formatViewerStatus(viewer, result, status, sizeof(status));
    setTransientStatus(status);
    return true;
  }
  selectedIndex = index;
  openViewer(path, title, viewer);
  viewerFullscreen = fullscreen;
  return true;
}

bool FilesApp::providerMounted() const {
  if (!storageSelected) {
    return anyProviderMounted();
  }
  return providerIsMounted(fileProviderById(providerId));
}

void FilesApp::closeViewer() {
  if (activeViewer != nullptr && activeViewer->close != nullptr) {
    activeViewer->close();
  }
  activeViewer = nullptr;
  viewerPath[0] = '\0';
  viewerTitle[0] = '\0';
  viewerOffset = 0;
  viewerSize = 0;
  viewerFullscreen = false;
  viewerOptionsOpen = false;
  pendingWallpaper = PendingWallpaper::None;
  viewerPageJumpOpen = false;
  viewerPreviousAvailable = false;
  viewerNextAvailable = false;
  viewerPageInput = "";
  nextPdfProgressRedrawMs = 0;
}

FileViewerActivity FilesApp::viewerActivity() const {
  FileViewerActivity activity;
  activity.keepAwake = filesViewerKeepAwake;
  activity.sleepGuard = filesViewerSleepGuard;
  return activity;
}

FileViewerRuntime
FilesApp::makeViewerRuntime(bool fullscreen, FileViewerActivity *activity) const {
  FileViewerRuntime runtime;
  runtime.providerId = providerId;
  runtime.path = viewerPath;
  runtime.title = viewerTitle;
  runtime.offset = viewerOffset;
  runtime.size = viewerSize;
  runtime.fullscreen = fullscreen;
  runtime.imageDither = viewerDither;
  runtime.imageScaleToFit = viewerScaleToFit;
  runtime.imageCacheEntries = 1;
  runtime.hasPreviousTrack = viewerPreviousAvailable;
  runtime.hasNextTrack = viewerNextAvailable;
  runtime.activity = activity;
  return runtime;
}

void FilesApp::scrollViewerBy(int8_t lines) {
  if (activeViewer == nullptr || activeViewer->scroll == nullptr || lines == 0) {
    return;
  }
  FileViewerActivity activity = viewerActivity();
  FileViewerRuntime runtime = makeViewerRuntime(viewerFullscreen, &activity);
  activeViewer->scroll(runtime, lines);
  viewerOffset = runtime.offset > viewerSize ? viewerSize : runtime.offset;
}

bool FilesApp::handleViewerTouch(const TouchPoint &point) {
  if (viewerPageJumpOpen) {
    return handleViewerPageJumpTouch(point);
  }

  if (viewerOptionsOpen) {
    if (point.x < kViewerOptionsX || point.x >= kViewerOptionsX + kViewerOptionsW ||
        point.y < kViewerOptionsY || point.y >= kViewerOptionsY + kViewerOptionsH) {
      viewerOptionsOpen = false;
      markListDirty();
      return true;
    }
    const int16_t row = (point.y - kViewerOptionsY - 18) / kViewerOptionsRowH;
    if (row == 0) {
      viewerDither = static_cast<uint8_t>((viewerDither + 1U) & 3U);
    } else if (row == 1) {
      viewerScaleToFit = !viewerScaleToFit;
    } else if (row == 2) {
      viewerFullscreen = !viewerFullscreen;
      viewerOptionsOpen = false;
    } else if (row == 3) {
      pendingWallpaper = PendingWallpaper::Home;
      viewerOptionsOpen = false;
    } else if (row == 4) {
      pendingWallpaper = PendingWallpaper::Lockscreen;
      viewerOptionsOpen = false;
    }
    markListDirty();
    return true;
  }

  if (activeViewer != nullptr && activeViewer->touch != nullptr) {
    FileViewerActivity activity = viewerActivity();
    FileViewerRuntime runtime = makeViewerRuntime(viewerFullscreen, &activity);
    if (activeViewer->touch(runtime, point)) {
      viewerOffset = runtime.offset;
      if (runtime.size != 0) {
        viewerSize = runtime.size;
      }
      if (runtime.trackNavigation != 0) {
        return openAdjacentTrack(runtime.trackNavigation);
      }
      markListDirty();
      return true;
    }
  }

  if (point.y < kListY || point.y >= kListBottomY) {
    return false;
  }
  scrollViewerBy(point.y < kScrollSplitY ? -kViewerScrollLines
                                         : kViewerScrollLines);
  markListDirty();
  return true;
}

bool FilesApp::handleViewerPageJumpTouch(const TouchPoint &point) {
  viewerPageKeyboard.setLayout(T9KeyboardComponent::Layout::Numbers);
  const bool inputWasEmpty = viewerPageInput.length() == 0;
  KeyboardEvent event = viewerPageKeyboard.hitTest(point, viewerPageInput, 5);
  if (event.action == KEY_NONE) {
    return false;
  }

  if (event.action == KEY_OK) {
    const uint16_t pageCount =
        viewerPageCount(activeViewer, providerId, viewerPath);
    uint32_t page = static_cast<uint32_t>(atoi(viewerPageInput.c_str()));
    if (page > 0 && pageCount > 0) {
      if (viewerIsPdf(activeViewer) && page > pageCount &&
          pdfViewerRequestPage(providerId, viewerPath, page - 1U)) {
        viewerOffset = page - 1U;
      } else {
        if (page > pageCount) {
          page = pageCount;
        }
        viewerOffset = page - 1U;
      }
    }
    viewerPageJumpOpen = false;
    viewerPageInput = "";
  }
  if (event.action == KEY_BACKSPACE && inputWasEmpty) {
    viewerPageJumpOpen = false;
  }
  markDirtyRegion(0, 0, 200, 200);
  return true;
}

void FilesApp::draw(Adafruit_GFX &gfx) {
#if FILES_APP_TIMING_LOG
  filesTiming.nameShorten.reset();
  const uint32_t drawStartedAt = micros();
#endif
  if (activeViewer != nullptr) {
    drawViewer(gfx);
#if FILES_APP_TIMING_LOG
    filesLogPrintf("[files] draw viewer total=%luus ",
                   static_cast<unsigned long>(elapsedMicros(drawStartedAt)));
    logTimingAccumulator("shorten", filesTiming.nameShorten);
    finishFilesLogLine();
#endif
    return;
  }

  clearExpiredStatus();
  if (!storageSelected) {
    drawStoragePicker(gfx);
#if FILES_APP_TIMING_LOG
    filesLogPrintf("[files] draw storage total=%luus ",
                   static_cast<unsigned long>(elapsedMicros(drawStartedAt)));
    logTimingAccumulator("shorten", filesTiming.nameShorten);
    finishFilesLogLine();
#endif
    return;
  }

  if (!loaded || cardMounted != providerMounted()) {
    loadDirectory();
  }

  if (drawTextScratch == nullptr) {
    gfx.setTextColor(1);
    gfx.setTextSize(1);
    const char *message = "ALLOC FAILED";
    int16_t textX = 0;
    int16_t textY = 0;
    uint16_t textW = 0;
    uint16_t textH = 0;
    gfx.getTextBounds(message, 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor((200 - static_cast<int16_t>(textW)) / 2 - textX,
                  (200 - static_cast<int16_t>(textH)) / 2 - textY);
    gfx.print(message);
    return;
  }

  gfx.setTextColor(1);
  gfx.setTextSize(1);
  char *displayText = drawTextScratch;
  char *fittedText = drawTextScratch + FILES_PATH_CAPACITY;
  const char *folderName =
      isRootPath(currentPath) ? "/" : displayName(currentPath);
  copyDisplayText(displayText, FILES_PATH_CAPACITY, folderName);
  fitMiddleEllipsis(gfx, displayText, 140, fittedText,
                    FILES_PATH_CAPACITY);
  gfx.setCursor(4, kHeaderY);
  gfx.print(pickerMode ? "Pick a file" : fittedText);
  drawRightText(gfx, statusUntil != 0
                         ? statusText
                         : (pickerMode ? fittedText : "SPLIT"),
                kHeaderY);
  gfx.drawLine(0, kHeaderLineY, 199, kHeaderLineY, 1);

  if (entryCount == 0) {
    const char *emptyText = statusText[0] ? statusText : "empty";
    int16_t textX = 0;
    int16_t textY = 0;
    uint16_t textW = 0;
    uint16_t textH = 0;
    gfx.getTextBounds(emptyText, 0, 0, &textX, &textY, &textW, &textH);
    const int16_t listTop = kHeaderLineY + 1;
    gfx.setCursor((200 - static_cast<int16_t>(textW)) / 2 - textX,
                  listTop + (200 - listTop -
                             static_cast<int16_t>(textH)) /
                                2 -
                      textY);
    gfx.print(emptyText);
  }

  const uint8_t visibleCount =
      entries == nullptr ? 0 :
      entryCount - scrollOffset < MAX_VISIBLE_ENTRIES
          ? static_cast<uint8_t>(entryCount - scrollOffset)
          : MAX_VISIBLE_ENTRIES;
  for (uint8_t i = 0; i < visibleCount; i++) {
    const uint16_t index = scrollOffset + i;
    const int16_t y = kListY + i * kRowH;
    const FileEntry &entry = entries[index];
    const bool selected = index == selectedIndex;
    if (selected) {
      gfx.fillRect(2, y - 3, 196, kRowH - 1, 1);
      gfx.setTextColor(0);
    }
    drawIcon(gfx, entry.icon, kIconX, y + 14);

    char sizeText[16];
    formatSize(entry.directory ? 0 : entry.size, sizeText, sizeof(sizeText));
    const uint16_t sizeW = textWidth(gfx, sizeText);
    const int16_t nameMaxW =
        192 - kNameX - (sizeText[0] ? static_cast<int16_t>(sizeW) + 8 : 0);
    copyDisplayText(displayText, FILES_LABEL_CAPACITY, entryName(entry));
    fitMiddleEllipsis(gfx, displayText, nameMaxW, fittedText,
                      FILES_LABEL_CAPACITY);
    gfx.setCursor(kNameX, y + 4);
    gfx.print(fittedText);
    if (!entry.directory && entry.size > 0) {
      gfx.setCursor(192 - static_cast<int16_t>(sizeW), y + 4);
      gfx.print(sizeText);
    }
    if (selected) {
      gfx.setTextColor(1);
    }
  }

  drawScrollbar(gfx);
#if FILES_APP_TIMING_LOG
  filesLogPrintf("[files] draw list total=%luus entries=%u offset=%u loading=%u ",
                 static_cast<unsigned long>(elapsedMicros(drawStartedAt)),
                 static_cast<unsigned>(entryCount),
                 static_cast<unsigned>(scrollOffset), directoryLoading ? 1 : 0);
  logTimingAccumulator("shorten", filesTiming.nameShorten);
  finishFilesLogLine();
#endif
}

void FilesApp::drawViewer(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);

  if (viewerPageJumpOpen) {
    gfx.fillRect(0, 0, 200, 200, 0);
    viewerPageKeyboard.setLayout(T9KeyboardComponent::Layout::Numbers);
    viewerPageKeyboard.draw(gfx, viewerPageInput, 5);
    return;
  }

  if (pendingWallpaper != PendingWallpaper::None) {
    if (activeViewer != nullptr && activeViewer->draw != nullptr) {
      FileViewerActivity activity = viewerActivity();
      FileViewerRuntime runtime = makeViewerRuntime(true, &activity);
      activeViewer->draw(gfx, runtime);
    }
    const WallpaperTarget target =
        pendingWallpaper == PendingWallpaper::Home
            ? WallpaperTarget::Home
            : WallpaperTarget::Lockscreen;
    const WallpaperResult result =
        saveFramebufferWallpaper(static_cast<const AppDisplay &>(gfx), target);
    pendingWallpaper = PendingWallpaper::None;
    const char *message = wallpaperResultMessage(result);
    gfx.fillRect(24, 84, 152, 32, 0);
    gfx.drawRect(24, 84, 152, 32, 1);
    int16_t textX = 0;
    int16_t textY = 0;
    uint16_t textW = 0;
    uint16_t textH = 0;
    gfx.getTextBounds(message, 0, 0, &textX, &textY, &textW, &textH);
    gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(textW)) / 2 - textX,
                  100 - textY);
    gfx.print(message);
    return;
  }

  if (viewerFullscreen) {
    if (activeViewer != nullptr && activeViewer->draw != nullptr) {
      FileViewerActivity activity = viewerActivity();
      FileViewerRuntime runtime = makeViewerRuntime(true, &activity);
      activeViewer->draw(gfx, runtime);
    }
    if (viewerOptionsOpen) {
      drawViewerOptions(gfx);
    }
    return;
  }

  char title[sizeof(viewerTitle)];
  fitMiddleEllipsis(gfx, viewerTitle, 136, title, sizeof(title));
  gfx.setCursor(4, kHeaderY);
  gfx.print(title[0] != '\0' ? title : "file");
  char viewerStatus[18];
  if (viewerSupportsPageJump(activeViewer)) {
    FileViewerActivity activity = viewerActivity();
    FileViewerRuntime runtime = makeViewerRuntime(false, &activity);
    viewerPageStatus(activeViewer, runtime, viewerStatus, sizeof(viewerStatus));
    drawRightText(gfx, viewerStatus, kHeaderY);
  } else {
    drawRightText(gfx, activeViewer != nullptr && activeViewer->label != nullptr
                           ? activeViewer->label
                           : "VIEW",
                  kHeaderY);
  }
  gfx.drawLine(0, kHeaderLineY, 199, kHeaderLineY, 1);

  if (activeViewer != nullptr && activeViewer->draw != nullptr) {
    FileViewerActivity activity = viewerActivity();
    FileViewerRuntime runtime = makeViewerRuntime(false, &activity);
    activeViewer->draw(gfx, runtime);
    if (viewerSupportsPageJump(activeViewer)) {
      viewerSize = viewerPageCount(activeViewer, providerId, viewerPath);
      const bool pendingPdfPage =
          viewerIsPdf(activeViewer) &&
          pdfViewerLoading(providerId, viewerPath) &&
          viewerOffset >= viewerSize;
      if (pendingPdfPage && viewerOffset < UINT32_MAX) {
        // Preserve the requested lazy page until the rolling index finds it.
        viewerSize = viewerOffset + 1U;
      } else if (viewerSize > 0 && viewerOffset >= viewerSize) {
        viewerOffset = viewerSize - 1;
      }
    }
  }
  drawViewerScrollbar(gfx);
  if (viewerOptionsOpen) {
    drawViewerOptions(gfx);
  }
}

void FilesApp::drawViewerOptions(Adafruit_GFX &gfx) {
  gfx.fillRect(kViewerOptionsX, kViewerOptionsY, kViewerOptionsW,
               kViewerOptionsH, 0);
  gfx.drawRect(kViewerOptionsX, kViewerOptionsY, kViewerOptionsW,
               kViewerOptionsH, 1);
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(kViewerOptionsX + 8, kViewerOptionsY + 8);
  gfx.print("IMAGE");

  char line[28];
  snprintf(line, sizeof(line), "DITHER %s", imageDitherName(viewerDither));
  gfx.setCursor(kViewerOptionsX + 8, kViewerOptionsY + 28);
  gfx.print(line);
  snprintf(line, sizeof(line), "SCALE %s", viewerScaleToFit ? "FIT" : "1:1");
  gfx.setCursor(kViewerOptionsX + 8, kViewerOptionsY + 56);
  gfx.print(line);
  snprintf(line, sizeof(line), "FULL %s", viewerFullscreen ? "ON" : "OFF");
  gfx.setCursor(kViewerOptionsX + 8, kViewerOptionsY + 84);
  gfx.print(line);
  gfx.setCursor(kViewerOptionsX + 8, kViewerOptionsY + 112);
  gfx.print("SET HOME WALLPAPER");
  gfx.setCursor(kViewerOptionsX + 8, kViewerOptionsY + 140);
  gfx.print("SET LOCK WALLPAPER");
}

void FilesApp::drawViewerScrollbar(Adafruit_GFX &gfx) const {
  if (activeViewer == nullptr || activeViewer->visibleBytes == nullptr) {
    return;
  }

  FileViewerActivity activity = viewerActivity();
  FileViewerRuntime runtime = makeViewerRuntime(viewerFullscreen, &activity);
  const uint32_t visible = activeViewer->visibleBytes(runtime);
  drawVerticalScrollbar(gfx, viewerOffset, visible, viewerSize, kListY,
                        kListBottomY - kListY);
}

void FilesApp::drawScrollbar(Adafruit_GFX &gfx) const {
  drawVerticalScrollbar(gfx, scrollOffset, MAX_VISIBLE_ENTRIES, entryCount,
                        kListY, kListBottomY - kListY);
}

void FilesApp::drawStoragePicker(Adafruit_GFX &gfx) {
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  gfx.setCursor(4, kHeaderY);
  gfx.print(pickerMode ? "Pick a file" : "FILES");
  drawRightText(gfx, statusUntil != 0 ? statusText : "SELECT", kHeaderY);
  gfx.drawLine(0, kHeaderLineY, 199, kHeaderLineY, 1);

  const size_t count = fileProviderCount();
  for (size_t i = 0; i < count && i < MAX_VISIBLE_ENTRIES; i++) {
    const FileProvider *provider = fileProviderAt(i);
    const int16_t y = 38 + static_cast<int16_t>(i) * kRowH;
    const bool selected = selectedIndex == static_cast<int16_t>(i);
    if (selected) {
      gfx.fillRect(2, y - 3, 196, kRowH - 1, 1);
      gfx.setTextColor(0);
    }
    drawIcon(gfx, '4', kIconX, y + 14);
    gfx.setCursor(kNameX, y + 4);
    gfx.print(provider != nullptr && provider->label != nullptr
                  ? provider->label
                  : "STORAGE");
    char freeText[16];
    formatProviderFree(provider, freeText, sizeof(freeText));
    drawRightText(gfx, freeText, y + 4);
    if (selected) {
      gfx.setTextColor(1);
    }
  }
}

void FilesApp::markDirtyRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) {
    return;
  }
  if (!hasDirtyRegion) {
    dirtyX = x;
    dirtyY = y;
    dirtyW = w;
    dirtyH = h;
    hasDirtyRegion = true;
    return;
  }

  const int16_t left = dirtyX < x ? dirtyX : x;
  const int16_t top = dirtyY < y ? dirtyY : y;
  const int16_t right =
      dirtyX + dirtyW > x + w ? dirtyX + dirtyW : x + w;
  const int16_t bottom =
      dirtyY + dirtyH > y + h ? dirtyY + dirtyH : y + h;
  dirtyX = left;
  dirtyY = top;
  dirtyW = right - left;
  dirtyH = bottom - top;
}

void FilesApp::markListDirty() {
  markDirtyRegion(0, 0, 200, 200);
}

bool FilesApp::handleTouch(const TouchPoint &point) {
  if (activeViewer != nullptr) {
    return handleViewerTouch(point);
  }

  if (!storageSelected) {
    if (point.y >= 38 && point.y < 38 + MAX_VISIBLE_ENTRIES * kRowH) {
      const int16_t index = (point.y - 38) / kRowH;
      if (index < 0 || static_cast<size_t>(index) >= fileProviderCount()) {
        return false;
      }
      if (selectedIndex != index) {
        selectedIndex = index;
        setStatus("");
        markListDirty();
        return true;
      }
      return activateStorage();
    }
    return false;
  }

  if (point.y < kListY || point.y >= kListBottomY) {
    return false;
  }

  uint16_t nextOffset = scrollOffset;
  const UiListTouchResult listTouch = uiListHandleTouch(
      point, entryCount, MAX_VISIBLE_ENTRIES, kListY, kRowH,
      kListBottomY - kListY, kListSplitX, nextOffset);
  if (listTouch.action == UiListTouchAction::Scroll) {
    if (scrollOffset != nextOffset) {
      scrollOffset = nextOffset;
      selectedIndex = NO_SELECTION;
      markListDirty();
      if (entryCount > 0) {
        setStatus(truncatedDirectory ? "LIMITED" : "");
      }
    }
    return true;
  }
  if (listTouch.action != UiListTouchAction::Select ||
      listTouch.index >= entryCount) {
    return false;
  }
  const uint16_t index = listTouch.index;
  if (selectedIndex != index) {
    selectedIndex = index;
    setStatus("");
    markListDirty();
    return true;
  }
  return activateIndex(index);
}

bool FilesApp::handleMenuButton() {
  if (activeViewer != nullptr) {
    if (viewerPageJumpOpen) {
      viewerPageJumpOpen = false;
      viewerPageInput = "";
      return true;
    }
    if (activeViewer->togglePlayback != nullptr &&
        audioPlayerConfiguredLayout() == AudioPlayerLayoutStyle::Artwork) {
      volumeDown();
      audioPlayerShowVolumeFeedback();
      return true;
    }
    closeViewer();
    return true;
  }

  if (!storageSelected) {
    return false;
  }
  if (isRootPath(currentPath)) {
    storageSelected = false;
    loaded = false;
    scrollOffset = 0;
    selectedIndex = NO_SELECTION;
    setStatus("");
    return true;
  }
  goUp();
  return true;
}

bool FilesApp::playerButtonsControlVolume() const {
  return activeViewer != nullptr && activeViewer->togglePlayback != nullptr &&
         audioPlayerConfiguredLayout() == AudioPlayerLayoutStyle::Artwork;
}

bool FilesApp::handleMenuDoubleButton() {
  if (!playerButtonsControlVolume()) {
    return false;
  }
  volumeDown();
  volumeDown();
  audioPlayerShowVolumeFeedback();
  return true;
}

bool FilesApp::handleMenuLongButton() {
  if (activeViewer == nullptr) {
    return false;
  }
  closeViewer();
  return true;
}

bool FilesApp::handlePowerButton() {
  if (activeViewer != nullptr) {
    if (activeViewer->togglePlayback != nullptr) {
      if (audioPlayerConfiguredLayout() == AudioPlayerLayoutStyle::Artwork) {
        volumeUp();
        audioPlayerShowVolumeFeedback();
        markDirtyRegion(0, 0, 200, 200);
        return true;
      }
      FileViewerActivity activity = viewerActivity();
      FileViewerRuntime runtime = makeViewerRuntime(viewerFullscreen, &activity);
      if (activeViewer->togglePlayback(runtime)) {
        viewerOffset = runtime.offset;
        if (runtime.size != 0) {
          viewerSize = runtime.size;
        }
        markDirtyRegion(0, 0, 200, 200);
      }
    } else if (activeViewer->id != nullptr &&
        (equalsIgnoreCase(activeViewer->id, "raster") ||
         equalsIgnoreCase(activeViewer->id, "png"))) {
      viewerOptionsOpen = !viewerOptionsOpen;
      markDirtyRegion(0, 0, 200, 200);
    } else if (activeViewer->id != nullptr && equalsIgnoreCase(activeViewer->id, "svg")) {
      viewerFullscreen = !viewerFullscreen;
      markDirtyRegion(0, 0, 200, 200);
    } else if (viewerSupportsPageJump(activeViewer)) {
      if ((viewerIsPdf(activeViewer) && pdfViewerLoading(providerId, viewerPath)) ||
          (viewerIsEpub(activeViewer) && epubViewerLoading(providerId, viewerPath))) {
        return true;
      }
      viewerPageJumpOpen = !viewerPageJumpOpen;
      viewerPageInput = "";
      markDirtyRegion(0, 0, 200, 200);
    }
    return true;
  }

  if (!storageSelected) {
    return activateStorage();
  }
  if (selectedIndex < 0 || selectedIndex >= entryCount) {
    return false;
  }
  return activateIndex(static_cast<uint16_t>(selectedIndex));
}

bool FilesApp::handlePowerDoubleButton() {
  if (!playerButtonsControlVolume()) {
    return false;
  }
  volumeUp();
  volumeUp();
  audioPlayerShowVolumeFeedback();
  return true;
}

bool FilesApp::update() {
  if (activeViewer != nullptr && activeViewer->update != nullptr) {
    FileViewerActivity activity = viewerActivity();
    FileViewerRuntime runtime = makeViewerRuntime(viewerFullscreen, &activity);
    if (activeViewer->update(runtime)) {
      viewerOffset = runtime.offset;
      if (runtime.size != 0) {
        viewerSize = runtime.size;
      }
      markDirtyRegion(0, 0, 200, 200);
      return true;
    }
  }

  if (viewerIsPdf(activeViewer)) {
    inactivitySleepKeepAwake();
    const bool cacheReady = pdfViewerProgress(providerId, viewerPath) == 100;
    const bool wasLoading = pdfViewerLoading(providerId, viewerPath) ||
                            (!cacheReady &&
                             pdfViewerPageCount(providerId, viewerPath) == 0);
    bool finished = false;
    if (!cacheReady || pdfViewerLoading(providerId, viewerPath)) {
      LockscreenGuard sleepGuard;
      finished = pdfViewerContinueLoading(providerId, viewerPath,
                                          kPdfBackgroundIndexBudgetUs);
    }
    const bool loading = pdfViewerLoading(providerId, viewerPath);
    if (!loading) {
      viewerSize = pdfViewerPageCount(providerId, viewerPath);
      if (viewerSize > 0 && viewerOffset >= viewerSize) {
        viewerOffset = viewerSize - 1;
      }
      if (wasLoading || finished) {
        nextPdfProgressRedrawMs = 0;
        markDirtyRegion(0, 0, 200, 200);
        return true;
      }
    } else {
      const unsigned long now = millis();
      if (nextPdfProgressRedrawMs == 0 || now >= nextPdfProgressRedrawMs) {
        nextPdfProgressRedrawMs = now + kPdfProgressRedrawMs;
        markDirtyRegion(0, 0, 200, 200);
        return true;
      }
      return false;
    }
  }
  if (viewerIsEpub(activeViewer)) {
    const bool wasLoading = epubViewerLoading(providerId, viewerPath);
    const bool imageLoading =
        epubViewerImageLoading(providerId, viewerPath, viewerOffset);
    if (wasLoading || imageLoading) {
      inactivitySleepKeepAwake();
    }
    if (wasLoading) {
      LockscreenGuard sleepGuard;
      const bool stillActive = epubViewerContinueLoading(
          providerId, viewerPath, kEpubBackgroundIndexBudgetUs);
      const bool loading = epubViewerLoading(providerId, viewerPath);
      if (!loading) {
        viewerSize = epubViewerPageCount(providerId, viewerPath);
        if (viewerSize > 0 && viewerOffset >= viewerSize) {
          viewerOffset = viewerSize - 1;
        }
        markDirtyRegion(0, 0, 200, 200);
        return true;
      }
      const unsigned long now = millis();
      if (!stillActive || nextPdfProgressRedrawMs == 0 ||
          now >= nextPdfProgressRedrawMs) {
        nextPdfProgressRedrawMs = now + kPdfProgressRedrawMs;
        markDirtyRegion(0, 0, 200, 200);
        return true;
      }
      return false;
    }
    if (imageLoading) {
      LockscreenGuard sleepGuard;
      epubViewerContinueImage(providerId, viewerPath, viewerOffset);
      markDirtyRegion(0, 0, 200, 200);
      return true;
    }
  }
  if (clearExpiredStatus()) {
    return true;
  }
  const bool mounted = providerMounted();
  if (storageSelected && !mounted) {
    copyText(currentPath, sizeof(currentPath), "/");
    scrollOffset = 0;
    selectedIndex = NO_SELECTION;
    storageSelected = false;
    providerId[0] = '\0';
    loaded = false;
    directoryLoading = false;
    directoryScanPass = DirectoryScanPass::Folders;
    directoryScanRoot = File();
    entryCount = 0;
    folderCount = 0;
    entryNamePoolUsed = 0;
    if (entryNamePool != nullptr) {
      entryNamePool[0] = '\0';
    }
    setStatus("");
    return true;
  }

  if (directoryLoading) {
    const uint16_t before = entryCount;
    continueDirectoryLoad(16);
    return (before == 0 && entryCount > 0) || !directoryLoading;
  }

  if (cardMounted == mounted) {
    return false;
  }
  if (storageSelected) {
    loadDirectory();
  } else {
    cardMounted = mounted;
  }
  return true;
}
