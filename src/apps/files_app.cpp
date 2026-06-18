#include "apps/files_app.h"
#include "fs/file_provider.h"
#include "fs/providers/epub_file_viewer.h"
#include "fs/providers/pdf_file_viewer.h"
#include "sys/inactivity_sleep_guard.h"
#include "ui/icon_ascii_font.h"

#include <Arduino.h>
#include <FS.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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
  char line[192];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
#if FILES_APP_HAS_ROM_PRINTF
  esp_rom_printf("%s", line);
#endif
  Serial.print(line);
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
    inactivitySleepGuardAcquire();
  } else {
    inactivitySleepGuardRelease();
  }
}

struct FilesContextSnapshotV1 {
  uint8_t version = 1;
  char path[96] = "/";
};

struct FilesContextSnapshotV3 {
  uint8_t version = 3;
  char path[96] = "/";
  uint16_t scrollOffset = 0;
  int16_t selectedIndex = -1;
  uint8_t mode = 0;
  uint8_t storageSelected = 0;
};

struct FilesContextSnapshot {
  uint8_t version = 5;
  char path[FILES_PATH_CAPACITY] = "/";
  char providerId[12] = {};
  uint16_t scrollOffset = 0;
  int16_t selectedIndex = -1;
  uint8_t mode = 0;
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
static const int8_t kScrollStep = 3;
static const unsigned long kStatusMs = 2000;
static const unsigned long kPdfProgressRedrawMs = 2000;
static const uint32_t kPdfBackgroundIndexBudgetUs = 180000;
static const uint32_t kEpubBackgroundIndexBudgetUs = 180000;
static const uint8_t kSmallFolderLimit = 8;
static const int8_t kViewerScrollLines = 4;
static const int16_t kScrollbarX = 198;
static const int16_t kScrollbarW = 2;
static const int16_t kViewerOptionsX = 20;
static const int16_t kViewerOptionsY = 54;
static const int16_t kViewerOptionsW = 160;
static const int16_t kViewerOptionsH = 96;
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
      extensionIs(extension, "opus")) {
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
      extensionIs(extension, "pink")) {
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
  copyText(currentPath, sizeof(currentPath), "/");
  scrollOffset = 0;
  selectedIndex = NO_SELECTION;
  statusUntil = 0;
  mode = Mode::Select;
  activeViewer = nullptr;
  storageSelected = false;
  setStatus("");
  loaded = false;
  directoryLoading = false;
  directoryScanPass = DirectoryScanPass::Folders;
  directoryScanRoot = File();
  folderCount = 0;
  providerId[0] = '\0';
  viewerPath[0] = '\0';
  viewerTitle[0] = '\0';
  viewerOptionsOpen = false;
  viewerPageJumpOpen = false;
  viewerPageInput = "";
  hasDirtyRegion = false;
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
  snapshot.mode = static_cast<uint8_t>(mode);
  snapshot.storageSelected = storageSelected ? 1 : 0;
  memcpy(buffer, &snapshot, sizeof(snapshot));
  return sizeof(snapshot);
}

void FilesApp::restoreContext(const uint8_t *buffer, size_t length) {
  if (buffer == nullptr) {
    return;
  }

  if (length == sizeof(FilesContextSnapshotV1)) {
    FilesContextSnapshotV1 snapshot;
    memcpy(&snapshot, buffer, sizeof(snapshot));
    snapshot.path[sizeof(snapshot.path) - 1] = '\0';
    if (snapshot.version == 1 && snapshot.path[0] == '/') {
      openDirectory(snapshot.path);
    }
    return;
  }

  FilesContextSnapshot snapshot;
  if (length == sizeof(FilesContextSnapshotV3)) {
    FilesContextSnapshotV3 previous;
    memcpy(&previous, buffer, sizeof(previous));
    previous.path[sizeof(previous.path) - 1] = '\0';
    if (previous.version != 2 && previous.version != 3) {
      return;
    }
    copyText(snapshot.path, sizeof(snapshot.path), previous.path);
    const FileProvider *provider = defaultFileProvider();
    copyText(snapshot.providerId, sizeof(snapshot.providerId),
             provider != nullptr ? provider->id : "");
    snapshot.scrollOffset = previous.scrollOffset;
    snapshot.selectedIndex = previous.selectedIndex;
    snapshot.mode = previous.mode;
    snapshot.storageSelected = previous.storageSelected;
  } else if (length == sizeof(FilesContextSnapshot)) {
    memcpy(&snapshot, buffer, sizeof(snapshot));
    snapshot.path[sizeof(snapshot.path) - 1] = '\0';
    snapshot.providerId[sizeof(snapshot.providerId) - 1] = '\0';
  } else {
    return;
  }

  if (snapshot.version != 2 && snapshot.version != 3) {
    if (snapshot.version != 5) {
      return;
    }
  }
  if (snapshot.path[0] != '/') {
    return;
  }
  storageSelected = snapshot.version == 2 || snapshot.storageSelected != 0;
  copyText(providerId, sizeof(providerId), snapshot.providerId);
  if (!storageSelected) {
    copyText(currentPath, sizeof(currentPath), "/");
    providerId[0] = '\0';
    loaded = false;
    mode = Mode::Select;
    selectedIndex = NO_SELECTION;
    return;
  }

  copyText(currentPath, sizeof(currentPath), snapshot.path);
  loadDirectory();
  scrollOffset = snapshot.scrollOffset;
  selectedIndex = snapshot.selectedIndex;
  mode = snapshot.mode == static_cast<uint8_t>(Mode::Select) ? Mode::Select
                                                             : Mode::Scroll;
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
    entryNamePool[0] = '\0';
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
  if (selectedIndex == NO_SELECTION) {
    mode = entryCount < kSmallFolderLimit ? Mode::Select : Mode::Scroll;
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
    if (!isMetadataName(name) && isDirectory == wantsDirectory) {
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
  if (needed > sizeof(entryNamePool) - entryNamePoolUsed) {
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
  if (entry.nameOffset >= entryNamePoolUsed) {
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
  mode = entryCount < kSmallFolderLimit ? Mode::Select : Mode::Scroll;
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

void FilesApp::scrollBy(int8_t delta) {
  const uint16_t maxOffset =
      entryCount > MAX_VISIBLE_ENTRIES ? entryCount - MAX_VISIBLE_ENTRIES : 0;
  int32_t next = static_cast<int32_t>(scrollOffset) + delta;
  if (next < 0) {
    next = 0;
  }
  if (next > maxOffset) {
    next = maxOffset;
  }
  scrollOffset = static_cast<uint16_t>(next);
  selectedIndex = NO_SELECTION;
  markListDirty();
  if (entryCount > 0) {
    setStatus(truncatedDirectory ? "LIMITED" : "");
  }
}

bool FilesApp::activateIndex(uint16_t index) {
  if (index >= entryCount) {
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

  const FileViewerExtension *viewer = nullptr;
  FileViewerActivity activity = viewerActivity();
  const FileViewerOpenResult result =
      openFileWithViewer(providerId, path, &viewer, &activity);
  if (result == FileViewerOpenResult::Opened) {
    char title[FILES_LABEL_CAPACITY];
    copyDisplayText(title, sizeof(title), name);
    return openViewer(path, title, viewer != nullptr ? viewer->id : "");
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

bool FilesApp::openViewer(const char *path, const char *title,
                          const char *viewerId) {
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
  viewerPageJumpOpen = false;
  viewerScaleToFit = true;
  viewerDither = 1;
  viewerPageInput = "";
  nextPdfProgressRedrawMs = 0;

  activeViewer = nullptr;
  const FileViewerExtension *viewer = findFileViewerForPath(path);
  if (viewer == nullptr || viewer->draw == nullptr) {
    setTransientStatus("NOT SUPPORTED");
    return true;
  }
  if (!equalsIgnoreCase(viewerId, viewer->id)) {
    setTransientStatus("NOT SUPPORTED");
    return true;
  }

  activeViewer = viewer;
  if (viewerSupportsPageJump(activeViewer)) {
    viewerSize = viewerPageCount(activeViewer, providerId, viewerPath);
  }
  return true;
}

bool FilesApp::providerMounted() const {
  if (!storageSelected) {
    return anyProviderMounted();
  }
  return providerIsMounted(fileProviderById(providerId));
}

void FilesApp::closeViewer() {
  activeViewer = nullptr;
  viewerPath[0] = '\0';
  viewerTitle[0] = '\0';
  viewerOffset = 0;
  viewerSize = 0;
  viewerFullscreen = false;
  viewerOptionsOpen = false;
  viewerPageJumpOpen = false;
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
    }
    markListDirty();
    return true;
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
  KeyboardEvent event = viewerPageKeyboard.hitTest(point, viewerPageInput, 5);
  if (event.action == KEY_NONE) {
    return false;
  }

  if (event.action == KEY_OK) {
    const uint16_t pageCount =
        viewerPageCount(activeViewer, providerId, viewerPath);
    uint32_t page = static_cast<uint32_t>(atoi(viewerPageInput.c_str()));
    if (page > 0 && pageCount > 0) {
      if (page > pageCount) {
        page = pageCount;
      }
      viewerOffset = page - 1U;
    }
    viewerPageJumpOpen = false;
    viewerPageInput = "";
  }
  if (event.action == KEY_BACKSPACE && viewerPageInput.length() == 0) {
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

  gfx.setTextColor(1);
  gfx.setTextSize(1);
  char folderLabel[sizeof(currentPath)];
  char displayFolderName[sizeof(currentPath)];
  const char *folderName =
      isRootPath(currentPath) ? "/" : displayName(currentPath);
  copyDisplayText(displayFolderName, sizeof(displayFolderName), folderName);
  fitMiddleEllipsis(gfx, displayFolderName, 140, folderLabel,
                    sizeof(folderLabel));
  gfx.setCursor(4, kHeaderY);
  gfx.print(folderLabel);
  drawRightText(gfx,
                statusUntil != 0 ? statusText
                                 : (mode == Mode::Scroll ? "SCROLL" : "SELECT"),
                kHeaderY);
  gfx.drawLine(0, kHeaderLineY, 199, kHeaderLineY, 1);

  if (entryCount == 0) {
    gfx.setCursor(72, 92);
    gfx.print(statusText[0] ? statusText : "empty");
  }

  const uint8_t visibleCount =
      entryCount - scrollOffset < MAX_VISIBLE_ENTRIES
          ? static_cast<uint8_t>(entryCount - scrollOffset)
          : MAX_VISIBLE_ENTRIES;
  for (uint8_t i = 0; i < visibleCount; i++) {
    const uint16_t index = scrollOffset + i;
    const int16_t y = kListY + i * kRowH;
    const FileEntry &entry = entries[index];
    const bool selected = mode == Mode::Select && index == selectedIndex;
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
    char displayLabel[FILES_LABEL_CAPACITY];
    char label[FILES_LABEL_CAPACITY];
    copyDisplayText(displayLabel, sizeof(displayLabel), entryName(entry));
    fitMiddleEllipsis(gfx, displayLabel, nameMaxW, label, sizeof(label));
    gfx.setCursor(kNameX, y + 4);
    gfx.print(label);
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
      if (viewerSize > 0 && viewerOffset >= viewerSize) {
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
  gfx.print("FILES");
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

  if (mode == Mode::Scroll) {
    scrollBy(point.y < kScrollSplitY ? -kScrollStep : kScrollStep);
    return true;
  }

  const uint8_t row = (point.y - kListY) / kRowH;
  const uint16_t index = scrollOffset + row;
  if (row >= MAX_VISIBLE_ENTRIES || index >= entryCount) {
    return false;
  }
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
    mode = Mode::Select;
    setStatus("");
    return true;
  }
  goUp();
  return true;
}

bool FilesApp::handlePowerButton() {
  if (activeViewer != nullptr) {
    if (activeViewer->id != nullptr &&
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
    mode = Mode::Select;
    return true;
  }
  mode = mode == Mode::Scroll ? Mode::Select : Mode::Scroll;
  selectedIndex = NO_SELECTION;
  clampView();
  return true;
}

bool FilesApp::update() {
  if (viewerIsPdf(activeViewer)) {
    inactivitySleepKeepAwake();
    const bool cacheReady = pdfViewerProgress(providerId, viewerPath) == 100;
    const bool wasLoading = pdfViewerLoading(providerId, viewerPath) ||
                            (!cacheReady &&
                             pdfViewerPageCount(providerId, viewerPath) == 0);
    bool finished = false;
    if (!cacheReady || pdfViewerLoading(providerId, viewerPath)) {
      ScopedInactivitySleepGuard sleepGuard;
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
      ScopedInactivitySleepGuard sleepGuard;
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
      ScopedInactivitySleepGuard sleepGuard;
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
    mode = Mode::Select;
    storageSelected = false;
    providerId[0] = '\0';
    loaded = false;
    directoryLoading = false;
    directoryScanPass = DirectoryScanPass::Folders;
    directoryScanRoot = File();
    entryCount = 0;
    folderCount = 0;
    entryNamePoolUsed = 0;
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
