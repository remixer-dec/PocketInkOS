#include "sys/pink_executable.h"
#include "sys/pink_library.h"
#include "sys/agentfiesta/agentfiesta.h"
#include "sys/debug_logging.h"
#include "fs/providers/image_file_viewer.h"
#include "fs/providers/svg_file_viewer.h"
#include "sys/ext_app_api/app_api.h"
#include "sys/psram_allocator.h"
#include "sys/services/connectivity_service.h"
#include "fs/file_provider.h"
#include "sys/system_config.h"
#include "sys/global.h"
#include "sys/services/lockscreen_guard.h"
#include "sys/services/sd_storage.h"
#include "sys/system_fonts.h"
#include "ui/system_qr.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <stdio.h>
#include <new>

#ifndef MALLOC_CAP_EXEC
#define MALLOC_CAP_EXEC 0
#endif
#ifndef MALLOC_CAP_32BIT
#define MALLOC_CAP_32BIT 0
#endif

namespace {

static const uint16_t kPinkPathCapacity = 192;
static const uint8_t kPinkLabelCapacity = PINK_APP_LABEL_CAPACITY;
static const uint8_t kPinkIconCapacity = PINK_APP_ICON_CAPACITY;
static const uint8_t kPinkIdCapacity = 16;
static const size_t kPinkMemorySize = 4096;
static const size_t kReturnableJsPathCapacity = 288U;
static const size_t kReturnableJsMaxStateBytes = 256U * 1024U;
static const uint32_t kReturnableJsResultMagic = 0x52534a50UL; // "PJSR"
static const size_t kPinkMaxRelocations = 4096;
static const size_t kPinkLibraryMaxCodeBytes = 64U * 1024U;
static const size_t kPinkLibraryMaxRodataBytes = 512U * 1024U;
static const size_t kPinkLibraryMaxImageBytes =
    kPinkLibraryMaxCodeBytes + kPinkLibraryMaxRodataBytes;
static const uint32_t kPinkLibraryMaxRelocations = 16384;

bool hiddenByMenuButtonApp(const char *id) {
  return id != nullptr && strcmp(id, currentSystemConfig().home.sunBtnApp) == 0;
}

struct PinkExecutableEntry {
  char *id = nullptr;
  char *path = nullptr;
  char *label = nullptr;
  char *icon = nullptr;
  bool iconFont = false;
  MenuCategory category = MENU_APPS;
  PeriodicTaskSchedule periodic;
  uint32_t packedPeriodicSchedule = 0;
};

struct PinkScanContext {
  size_t next = 0;
  const char *directory = nullptr;
  MenuCategory category = MENU_APPS;
};

struct ReturnableJsResultTrailer {
  uint32_t magic = kReturnableJsResultMagic;
  uint16_t version = 1;
  uint16_t size = sizeof(ReturnableJsResultTrailer);
  char error[120] = {};
};

struct ReturnableJsState {
  char ownerPath[kReturnableJsPathCapacity] = {};
  char sourcePath[kReturnableJsPathCapacity] = {};
  uint8_t *state = nullptr;
  size_t stateSize = 0;
  bool launched = false;
  bool errorSet = false;
};

PinkExecutableEntry *pinkApps = nullptr;
size_t pinkAppCount = 0;
size_t pinkAppCapacity = 0;
Adafruit_GFX *pinkDrawTarget = nullptr;
ImageRenderCachePool pinkImageRenderCache;
char activeAppId[kPinkIdCapacity] = {};
char activeAppPath[kPinkPathCapacity] = {};
uint8_t *activeMemory = nullptr;
size_t activeMemorySize = 0;
uint8_t pinkSleepGuardDepth = 0;
uint8_t pinkCurrentEventType = 0;
uint8_t pinkFileAuthorizationIntent = 0;
ReturnableJsState *pendingReturnableJs = nullptr;

class PinkExecutableApp;
PinkExecutableApp *activePinkApp = nullptr;

bool copyReturnableJsPath(char *out, size_t capacity, const char *path) {
  if (out == nullptr || capacity == 0 || path == nullptr || path[0] != '/') {
    return false;
  }
  const size_t length = strlen(path);
  if (length >= capacity) {
    return false;
  }
  memcpy(out, path, length + 1U);
  return true;
}

uint32_t hashPath(const char *text) {
  uint32_t hash = 2166136261UL;
  while (text != nullptr && *text != '\0') {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619UL;
  }
  return hash;
}

bool extensionIsPink(const char *name) {
  if (name == nullptr || name[0] == '.') {
    return false;
  }
  const char *dot = strrchr(name, '.');
  if (dot == nullptr) {
    return false;
  }
  return strcasecmp(dot, ".pink") == 0;
}

size_t align4(size_t value) { return (value + 3U) & ~static_cast<size_t>(3U); }

bool copyText(char *dest, size_t destSize, const char *source) {
  if (dest == nullptr || destSize == 0) {
    return false;
  }
  const char *text = source != nullptr ? source : "";
  const size_t length = strlen(text);
  const size_t copyLength = length < destSize ? length : destSize - 1;
  if (copyLength > 0) {
    memcpy(dest, text, copyLength);
  }
  dest[copyLength] = '\0';
  return length < destSize;
}

bool validPinkLibraryManifest(const PinkExecutableHeader &header,
                              const PinkLibraryManifest &manifest,
                              uint32_t expectedType = 0) {
  return pinkExecutableHeaderSupported(header) &&
         header.headerSize ==
             sizeof(PinkExecutableHeader) + sizeof(PinkLibraryManifest) &&
         manifest.magic == PINK_LIBRARY_MAGIC &&
         manifest.version == PINK_LIBRARY_MANIFEST_VERSION &&
         manifest.size == sizeof(PinkLibraryManifest) &&
         (expectedType == 0 || manifest.type == expectedType) &&
         manifest.id[0] != '\0' &&
         memchr(manifest.id, '\0', sizeof(manifest.id)) != nullptr &&
         manifest.extensions[0] != '\0' &&
         memchr(manifest.extensions, '\0', sizeof(manifest.extensions)) !=
             nullptr;
}

bool readPinkLibraryManifest(File &file, PinkExecutableHeader &header,
                             PinkLibraryManifest &manifest,
                             uint32_t expectedType = 0) {
  return file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) ==
             sizeof(header) &&
         file.read(reinterpret_cast<uint8_t *>(&manifest), sizeof(manifest)) ==
             sizeof(manifest) &&
         validPinkLibraryManifest(header, manifest, expectedType);
}

const char *pinkLibraryLoadError(PinkExecutableLoadResult result) {
  switch (result) {
  case PinkExecutableLoadResult::Loaded:
    return "";
  case PinkExecutableLoadResult::InvalidExecutable:
    return "invalid library image";
  case PinkExecutableLoadResult::InvalidRelocations:
    return "invalid library relocations";
  case PinkExecutableLoadResult::SeekFailed:
    return "library seek failed";
  case PinkExecutableLoadResult::NoExecutableMemory:
    return "no executable memory";
  case PinkExecutableLoadResult::NoDataMemory:
    return "no library data memory";
  case PinkExecutableLoadResult::ReadFailed:
    return "library read failed";
  }
  return "library load failed";
}

void copyLabel(char *dest, size_t destSize, const char *filename) {
  if (dest == nullptr || destSize == 0) {
    return;
  }

  size_t out = 0;
  const char *cursor = filename != nullptr ? filename : "";
  while (*cursor != '\0' && *cursor != '.' && out + 1 < destSize) {
    const char c = *cursor++;
    dest[out++] = (c >= 0x20 && c <= 0x7e) ? c : '_';
  }
  dest[out] = '\0';
  if (dest[0] == '\0') {
    copyText(dest, destSize, "pink");
  }
}

void copyIcon(char *dest, size_t destSize, const char *label) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  char c = label != nullptr && label[0] != '\0' ? label[0] : 'P';
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  dest[0] = (c >= 0x20 && c <= 0x7e) ? c : 'P';
  if (destSize > 1) {
    dest[1] = '\0';
  }
}

bool printableMetadataText(const char *text, size_t capacity) {
  if (text == nullptr || capacity == 0 || text[0] == '\0' ||
      memchr(text, '\0', capacity) == nullptr) {
    return false;
  }
  for (size_t i = 0; i < capacity && text[i] != '\0'; i++) {
    if (text[i] < 0x20 || text[i] > 0x7e) {
      return false;
    }
  }
  return true;
}

bool validAppMetadata(const PinkExecutableHeader &header,
                      const PinkAppMetadata &metadata) {
  return pinkExecutableHeaderSupported(header) &&
         header.headerSize ==
             sizeof(PinkExecutableHeader) + sizeof(PinkAppMetadata) &&
         metadata.magic == PINK_APP_METADATA_MAGIC &&
         metadata.version == PINK_APP_METADATA_VERSION &&
         metadata.size == sizeof(PinkAppMetadata) &&
         printableMetadataText(metadata.label, sizeof(metadata.label)) &&
         printableMetadataText(metadata.icon, sizeof(metadata.icon));
}

bool readAppMetadata(const char *path, char *label, size_t labelSize,
                     char *icon, size_t iconSize, bool &iconFont) {
  File file = openProviderPath("sd", path);
  if (!file) {
    return false;
  }
  PinkExecutableHeader header;
  PinkAppMetadata metadata;
  const bool read =
      file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) ==
          sizeof(header) &&
      file.read(reinterpret_cast<uint8_t *>(&metadata), sizeof(metadata)) ==
          sizeof(metadata);
  file.close();
  if (!read || !validAppMetadata(header, metadata)) {
    return false;
  }
  const bool markedIcon = metadata.icon[0] == '@' && metadata.icon[1] != '\0';
  if (!copyText(label, labelSize, metadata.label) ||
      !copyText(icon, iconSize,
                markedIcon ? metadata.icon + 1U : metadata.icon)) {
    return false;
  }
  iconFont = markedIcon;
  return true;
}

const char *filenameForPath(const char *path) {
  if (path == nullptr) {
    return "";
  }
  const char *slash = strrchr(path, '/');
  return slash != nullptr ? slash + 1 : path;
}

bool joinPath(char *dest, size_t destSize, const char *directory,
              const char *name) {
  const int written = snprintf(dest, destSize, "%s/%s", directory, name);
  return written >= 0 && static_cast<size_t>(written) < destSize;
}

bool decodePeriodicSchedule(uint32_t packed, PeriodicTaskSchedule &out) {
  out = PeriodicTaskSchedule{};
  if (packed == 0) {
    return false;
  }
  const uint16_t interval = static_cast<uint16_t>(packed & 0xffffU);
  const uint8_t wakeLevel = static_cast<uint8_t>((packed >> 16U) & 0xffU);
  const uint8_t displayMode = static_cast<uint8_t>((packed >> 24U) & 0xffU);
  if (interval < CONFIG_PERIODIC_INTERVAL_MIN_MINUTES ||
      interval > CONFIG_PERIODIC_INTERVAL_MAX_MINUTES ||
      (wakeLevel != static_cast<uint8_t>(PeriodicWakeLevel::System) &&
       wakeLevel != static_cast<uint8_t>(PeriodicWakeLevel::Network)) ||
      displayMode > static_cast<uint8_t>(PeriodicDisplayMode::Full)) {
    return false;
  }
  out.intervalMinutes = interval;
  out.wakeLevel = static_cast<PeriodicWakeLevel>(wakeLevel);
  out.displayMode = static_cast<PeriodicDisplayMode>(displayMode);
  return true;
}

bool readPeriodicSchedule(const char *path, PeriodicTaskSchedule &schedule,
                          uint32_t &packed) {
  schedule = PeriodicTaskSchedule{};
  packed = 0;
  File file = openProviderPath("sd", path);
  if (!file) {
    return false;
  }
  PinkExecutableHeader header;
  const bool read =
      file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) ==
      sizeof(header);
  file.close();
  if (!read || !pinkExecutableHeaderSupported(header) ||
      header.abiVersion < 12 ||
      !decodePeriodicSchedule(header.periodicSchedule, schedule)) {
    return false;
  }
  packed = header.periodicSchedule;
  return true;
}

void clearPinkEntries() {
  for (size_t i = 0; i < pinkAppCount; i++) {
    free(pinkApps[i].id);
    free(pinkApps[i].path);
    free(pinkApps[i].label);
    free(pinkApps[i].icon);
    pinkApps[i] = PinkExecutableEntry{};
  }
  pinkAppCount = 0;
}

bool visiblePinkApp() { return sdStorageMounted(); }

bool ensurePinkEntries() {
  if (pinkApps != nullptr && pinkAppCapacity > 0) {
    return true;
  }
  pinkAppCapacity = 16;
  pinkApps =
      static_cast<PinkExecutableEntry *>(malloc(pinkAppCapacity *
                                               sizeof(PinkExecutableEntry)));
  if (pinkApps != nullptr) {
    for (size_t i = 0; i < pinkAppCapacity; i++) {
      pinkApps[i] = PinkExecutableEntry{};
    }
  } else {
    pinkAppCapacity = 0;
  }
  return pinkApps != nullptr;
}

bool growPinkEntries() {
  if (pinkAppCount < pinkAppCapacity) {
    return true;
  }
  if (pinkAppCapacity >= PINK_EXECUTABLE_MAX_APPS) {
    return false;
  }
  size_t nextCapacity = pinkAppCapacity * 2;
  if (nextCapacity > PINK_EXECUTABLE_MAX_APPS) {
    nextCapacity = PINK_EXECUTABLE_MAX_APPS;
  }
  PinkExecutableEntry *next = static_cast<PinkExecutableEntry *>(
      realloc(pinkApps, nextCapacity * sizeof(PinkExecutableEntry)));
  if (next == nullptr) {
    return false;
  }
  pinkApps = next;
  for (size_t i = pinkAppCapacity; i < nextCapacity; i++) {
    pinkApps[i] = PinkExecutableEntry{};
  }
  pinkAppCapacity = nextCapacity;
  return true;
}

char *duplicateText(const char *text) {
  const char *source = text != nullptr ? text : "";
  const size_t length = strlen(source);
  char *copy = static_cast<char *>(malloc(length + 1));
  if (copy == nullptr) {
    return nullptr;
  }
  memcpy(copy, source, length + 1);
  return copy;
}

bool scanPinkEntry(const FileProviderEntry &entry, void *context) {
  PinkScanContext *scan = static_cast<PinkScanContext *>(context);
  if (scan == nullptr || scan->next >= PINK_EXECUTABLE_MAX_APPS ||
      !growPinkEntries()) {
    return false;
  }
  if (entry.directory || !extensionIsPink(entry.name)) {
    return true;
  }

  char path[kPinkPathCapacity];
  char label[kPinkLabelCapacity];
  char icon[kPinkIconCapacity];
  bool iconFont = false;
  char id[kPinkIdCapacity];
  if (!joinPath(path, sizeof(path), scan->directory, entry.name)) {
    return true;
  }

  copyLabel(label, sizeof(label), entry.name);
  copyIcon(icon, sizeof(icon), label);
  readAppMetadata(path, label, sizeof(label), icon, sizeof(icon), iconFont);
  snprintf(id, sizeof(id), "pink:%08lx",
           static_cast<unsigned long>(hashPath(path)));

  PinkExecutableEntry &app = pinkApps[scan->next];
  app.id = duplicateText(id);
  app.path = duplicateText(path);
  app.label = duplicateText(label);
  app.icon = duplicateText(icon);
  app.iconFont = iconFont;
  if (app.id == nullptr || app.path == nullptr || app.label == nullptr ||
      app.icon == nullptr) {
    free(app.id);
    free(app.path);
    free(app.label);
    free(app.icon);
    app = PinkExecutableEntry{};
    return false;
  }
  app.category = scan->category;
  readPeriodicSchedule(path, app.periodic, app.packedPeriodicSchedule);
  scan->next++;
  pinkAppCount = scan->next;
  return true;
}

void scanPinkDirectory(const char *path, MenuCategory category) {
  const FileProvider *provider = defaultFileProvider();
  if (provider == nullptr || !sdStorageMounted()) {
    return;
  }

  PinkScanContext context;
  context.next = pinkAppCount;
  context.directory = path;
  context.category = category;
  listProviderDirectory(provider, path, scanPinkEntry, &context);
}

void drawMessage(Adafruit_GFX &gfx, const char *title, const char *detail) {
  gfx.setTextColor(1);
  gfx.setTextSize(2);
  gfx.setCursor(34, 72);
  gfx.print(title);
  gfx.setTextSize(1);
  gfx.setCursor(18, 104);
  gfx.print(detail);
}

uint8_t *allocateExecutableImage(size_t imageSize, bool &byteWritable) {
  const size_t allocatedSize = align4(imageSize);
  byteWritable = true;
  uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(
      allocatedSize, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (buffer != nullptr) {
    return buffer;
  }

  byteWritable = false;
  buffer = static_cast<uint8_t *>(heap_caps_malloc(
      allocatedSize, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT));
  if (buffer != nullptr) {
    return buffer;
  }

  return static_cast<uint8_t *>(
      heap_caps_malloc(allocatedSize, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL));
}

uint8_t *allocateExecutableRodata(size_t size, bool inPsram) {
  if (inPsram) {
    return static_cast<uint8_t *>(psram::allocate(size));
  }
  return static_cast<uint8_t *>(
      heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

bool readExecutableImage(File &file, uint8_t *dest, size_t imageSize,
                         bool byteWritable) {
  if (byteWritable) {
    return file.read(dest, imageSize) == imageSize;
  }

  uint32_t *words = reinterpret_cast<uint32_t *>(dest);
  size_t written = 0;
  while (written < imageSize) {
    uint8_t bytes[4] = {};
    const size_t remaining = imageSize - written;
    const size_t chunk = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
    if (file.read(bytes, chunk) != chunk) {
      return false;
    }
    words[written / sizeof(uint32_t)] =
        static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    written += chunk;
  }
  return true;
}

bool readU32(File &file, uint32_t &value) {
  uint8_t bytes[4];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    return false;
  }
  value = static_cast<uint32_t>(bytes[0]) |
          (static_cast<uint32_t>(bytes[1]) << 8) |
          (static_cast<uint32_t>(bytes[2]) << 16) |
          (static_cast<uint32_t>(bytes[3]) << 24);
  return true;
}

bool relocateWord(uint32_t &word, const PinkExecutableHeader &header,
                  uint8_t *code, uint8_t *rodata) {
  if (word < header.codeSize) {
    word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(code) + word);
    return true;
  }
  const size_t totalSize = static_cast<size_t>(header.codeSize) +
                           static_cast<size_t>(header.rodataSize);
  if (totalSize >= header.codeSize && word >= header.codeSize &&
      static_cast<size_t>(word) < totalSize && rodata != nullptr) {
    word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rodata) +
                                 (word - header.codeSize));
    return true;
  }
  return false;
}

bool applyExecutableRelocations(File &file, const PinkExecutableHeader &header,
                                uint8_t *code, uint8_t *rodata) {
  if (header.relocationCount == 0) {
    return true;
  }
  if (!file.seek(header.headerSize)) {
    return false;
  }

  for (uint32_t i = 0; i < header.relocationCount; i++) {
    uint32_t location = 0;
    if (!readU32(file, location)) {
      return false;
    }
    const bool inRodata = (location & PINK_RELOCATION_RODATA) != 0;
    const uint32_t offset = location & PINK_RELOCATION_OFFSET_MASK;
    const uint32_t sectionSize = inRodata ? header.rodataSize : header.codeSize;
    if ((offset & 3U) != 0 || offset + sizeof(uint32_t) > sectionSize) {
      return false;
    }
    uint32_t *words =
        reinterpret_cast<uint32_t *>(inRodata ? rodata : code);
    if (words == nullptr ||
        !relocateWord(words[offset / sizeof(uint32_t)], header, code, rodata)) {
      return false;
    }
  }
  return true;
}

uint32_t hostMillis() { return millis(); }

void hostDelay(uint32_t ms) { delay(ms); }

void hostLog(const char *message) {
  if (message == nullptr || !debugLoggingEnabled()) {
    return;
  }
  // ROM output remains available while app code runs outside Arduino's
  // Serial buffering and is the same path used by AI transport diagnostics.
  esp_rom_printf("[pink] %s\n", message);
}

bool hostNetworkConnected() { return wifiIsConnected(); }

void hostKeepAwake() { inactivitySleepKeepAwake(); }

void hostDeepSleepPreventAcquire() {
  if (pinkSleepGuardDepth < 255U) {
    pinkSleepGuardDepth++;
    lockscreenGuardAcquire();
  }
}

void hostDeepSleepPreventRelease() {
  if (pinkSleepGuardDepth > 0U) {
    pinkSleepGuardDepth--;
    lockscreenGuardRelease();
  }
}

void releasePinkSleepGuards() {
  while (pinkSleepGuardDepth > 0U) {
    pinkSleepGuardDepth--;
    lockscreenGuardRelease();
  }
}

void hostSetFont(uint8_t font) {
  if (pinkDrawTarget == nullptr) {
    return;
  }
  switch (font) {
  case PINK_FONT_ICON_8:
    systemFontApply(*pinkDrawTarget, size_t{1});
    break;
  case PINK_FONT_ICON_12:
    systemFontApply(*pinkDrawTarget, size_t{2});
    break;
  default:
    systemFontApply(*pinkDrawTarget, size_t{0});
    break;
  }
}

void hostSetTextSize(uint8_t size) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->setTextSize(size);
  }
}

void hostSetTextColor(uint16_t color) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->setTextColor(color);
  }
}

void hostSetCursor(int16_t x, int16_t y) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->setCursor(x, y);
  }
}

void hostPrint(const char *text) {
  if (pinkDrawTarget != nullptr && text != nullptr) {
    pinkDrawTarget->print(text);
  }
}

void hostDrawPixel(int16_t x, int16_t y, uint16_t color) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->drawPixel(x, y, color);
  }
}

void hostDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  uint16_t color) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->drawLine(x0, y0, x1, y1, color);
  }
}

void hostDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->drawRect(x, y, w, h, color);
  }
}

void hostFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->fillRect(x, y, w, h, color);
  }
}

void hostFillScreen(uint16_t color) {
  if (pinkDrawTarget != nullptr) {
    pinkDrawTarget->fillScreen(color);
  }
}

void *hostPsramAlloc(size_t bytes) {
  return bytes > 0 ? psram::allocate(bytes) : nullptr;
}

void hostPsramFree(void *ptr) { psram::release(ptr); }

bool hostConfigGet(const char *key, char *out, size_t outSize) {
  return configGetForApp(activeAppId, key, out, outSize);
}

bool hostConfigSet(const char *key, const char *value) {
  return configSetForApp(activeAppId, key, value);
}

int8_t apiStatus(AppApiStatus status) {
  return static_cast<int8_t>(status);
}

ImageTransform imageTransform(const PinkImageTransform *transform) {
  ImageTransform out;
  if (transform != nullptr) {
    out.a = transform->a;
    out.b = transform->b;
    out.c = transform->c;
    out.d = transform->d;
    out.tx = transform->tx;
    out.ty = transform->ty;
  }
  return out;
}

int8_t hostDrawImagePath(const char *path, int16_t x, int16_t y, int16_t w,
                         int16_t h, uint8_t dither, bool scaleToFit,
                         const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr) {
    return PINK_API_ERROR;
  }
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, nullptr) ==
      AppApiStatus::Error) {
    return PINK_API_ERROR;
  }
  return drawImagePathTransformed(*pinkDrawTarget, "sd", path, x, y, w, h,
                                  dither, scaleToFit,
                                  imageTransform(transform))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

int8_t hostDrawImageBuffer(const uint8_t *data, size_t size,
                           const char *format, int16_t x, int16_t y, int16_t w,
                           int16_t h, uint8_t dither, bool scaleToFit,
                           const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr) {
    return PINK_API_ERROR;
  }
  return drawImageBufferTransformed(*pinkDrawTarget, data, size, format, x, y,
                                    w, h, dither, scaleToFit,
                                    imageTransform(transform))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

ImageRenderCachePool *pinkImageCache(uint8_t cacheEntries) {
  return cacheEntries != 0U && pinkImageRenderCache.setCapacity(cacheEntries)
             ? &pinkImageRenderCache
             : nullptr;
}

int8_t hostDrawImagePathCached(const char *path, int16_t x, int16_t y,
                               int16_t w, int16_t h, uint8_t dither,
                               bool scaleToFit, uint8_t cacheEntries,
                               const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr) {
    return PINK_API_ERROR;
  }
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, nullptr) ==
      AppApiStatus::Error) {
    return PINK_API_ERROR;
  }
  return drawImagePathTransformed(
             *pinkDrawTarget, "sd", path, x, y, w, h, dither, scaleToFit,
             imageTransform(transform), pinkImageCache(cacheEntries))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

int8_t hostDrawImageBufferCached(const uint8_t *data, size_t size,
                                 const char *format, int16_t x, int16_t y,
                                 int16_t w, int16_t h, uint8_t dither,
                                 bool scaleToFit, uint8_t cacheEntries,
                                 const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr) {
    return PINK_API_ERROR;
  }
  return drawImageBufferTransformed(
             *pinkDrawTarget, data, size, format, x, y, w, h, dither,
             scaleToFit, imageTransform(transform),
             pinkImageCache(cacheEntries))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

ImageSourceRect imageSourceRect(const PinkSpriteFrame &frame) {
  ImageSourceRect source;
  source.x = frame.x;
  source.y = frame.y;
  source.width = frame.width;
  source.height = frame.height;
  return source;
}

int8_t hostDrawSpritePath(const char *path, const PinkSpriteFrame *frame,
                          int16_t x, int16_t y, uint8_t dither,
                          uint8_t cacheEntries,
                          const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr || frame == nullptr || !frame->valid()) {
    return PINK_API_ERROR;
  }
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, nullptr) ==
      AppApiStatus::Error) {
    return PINK_API_ERROR;
  }
  return drawImagePathRegionTransformed(
             *pinkDrawTarget, "sd", path, imageSourceRect(*frame), x, y,
             dither, imageTransform(transform), pinkImageCache(cacheEntries))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

int8_t hostDrawSpriteBuffer(const uint8_t *data, size_t size,
                            const char *format,
                            const PinkSpriteFrame *frame, int16_t x,
                            int16_t y, uint8_t dither, uint8_t cacheEntries,
                            const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr || frame == nullptr || !frame->valid()) {
    return PINK_API_ERROR;
  }
  return drawImageBufferRegionTransformed(
             *pinkDrawTarget, data, size, format, imageSourceRect(*frame), x,
             y, dither, imageTransform(transform),
             pinkImageCache(cacheEntries))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

int8_t hostDrawVectorPath(const char *path, int16_t x, int16_t y, int16_t w,
                          int16_t h, const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr || path == nullptr) {
    return PINK_API_ERROR;
  }
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, nullptr) ==
      AppApiStatus::Error) {
    return PINK_API_ERROR;
  }
  return drawVectorPathTransformed(*pinkDrawTarget, "sd", path, x, y, w, h,
                                   imageTransform(transform))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

int8_t hostDrawVectorString(const char *svg, size_t size, int16_t x,
                            int16_t y, int16_t w, int16_t h,
                            const PinkImageTransform *transform) {
  if (pinkDrawTarget == nullptr || svg == nullptr) {
    return PINK_API_ERROR;
  }
  return drawVectorStringTransformed(*pinkDrawTarget, svg, size, x, y, w, h,
                                     imageTransform(transform))
             ? PINK_API_OK
             : PINK_API_ERROR;
}

int8_t hostFileRead(const char *path, size_t offset, uint8_t *out,
                    size_t capacity, size_t *outRead, size_t *outFileSize) {
  return apiStatus(
      appApiReadFile(path, offset, out, capacity, outRead, outFileSize));
}

int8_t hostFileList(const char *path, PinkDirectoryEntryHandler handler,
                    void *context) {
  return apiStatus(appApiListDirectory(path, handler, context));
}

int8_t hostFileWrite(const char *path, const uint8_t *data, size_t size,
                     bool append) {
  return apiStatus(appApiWriteFile(path, data, size, append));
}

int8_t hostFileDelete(const char *path) {
  return apiStatus(appApiDeleteFile(path));
}

int8_t hostFilePicker(const char *initialPath, const char *extensionFilter,
                      char *outPath, size_t outSize) {
  const AppApiStatus status =
      appApiRequestFilePicker(initialPath, extensionFilter);
  if (status == AppApiStatus::Ok || status == AppApiStatus::Denied) {
    return apiStatus(appApiTakePickedFile(outPath, outSize));
  }
  return apiStatus(status);
}

bool hostOpenApp(const char *appId) { return appApiRequestOpenApp(appId); }

int8_t hostGpioConfigure(uint8_t pin, uint8_t mode) {
  if (mode > static_cast<uint8_t>(AppApiGpioMode::Output)) {
    return PINK_API_ERROR;
  }
  return apiStatus(appApiGpioConfigure(pin, static_cast<AppApiGpioMode>(mode)));
}

int8_t hostGpioRead(uint8_t pin, int *value) {
  return apiStatus(appApiGpioRead(pin, value));
}

int8_t hostGpioWrite(uint8_t pin, bool high) {
  return apiStatus(appApiGpioWrite(pin, high));
}

bool hostSensorSnapshot(PinkSensorSnapshot *out, bool refresh) {
  if (out == nullptr) {
    return false;
  }
  const EnvironmentSnapshot snapshot = appApiSensorSnapshot(refresh);
  out->temperatureValid = snapshot.ambientTemperatureValid;
  out->humidityValid = snapshot.ambientHumidityValid;
  out->cpuTemperatureValid = snapshot.chipTemperatureValid;
  out->reserved = 0;
  out->temperatureC = snapshot.ambientTemperatureC;
  out->humidityPct = snapshot.ambientHumidityPct;
  out->cpuTemperatureC = snapshot.chipTemperatureC;
  return true;
}

int8_t hostWifiScan() { return apiStatus(appApiWifiScan()); }
size_t hostWifiNetworkCount() { return appApiWifiNetworkCount(); }

bool hostWifiNetworkAt(size_t index, PinkWifiNetwork *out) {
  if (out == nullptr) {
    return false;
  }
  AppApiWifiNetwork network;
  if (!appApiWifiNetworkAt(index, network)) {
    return false;
  }
  copyText(out->ssid, sizeof(out->ssid), network.ssid);
  out->rssi = network.rssi;
  return true;
}

size_t hostFontCount() { return systemFontCount(); }

bool hostFontInfo(size_t index, char *name, size_t nameSize,
                  uint8_t *nominalSize) {
  SystemFontInfo info;
  if (!systemFontInfo(index, info) || !copyText(name, nameSize, info.name)) {
    return false;
  }
  if (nominalSize != nullptr) {
    *nominalSize = info.nominalSize;
  }
  return true;
}

bool hostSetFontByName(const char *name) {
  return pinkDrawTarget != nullptr && systemFontApply(*pinkDrawTarget, name);
}

int8_t hostCaptureWav(uint32_t durationMs, const uint8_t **data, size_t *size) {
  return apiStatus(appApiCaptureWav(durationMs, data, size));
}

int8_t hostCapturePcm16(uint32_t durationMs, const uint8_t **data,
                        size_t *size) {
  return apiStatus(appApiCapturePcm16(durationMs, data, size));
}

int32_t hostAudioLoadPath(const char *path) {
  return appApiAudioLoadPath(path);
}
bool hostAudioFormatSupported(const char *format) {
  return appApiAudioFormatSupported(format);
}
int32_t hostAudioLoadBuffer(const uint8_t *data, size_t size,
                            const char *format) {
  return appApiAudioLoadBuffer(data, size, format);
}
int8_t hostAudioPlay(uint32_t handle) {
  return apiStatus(appApiAudioPlay(handle));
}
int8_t hostAudioStop(uint32_t handle) {
  return apiStatus(appApiAudioStop(handle));
}
int8_t hostAudioUpdate(uint32_t handle) {
  return apiStatus(appApiAudioUpdate(handle));
}
void hostAudioRelease(uint32_t handle) { appApiAudioRelease(handle); }

int8_t hostGetDateInput(int initialYear, int initialMonth, int initialDay,
                        int *year, int *month, int *day) {
  if (year == nullptr || month == nullptr || day == nullptr) {
    return PINK_API_ERROR;
  }
  DatePickerDate selected;
  const AppApiStatus status = appApiGetDateInput(
      {initialYear, initialMonth, initialDay}, selected);
  if (status == AppApiStatus::Ok) {
    *year = selected.year;
    *month = selected.month;
    *day = selected.day;
  }
  return apiStatus(status);
}

bool hostDrawQr(const char *text, int16_t x, int16_t y, int16_t size,
                uint16_t color) {
  if (pinkDrawTarget == nullptr) {
    return false;
  }
  SystemQrCode qr;
  if (!qr.encode(text)) {
    return false;
  }
  qr.draw(*pinkDrawTarget, x, y, size, color);
  return true;
}

int8_t hostGetKeyboardInput(const char *initialText, uint8_t layout,
                            size_t maxLength, char *out, size_t outSize) {
  return apiStatus(appApiGetKeyboardInput(initialText, layout, maxLength, out,
                                          outSize));
}

int8_t agentStatus(agentfiesta::Status status) {
  return static_cast<int8_t>(status);
}

bool validAgentConfig(const PinkAgentFiestaConfig *config) {
  return config != nullptr &&
         config->structSize >= sizeof(PinkAgentFiestaConfig) &&
         config->apiVersion == PINK_AGENTFIESTA_API_VERSION &&
         (config->options & ~(PINK_AGENTFIESTA_TRANSCRIPTION |
                              PINK_AGENTFIESTA_VOICE_OUTPUT)) == 0U;
}

agentfiesta::SessionConfig nativeAgentConfig(
    const PinkAgentFiestaConfig &config) {
  agentfiesta::SessionConfig native;
  native.toolPacks = config.toolPacks;
  native.systemPromptPath = config.systemPromptPath;
  native.appPromptPath = config.appPromptPath;
  native.historyLimit = config.historyLimit;
  native.modelIndex = config.modelIndex;
  native.approvalEnabled = config.approvalEnabled != 0;
  native.transcriptionMode =
      (config.options & PINK_AGENTFIESTA_TRANSCRIPTION) != 0U;
  native.voiceOutput =
      (config.options & PINK_AGENTFIESTA_VOICE_OUTPUT) != 0U;
  return native;
}

int8_t hostAgentSessionStart(const PinkAgentFiestaConfig *config) {
  return validAgentConfig(config)
             ? agentStatus(agentfiesta::start(activeAppId, activeAppPath,
                                              nativeAgentConfig(*config)))
             : static_cast<int8_t>(PINK_AGENTFIESTA_ERROR);
}

void hostAgentSessionStop() { agentfiesta::stop(activeAppId); }

int8_t hostAgentSessionConfigure(const PinkAgentFiestaConfig *config) {
  return validAgentConfig(config)
             ? agentStatus(
                   agentfiesta::configure(nativeAgentConfig(*config)))
             : static_cast<int8_t>(PINK_AGENTFIESTA_ERROR);
}

int8_t hostAgentSubmitText(const char *text) {
  return agentStatus(agentfiesta::submitText(text));
}

int8_t hostAgentSubmitWav(const uint8_t *wav, size_t wavBytes,
                          const char *optionalText) {
  return agentStatus(agentfiesta::submitWav(wav, wavBytes, optionalText));
}

int8_t hostAgentUpdate() { return agentStatus(agentfiesta::update()); }

int8_t hostAgentPollEvent(PinkAgentFiestaEvent *event, char *text,
                          size_t textSize) {
  if (event == nullptr || event->structSize < sizeof(PinkAgentFiestaEvent)) {
    return PINK_AGENTFIESTA_ERROR;
  }
  agentfiesta::Event native;
  const agentfiesta::Status status =
      agentfiesta::pollEvent(native, text, textSize);
  if (status == agentfiesta::Status::Ok) {
    event->type = static_cast<uint8_t>(native.type);
    event->entryType = static_cast<uint8_t>(native.entryType);
    event->status = static_cast<int8_t>(native.status);
    event->id = native.id;
  }
  return agentStatus(status);
}

void hostAgentCancel() { agentfiesta::cancel(); }

int8_t hostAgentToolResult(uint32_t callId, const char *result, bool error) {
  return agentStatus(agentfiesta::provideToolResult(callId, result, error));
}

void hostAgentDrawFeed(int16_t x, int16_t y, int16_t width, int16_t height) {
  if (pinkDrawTarget != nullptr) {
    agentfiesta::drawFeed(*pinkDrawTarget, x, y, width, height);
  }
}

void hostAgentScrollFeed(int8_t wrappedLines) {
  agentfiesta::scrollFeed(wrappedLines);
}

uint8_t hostAgentHistoryLimit() { return agentfiesta::historyLimit(); }

int8_t hostAgentSetHistoryLimit(uint8_t entries) {
  return agentStatus(agentfiesta::setHistoryLimit(entries));
}

bool hostAgentApprovalEnabled() { return agentfiesta::approvalEnabled(); }

void hostAgentSetApprovalEnabled(bool enabled) {
  agentfiesta::setApprovalEnabled(enabled);
}

size_t hostAgentSnapshotSize() { return agentfiesta::snapshotSize(); }

int8_t hostAgentSnapshotSave(uint8_t *out, size_t outSize, size_t *written) {
  if (written == nullptr) {
    return PINK_AGENTFIESTA_ERROR;
  }
  return agentStatus(agentfiesta::saveSnapshot(out, outSize, *written));
}

int8_t hostAgentSnapshotRestore(const uint8_t *data, size_t size) {
  return agentStatus(agentfiesta::restoreSnapshot(data, size));
}

int8_t hostAgentHandoff(const char *targetAppPath, const char *prompt) {
  return agentStatus(agentfiesta::handoff(targetAppPath, prompt));
}

int8_t hostAgentRegisterTool(const char *name, const char *description,
                             const char *parametersJson) {
  return agentStatus(
      agentfiesta::registerTool(name, description, parametersJson));
}

int8_t hostAgentPendingToolName(uint32_t callId, char *out, size_t outSize) {
  return agentStatus(agentfiesta::pendingToolName(callId, out, outSize));
}

int8_t hostAuthorizeFile(const char *path, uint8_t operations) {
  constexpr uint8_t kAllowed =
      PINK_AUTHORIZE_FILE_WRITE | PINK_AUTHORIZE_FILE_DELETE;
  if (operations == 0U || (operations & ~kAllowed) != 0U) {
    return PINK_API_ERROR;
  }

  if (path == nullptr) {
    if (pinkCurrentEventType != PINK_EVENT_TOUCH &&
        pinkCurrentEventType != PINK_EVENT_TOUCH_DOWN) {
      return PINK_API_DENIED;
    }
    pinkFileAuthorizationIntent = operations;
    return PINK_API_OK;
  }
  if (path[0] != '/') {
    return PINK_API_ERROR;
  }

  char workspace[288] = "/misc/agentfiesta/";
  size_t workspaceLength = strlen(workspace);
  const char *name = strrchr(activeAppPath, '/');
  name = name != nullptr ? name + 1U : activeAppPath;
  const char *extension = strrchr(name, '.');
  const size_t nameLength = extension != nullptr
                                ? static_cast<size_t>(extension - name)
                                : strlen(name);
  bool safeName = nameLength > 0U &&
                  workspaceLength + nameLength + 1U < sizeof(workspace);
  for (size_t i = 0; safeName && i < nameLength; i++) {
    const char value = name[i];
    safeName = (value >= 'a' && value <= 'z') ||
               (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9') || value == '_' || value == '-';
  }
  if (safeName) {
    memcpy(workspace + workspaceLength, name, nameLength);
    workspaceLength += nameLength;
    workspace[workspaceLength++] = '/';
    workspace[workspaceLength] = '\0';
  }
  const bool ownedWorkspace =
      safeName && strncmp(path, workspace, workspaceLength) == 0 &&
      path[workspaceLength] != '\0';

  bool explicitExport = false;
  if (!ownedWorkspace &&
      (pinkFileAuthorizationIntent & operations) == operations &&
      strncmp(path, "/bin/js/", 8U) == 0) {
    const char *fileName = path + 8U;
    const char *dot = strrchr(fileName, '.');
    explicitExport = fileName[0] != '\0' && dot != nullptr &&
                     strcmp(dot, ".js") == 0;
    for (const char *at = fileName; explicitExport && at < dot; at++) {
      const char value = *at;
      explicitExport = (value >= 'a' && value <= 'z') ||
                       (value >= '0' && value <= '9') || value == '-';
    }
    // An outside-workspace attempt consumes the one-shot gesture regardless
    // of whether its destination passes validation.
    pinkFileAuthorizationIntent = 0;
  }
  if (!ownedWorkspace && !explicitExport) {
    return PINK_API_DENIED;
  }
  if ((operations & PINK_AUTHORIZE_FILE_WRITE) != 0U &&
      !appApiAuthorizeCapability(AppApiAgentCapability::FileWrite, path)) {
    return PINK_API_ERROR;
  }
  if ((operations & PINK_AUTHORIZE_FILE_DELETE) != 0U &&
      !appApiAuthorizeCapability(AppApiAgentCapability::FileDelete, path)) {
    return PINK_API_ERROR;
  }
  return PINK_API_OK;
}

int8_t hostLaunchPocketInkJsReturnable(const char *sourcePath,
                                       uint8_t *ownedState,
                                       size_t stateSize) {
  return returnablePocketInkJsQueue(activeAppPath, sourcePath, ownedState,
                                    stateSize)
             ? PINK_API_OK
             : PINK_API_ERROR;
}

const PinkAgentFiestaApi pinkAgentFiestaApi = {
    PINK_AGENTFIESTA_API_VERSION,
    0,
    sizeof(PinkAgentFiestaApi),
    PINK_AGENTFIESTA_MAX_VOICE_MS,
    hostAgentSessionStart,
    hostAgentSessionStop,
    hostAgentSessionConfigure,
    hostAgentSubmitText,
    hostAgentSubmitWav,
    hostAgentUpdate,
    hostAgentPollEvent,
    hostAgentCancel,
    hostAgentToolResult,
    hostAgentDrawFeed,
    hostAgentScrollFeed,
    hostAgentHistoryLimit,
    hostAgentSetHistoryLimit,
    hostAgentApprovalEnabled,
    hostAgentSetApprovalEnabled,
    hostAgentSnapshotSize,
    hostAgentSnapshotSave,
    hostAgentSnapshotRestore,
    hostAgentHandoff,
    hostAgentRegisterTool,
    hostAgentPendingToolName,
};

uint32_t pinkFeatureFlags() {
  uint32_t flags = PINK_FEATURE_READ_FILES | PINK_FEATURE_WRITE_FILES |
                   PINK_FEATURE_DELETE_FILES | PINK_FEATURE_AUDIO_CAPTURE |
                   PINK_FEATURE_GPIO | PINK_FEATURE_SENSORS |
                   PINK_FEATURE_RETURNABLE_JS;
#if ENABLE_NETWORK_APPS
  flags |= PINK_FEATURE_WIFI_SCAN | PINK_FEATURE_AGENTFIESTA;
#endif
  return flags;
}

PinkHost makeHost() {
  PinkHost host = {};
  host.abiVersion = PINK_EXECUTABLE_ABI_VERSION;
  host.screenWidth = EPD_WIDTH;
  host.screenHeight = EPD_HEIGHT;
  host.reserved = 0;
  host.appId = activeAppId;
  host.appPath = activeAppPath;
  host.memory = activeMemory;
  host.memorySize = activeMemorySize;
  host.millis = hostMillis;
  host.delayMs = hostDelay;
  host.log = hostLog;
  host.networkConnected = hostNetworkConnected;
  host.keepAwake = hostKeepAwake;
  host.deepSleepPreventAcquire = hostDeepSleepPreventAcquire;
  host.deepSleepPreventRelease = hostDeepSleepPreventRelease;
  host.setFont = hostSetFont;
  host.setTextSize = hostSetTextSize;
  host.setTextColor = hostSetTextColor;
  host.setCursor = hostSetCursor;
  host.print = hostPrint;
  host.drawPixel = hostDrawPixel;
  host.drawLine = hostDrawLine;
  host.drawRect = hostDrawRect;
  host.fillRect = hostFillRect;
  host.fillScreen = hostFillScreen;
  host.psramAlloc = hostPsramAlloc;
  host.psramFree = hostPsramFree;
  host.configGet = hostConfigGet;
  host.configSet = hostConfigSet;
  host.hostSize = sizeof(PinkHost);
  host.featureFlags = pinkFeatureFlags();
  host.reserved2 = 0;
  host.drawImagePath = hostDrawImagePath;
  host.drawImageBuffer = hostDrawImageBuffer;
  host.fileRead = hostFileRead;
  host.fileWrite = hostFileWrite;
  host.filePicker = hostFilePicker;
  host.openApp = hostOpenApp;
  host.gpioConfigure = hostGpioConfigure;
  host.gpioRead = hostGpioRead;
  host.gpioWrite = hostGpioWrite;
  host.sensorSnapshot = hostSensorSnapshot;
  host.wifiScan = hostWifiScan;
  host.wifiNetworkCount = hostWifiNetworkCount;
  host.wifiNetworkAt = hostWifiNetworkAt;
  host.fontCount = hostFontCount;
  host.fontInfo = hostFontInfo;
  host.setFontByName = hostSetFontByName;
  host.audioLoadPath = hostAudioLoadPath;
  host.audioFormatSupported = hostAudioFormatSupported;
  host.audioLoadBuffer = hostAudioLoadBuffer;
  host.audioPlay = hostAudioPlay;
  host.audioStop = hostAudioStop;
  host.audioUpdate = hostAudioUpdate;
  host.audioRelease = hostAudioRelease;
  host.drawQr = hostDrawQr;
  host.fileDelete = hostFileDelete;
  host.audioCaptureWav = hostCaptureWav;
  host.getDateInput = hostGetDateInput;
  host.getKeyboardInput = hostGetKeyboardInput;
  host.audioCapturePcm16 = hostCapturePcm16;
  host.drawVectorPath = hostDrawVectorPath;
  host.drawVectorString = hostDrawVectorString;
  host.drawImagePathCached = hostDrawImagePathCached;
  host.drawImageBufferCached = hostDrawImageBufferCached;
  host.agentFiesta = &pinkAgentFiestaApi;
  host.authorizeFile = hostAuthorizeFile;
  host.launchPocketInkJsReturnable = hostLaunchPocketInkJsReturnable;
  host.fileList = hostFileList;
  host.drawSpritePath = hostDrawSpritePath;
  host.drawSpriteBuffer = hostDrawSpriteBuffer;
  return host;
}

class PinkExecutableApp : public ActiveApp {
public:
  bool launch(const PinkExecutableEntry &entry,
              uint32_t expectedPeriodicSchedule = 0) {
    unload();
    copyText(path, sizeof(path), entry.path);
    copyText(appId, sizeof(appId), entry.id);
    copyText(activeAppPath, sizeof(activeAppPath), path);
    copyText(activeAppId, sizeof(activeAppId), appId);
    pinkFileAuthorizationIntent = 0;
    for (size_t i = 0; i < sizeof(memory); i++) {
      memory[i] = 0;
    }
    activeMemory = memory;
    activeMemorySize = sizeof(memory);
    appApiBeginSession(activeAppId);
    load();
    if (expectedPeriodicSchedule != 0 &&
        loadedPeriodicSchedule != expectedPeriodicSchedule) {
      unload();
      return false;
    }
    activePinkApp = this;
    sendSimpleEvent(PINK_EVENT_START);
    return this->entry != nullptr;
  }

  void stop() { unload(); }

  PeriodicTaskAction runPeriodic(const PinkExecutablePeriodicTask &task,
                                 int64_t localUnix,
                                 uint16_t effectiveIntervalMinutes,
                                 Adafruit_GFX *gfx, bool &dirty) {
    PinkExecutableEntry executable;
    executable.id = const_cast<char *>(task.id);
    executable.path = const_cast<char *>(task.path);
    executable.label = const_cast<char *>("periodic");
    executable.periodic = task.schedule;
    executable.packedPeriodicSchedule = task.packedSchedule;
    dirty = false;
    if (!launch(executable, task.packedSchedule)) {
      return PeriodicTaskAction::Complete;
    }

    PinkEvent event;
    event.type = PINK_EVENT_PERIODIC;
    event.periodicLocalUnix = localUnix;
    event.periodicIntervalMinutes = effectiveIntervalMinutes;
    event.periodicWakeLevel = static_cast<uint8_t>(task.schedule.wakeLevel);
    event.periodicDisplayMode =
        static_cast<uint8_t>(task.schedule.displayMode);
    event.periodicAction =
        static_cast<uint8_t>(PeriodicTaskAction::Complete);
    callEvent(event, gfx);
    dirty = event.dirty != 0;

    PeriodicTaskAction action = PeriodicTaskAction::Complete;
    if (event.periodicAction ==
        static_cast<uint8_t>(PeriodicTaskAction::WakeDevice)) {
      action = PeriodicTaskAction::WakeDevice;
    } else if (event.periodicAction ==
               static_cast<uint8_t>(PeriodicTaskAction::WakeApp)) {
      action = PeriodicTaskAction::WakeApp;
    }
    stop();
    return action;
  }

  void draw(Adafruit_GFX &gfx) override {
    if (status[0] != '\0') {
      drawMessage(gfx, "PINK", status);
      return;
    }
    call(PINK_EVENT_DRAW, nullptr, &gfx);
  }

  bool handleTouch(const TouchPoint &point) override {
    PinkEvent event;
    event.type = PINK_EVENT_TOUCH;
    event.x = point.x;
    event.y = point.y;
    callEvent(event, nullptr);
    return event.dirty != 0 || event.handled != 0;
  }

  bool update() override {
    PinkEvent event;
    event.type = PINK_EVENT_UPDATE;
    callEvent(event, nullptr);
    const bool dirty = pendingDirty || event.dirty != 0;
    pendingDirty = false;
    return dirty;
  }

  bool hasActiveSession() const override { return false; }

  bool loaded() const { return entry != nullptr; }

  AppEventResult handleButton(uint8_t type) {
    PinkEvent event;
    event.type = type;
    callEvent(event, nullptr);
    if (event.dirty != 0) {
      return AppEventResult::Dirty;
    }
    if (event.handled != 0) {
      return AppEventResult::Handled;
    }
    return AppEventResult::Unhandled;
  }

  void handleRawTouch(const TouchEvent &touch) {
    PinkEvent event;
    event.type = eventTypeForTouch(touch.type);
    event.x = touch.point.x;
    event.y = touch.point.y;
    callEvent(event, nullptr);
    pendingDirty = pendingDirty || event.dirty != 0;
  }

  size_t saveContext(uint8_t *buffer, size_t capacity) {
    PinkEvent event;
    event.type = PINK_EVENT_SAVE_STATE;
    event.data = buffer;
    event.dataCapacity = capacity;
    callEvent(event, nullptr);
    return event.dataLength <= capacity ? event.dataLength : capacity;
  }

  void restoreContext(const uint8_t *buffer, size_t length) {
    PinkEvent event;
    event.type = PINK_EVENT_RESTORE_STATE;
    event.data = const_cast<uint8_t *>(buffer);
    event.dataLength = length;
    event.dataCapacity = length;
    callEvent(event, nullptr);
    pendingDirty = pendingDirty || event.dirty != 0;
  }

private:
  char path[kPinkPathCapacity] = {};
  char appId[kPinkIdCapacity] = {};
  char status[32] = {};
  PinkExecutableImage image;
  uint32_t loadedPeriodicSchedule = 0;
  PinkEntryPoint entry = nullptr;
  bool pendingDirty = false;
  uint8_t memory[kPinkMemorySize] = {};

  uint8_t eventTypeForTouch(TouchEventType type) const {
    switch (type) {
    case TOUCH_EVENT_DOWN:
      return PINK_EVENT_TOUCH_DOWN;
    case TOUCH_EVENT_MOVE:
      return PINK_EVENT_TOUCH_MOVE;
    case TOUCH_EVENT_UP:
      return PINK_EVENT_TOUCH_UP;
    }
    return PINK_EVENT_TOUCH;
  }

  void unload() {
    if (entry != nullptr) {
      sendSimpleEvent(PINK_EVENT_STOP);
    }
    releasePinkSleepGuards();
    pinkImageRenderCache.clear();
    agentfiesta::stop(activeAppId);
    if (appApiSessionMatches(activeAppId)) {
      appApiEndSession();
    }
    pinkExecutableUnloadImage(image);
    loadedPeriodicSchedule = 0;
    entry = nullptr;
    status[0] = '\0';
    activeAppId[0] = '\0';
    activeAppPath[0] = '\0';
    activeMemory = nullptr;
    activeMemorySize = 0;
    pinkFileAuthorizationIntent = 0;
    if (activePinkApp == this) {
      activePinkApp = nullptr;
    }
    pendingDirty = false;
  }

  void load() {
    File file = openProviderPath("sd", path);
    if (!file) {
      copyText(status, sizeof(status), "OPEN FAILED");
      return;
    }

    PinkExecutableHeader header;
    if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
        sizeof(header)) {
      copyText(status, sizeof(status), "BAD HEADER");
      return;
    }
    if (header.headerSize >= sizeof(PinkExecutableHeader) + sizeof(uint32_t)) {
      uint32_t extensionMagic = 0;
      if (!readU32(file, extensionMagic)) {
        copyText(status, sizeof(status), "BAD HEADER");
        return;
      }
      if (extensionMagic == PINK_LIBRARY_MAGIC) {
        copyText(status, sizeof(status), "IS LIBRARY");
        return;
      }
    }
    const PinkExecutableLoadPolicy policy = {
        PINK_EXECUTABLE_MAX_IMAGE_BYTES,
        PINK_EXECUTABLE_MAX_IMAGE_BYTES,
        PINK_EXECUTABLE_MAX_IMAGE_BYTES,
        kPinkMaxRelocations,
        false,
    };
    const PinkExecutableLoadResult result =
        pinkExecutableLoadImage(file, header, policy, image);
    switch (result) {
    case PinkExecutableLoadResult::Loaded:
      break;
    case PinkExecutableLoadResult::InvalidExecutable:
      copyText(status, sizeof(status), "BAD EXECUTABLE");
      return;
    case PinkExecutableLoadResult::InvalidRelocations:
      copyText(status, sizeof(status), "BAD RELOCS");
      return;
    case PinkExecutableLoadResult::SeekFailed:
      copyText(status, sizeof(status), "SEEK FAILED");
      return;
    case PinkExecutableLoadResult::NoExecutableMemory:
      copyText(status, sizeof(status), "NO EXEC MEM");
      return;
    case PinkExecutableLoadResult::NoDataMemory:
      copyText(status, sizeof(status), "NO DATA MEM");
      return;
    case PinkExecutableLoadResult::ReadFailed:
      copyText(status, sizeof(status), "READ FAILED");
      return;
    }

    loadedPeriodicSchedule = header.periodicSchedule;
    entry = reinterpret_cast<PinkEntryPoint>(image.entryPoint);
    if (debugLoggingEnabled()) {
      esp_rom_printf(
          "[pink] loaded %s code=%p codeSize=%lu rodata=%p rodataSize=%lu "
          "entry=%p relocs=%lu\n",
          path, image.code, static_cast<unsigned long>(header.codeSize),
          image.rodata, static_cast<unsigned long>(header.rodataSize),
          reinterpret_cast<void *>(entry),
          static_cast<unsigned long>(header.relocationCount));
    }
    status[0] = '\0';
  }

  void sendSimpleEvent(uint8_t type) {
    PinkEvent event;
    event.type = type;
    callEvent(event, nullptr);
  }

  void call(uint8_t type, PinkEvent *event, Adafruit_GFX *gfx) {
    PinkEvent local;
    PinkEvent &target = event != nullptr ? *event : local;
    target.type = type;
    callEvent(target, gfx);
  }

  void callEvent(PinkEvent &event, Adafruit_GFX *gfx) {
    if (entry == nullptr) {
      return;
    }
    if (debugLoggingEnabled() &&
        (event.type == PINK_EVENT_START || event.type == PINK_EVENT_DRAW ||
         event.type == PINK_EVENT_STOP)) {
      esp_rom_printf("[pink] event %u entry=%p\n",
                     static_cast<unsigned>(event.type),
                     reinterpret_cast<void *>(entry));
    }
    pinkDrawTarget = gfx;
    event.eventSize = sizeof(PinkEvent);
    event.featureFlags = pinkFeatureFlags();
    PinkHost host = makeHost();
    const uint8_t previousEventType = pinkCurrentEventType;
    pinkCurrentEventType = event.type;
    entry(&host, &event);
    pinkCurrentEventType = previousEventType;
    pinkDrawTarget = nullptr;
  }
};

PinkExecutableApp pinkApp;

AppEventResult handlePinkButton(uint8_t type) {
  if (activePinkApp == nullptr) {
    return AppEventResult::Unhandled;
  }
  return activePinkApp->handleButton(type);
}

AppEventResult handlePinkMenu() {
  return handlePinkButton(PINK_EVENT_MENU_BUTTON);
}

AppEventResult handlePinkMenuDouble() {
  return handlePinkButton(PINK_EVENT_MENU_DOUBLE);
}

AppEventResult handlePinkMenuLong() {
  return handlePinkButton(PINK_EVENT_MENU_LONG);
}

AppEventResult handlePinkPower() {
  return handlePinkButton(PINK_EVENT_POWER_BUTTON);
}

AppEventResult handlePinkPowerDouble() {
  return handlePinkButton(PINK_EVENT_POWER_DOUBLE);
}

AppEventResult handlePinkPowerLong() {
  return handlePinkButton(PINK_EVENT_POWER_LONG);
}

void handlePinkRawTouch(const TouchEvent &touch) {
  if (activePinkApp != nullptr) {
    activePinkApp->handleRawTouch(touch);
  }
}

size_t savePinkContext(uint8_t *buffer, size_t capacity) {
  if (activePinkApp == nullptr) {
    return 0;
  }
  return activePinkApp->saveContext(buffer, capacity);
}

void restorePinkContext(const uint8_t *buffer, size_t length) {
  if (activePinkApp != nullptr) {
    activePinkApp->restoreContext(buffer, length);
  }
}

AppBehavior pinkBehavior() {
  AppBehavior behavior;
  behavior.onMenu = handlePinkMenu;
  behavior.onMenuDouble = handlePinkMenuDouble;
  behavior.onMenuLong = handlePinkMenuLong;
  behavior.onPower = handlePinkPower;
  behavior.onPowerDouble = handlePinkPowerDouble;
  behavior.onPowerLong = handlePinkPowerLong;
  behavior.onRawTouch = handlePinkRawTouch;
  behavior.onSaveContext = savePinkContext;
  behavior.onRestoreContext = restorePinkContext;
  behavior.onExit = pinkExecutableStop;
  return behavior;
}

bool populatePinkDefinition(const PinkExecutableEntry &entry, AppDefinition *out,
                            char *id, size_t idSize, char *label,
                            size_t labelSize, char *icon, size_t iconSize) {
  if (out == nullptr || id == nullptr || label == nullptr || icon == nullptr) {
    return false;
  }
  copyText(id, idSize, entry.id);
  copyText(label, labelSize, entry.label);
  if (entry.icon != nullptr && entry.icon[0] != '\0') {
    copyText(icon, iconSize, entry.icon);
  } else {
    copyIcon(icon, iconSize, label);
  }
  *out = AppDefinition{};
  out->id = id;
  out->label = label;
  out->icon = icon;
  out->category = entry.category;
  out->runtime = pinkExecutableRuntime();
  out->visible = visiblePinkApp;
  out->iconFont = entry.iconFont;
  out->behavior = pinkBehavior();
  return true;
}

} // namespace

bool returnablePocketInkJsQueue(const char *ownerPath, const char *sourcePath,
                                uint8_t *ownedState, size_t stateSize) {
  if (pendingReturnableJs != nullptr || ownedState == nullptr ||
      stateSize == 0 || stateSize > kReturnableJsMaxStateBytes) {
    return false;
  }
  void *memory = psram::allocate(sizeof(ReturnableJsState));
  if (memory == nullptr) {
    return false;
  }
  ReturnableJsState *prepared = new (memory) ReturnableJsState();
  if (!copyReturnableJsPath(prepared->ownerPath, sizeof(prepared->ownerPath),
                            ownerPath) ||
      !copyReturnableJsPath(prepared->sourcePath,
                            sizeof(prepared->sourcePath), sourcePath)) {
    prepared->~ReturnableJsState();
    psram::release(prepared);
    return false;
  }
  prepared->state = ownedState;
  prepared->stateSize = stateSize;
  pendingReturnableJs = prepared;
  return true;
}

bool returnablePocketInkJsRequested() {
  return pendingReturnableJs != nullptr && !pendingReturnableJs->launched;
}

bool returnablePocketInkJsTakeSource(char *out, size_t outSize) {
  if (!returnablePocketInkJsRequested() || out == nullptr || outSize == 0) {
    return false;
  }
  const size_t length = strlen(pendingReturnableJs->sourcePath);
  if (length >= outSize) {
    return false;
  }
  memcpy(out, pendingReturnableJs->sourcePath, length + 1U);
  pendingReturnableJs->launched = true;
  return true;
}

bool returnablePocketInkJsReturnPending() {
  return pendingReturnableJs != nullptr && pendingReturnableJs->launched;
}

bool returnablePocketInkJsOwnerPath(char *out, size_t outSize) {
  if (pendingReturnableJs == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  const size_t length = strlen(pendingReturnableJs->ownerPath);
  if (length >= outSize) {
    return false;
  }
  memcpy(out, pendingReturnableJs->ownerPath, length + 1U);
  return true;
}

bool returnablePocketInkJsState(const uint8_t *&data, size_t &size) {
  data = nullptr;
  size = 0;
  if (pendingReturnableJs == nullptr || pendingReturnableJs->state == nullptr) {
    return false;
  }
  data = pendingReturnableJs->state;
  size = pendingReturnableJs->stateSize;
  return true;
}

bool returnablePocketInkJsSetError(const char *error) {
  if (pendingReturnableJs == nullptr || pendingReturnableJs->state == nullptr ||
      error == nullptr || error[0] == '\0') {
    return false;
  }
  if (pendingReturnableJs->errorSet) {
    return true;
  }
  if (pendingReturnableJs->stateSize >
      kReturnableJsMaxStateBytes - sizeof(ReturnableJsResultTrailer)) {
    return false;
  }
  const size_t nextSize =
      pendingReturnableJs->stateSize + sizeof(ReturnableJsResultTrailer);
  uint8_t *next = static_cast<uint8_t *>(psram::allocate(nextSize));
  if (next == nullptr) {
    return false;
  }
  memcpy(next, pendingReturnableJs->state, pendingReturnableJs->stateSize);
  ReturnableJsResultTrailer trailer;
  const size_t length = strlen(error);
  const size_t copied = length < sizeof(trailer.error) - 1U
                            ? length
                            : sizeof(trailer.error) - 1U;
  memcpy(trailer.error, error, copied);
  memcpy(next + pendingReturnableJs->stateSize, &trailer, sizeof(trailer));
  psram::release(pendingReturnableJs->state);
  pendingReturnableJs->state = next;
  pendingReturnableJs->stateSize = nextSize;
  pendingReturnableJs->errorSet = true;
  return true;
}

void returnablePocketInkJsFinish() {
  if (pendingReturnableJs == nullptr) {
    return;
  }
  psram::release(pendingReturnableJs->state);
  pendingReturnableJs->~ReturnableJsState();
  psram::release(pendingReturnableJs);
  pendingReturnableJs = nullptr;
}

bool pinkExecutableHeaderSupported(const PinkExecutableHeader &header) {
  return header.magic == PINK_EXECUTABLE_MAGIC &&
         header.abiVersion >= PINK_EXECUTABLE_MIN_ABI_VERSION &&
         header.abiVersion <= PINK_EXECUTABLE_ABI_VERSION &&
         header.headerSize >= sizeof(PinkExecutableHeader);
}

PinkExecutableLoadResult pinkExecutableLoadImage(
    File &file, const PinkExecutableHeader &header,
    const PinkExecutableLoadPolicy &policy, PinkExecutableImage &image) {
  pinkExecutableUnloadImage(image);

  const size_t totalImageSize =
      static_cast<size_t>(header.codeSize) + header.rodataSize;
  if (!pinkExecutableHeaderSupported(header) || header.codeSize == 0 ||
      header.codeSize > policy.maxCodeBytes ||
      header.rodataSize > policy.maxRodataBytes ||
      totalImageSize < header.codeSize ||
      totalImageSize > policy.maxImageBytes ||
      header.entryOffset >= header.codeSize ||
      header.relocationCount > policy.maxRelocations) {
    return PinkExecutableLoadResult::InvalidExecutable;
  }

  const size_t relocationBytes =
      static_cast<size_t>(header.relocationCount) * sizeof(uint32_t);
  if (relocationBytes / sizeof(uint32_t) != header.relocationCount) {
    return PinkExecutableLoadResult::InvalidRelocations;
  }
  const size_t imageOffset =
      static_cast<size_t>(header.headerSize) + relocationBytes;
  if (imageOffset < header.headerSize || !file.seek(imageOffset)) {
    return PinkExecutableLoadResult::SeekFailed;
  }

  bool byteWritable = true;
  image.code = allocateExecutableImage(header.codeSize, byteWritable);
  if (image.code == nullptr) {
    return PinkExecutableLoadResult::NoExecutableMemory;
  }
  if (!readExecutableImage(file, image.code, header.codeSize, byteWritable)) {
    pinkExecutableUnloadImage(image);
    return PinkExecutableLoadResult::ReadFailed;
  }

  if (header.rodataSize > 0) {
    image.rodata = allocateExecutableRodata(header.rodataSize,
                                            policy.rodataInPsram);
    if (image.rodata == nullptr) {
      pinkExecutableUnloadImage(image);
      return PinkExecutableLoadResult::NoDataMemory;
    }
    if (file.read(image.rodata, header.rodataSize) != header.rodataSize) {
      pinkExecutableUnloadImage(image);
      return PinkExecutableLoadResult::ReadFailed;
    }
  }

  if (!applyExecutableRelocations(file, header, image.code, image.rodata)) {
    pinkExecutableUnloadImage(image);
    return PinkExecutableLoadResult::InvalidRelocations;
  }
  image.entryPoint = image.code + header.entryOffset;
  return PinkExecutableLoadResult::Loaded;
}

void pinkExecutableUnloadImage(PinkExecutableImage &image) {
  heap_caps_free(image.code);
  heap_caps_free(image.rodata);
  image = {};
}

bool pinkLibraryReadManifest(const char *path,
                             PinkLibraryManifest &manifest) {
  manifest = {};
  File file = openProviderPath("sd", path);
  if (!file) {
    return false;
  }
  PinkExecutableHeader header = {};
  const bool read = readPinkLibraryManifest(file, header, manifest);
  file.close();
  if (!read) {
    manifest = {};
  }
  return read;
}

PinkLoadedLibrary::~PinkLoadedLibrary() { unload(); }

bool PinkLoadedLibrary::load(const char *path, uint32_t expectedType,
                             char *error, size_t errorSize) {
  unload();
  copyText(error, errorSize, "library open failed");
  File file = openProviderPath("sd", path);
  if (!file) {
    return false;
  }

  PinkExecutableHeader header = {};
  if (!readPinkLibraryManifest(file, header, manifest, expectedType)) {
    copyText(error, errorSize, "invalid library manifest");
    unload();
    return false;
  }

  const PinkExecutableLoadPolicy policy = {
      kPinkLibraryMaxCodeBytes,
      kPinkLibraryMaxRodataBytes,
      kPinkLibraryMaxImageBytes,
      kPinkLibraryMaxRelocations,
      true,
  };
  const PinkExecutableLoadResult result =
      pinkExecutableLoadImage(file, header, policy, image);
  if (result != PinkExecutableLoadResult::Loaded) {
    copyText(error, errorSize, pinkLibraryLoadError(result));
    unload();
    return false;
  }

  copyText(error, errorSize, "");
  return true;
}

void PinkLoadedLibrary::unload() {
  pinkExecutableUnloadImage(image);
  manifest = {};
}

ActiveApp *pinkExecutableRuntime() { return &pinkApp; }

void pinkExecutableRefreshApps() {
  if (!ensurePinkEntries()) {
    pinkAppCount = 0;
    return;
  }
  clearPinkEntries();
  if (!sdStorageMounted()) {
    return;
  }

  scanPinkDirectory("/bin/games", MENU_GAMES);
  scanPinkDirectory("/bin/apps", MENU_APPS);
#if ENABLE_NETWORK_APPS
  scanPinkDirectory("/bin/network", MENU_NETWORK);
#endif
}

size_t pinkExecutableCount(MenuCategory category) {
  size_t count = 0;
  for (size_t i = 0; i < pinkAppCount; i++) {
    if (pinkApps[i].category == category &&
        !hiddenByMenuButtonApp(pinkApps[i].id)) {
      count++;
    }
  }
  return count;
}

size_t pinkExecutableAllCount() { return pinkAppCount; }

bool pinkExecutableDefinitionAt(MenuCategory category, size_t index,
                                AppDefinition *out, char *id, size_t idSize,
                                char *label, size_t labelSize, char *icon,
                                size_t iconSize) {
  if (pinkApps == nullptr || out == nullptr || id == nullptr ||
      label == nullptr || icon == nullptr) {
    return false;
  }
  size_t visibleIndex = 0;
  PinkExecutableEntry *entry = nullptr;
  for (size_t i = 0; i < pinkAppCount; i++) {
    if (pinkApps[i].category != category) {
      continue;
    }
    if (hiddenByMenuButtonApp(pinkApps[i].id)) {
      continue;
    }
    if (visibleIndex == index) {
      entry = &pinkApps[i];
      break;
    }
    visibleIndex++;
  }
  if (entry == nullptr) {
    return false;
  }
  return populatePinkDefinition(*entry, out, id, idSize, label, labelSize,
                                icon, iconSize);
}

bool pinkExecutableDefinitionAllAt(size_t index, AppDefinition *out, char *id,
                                   size_t idSize, char *label,
                                   size_t labelSize, char *icon,
                                   size_t iconSize) {
  if (pinkApps == nullptr || index >= pinkAppCount) {
    return false;
  }
  return populatePinkDefinition(pinkApps[index], out, id, idSize, label,
                                labelSize, icon, iconSize);
}

bool pinkExecutableDefinitionById(const char *id, AppDefinition *out,
                                  char *idOut, size_t idSize, char *label,
                                  size_t labelSize, char *icon,
                                  size_t iconSize) {
  if (pinkApps == nullptr || id == nullptr || id[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < pinkAppCount; i++) {
    if (strcmp(pinkApps[i].id, id) == 0) {
      return populatePinkDefinition(pinkApps[i], out, idOut, idSize, label,
                                    labelSize, icon, iconSize);
    }
  }
  return false;
}

bool pinkExecutableLaunchPath(const char *path, AppDefinition *out, char *id,
                              size_t idSize, char *label, size_t labelSize,
                              char *icon, size_t iconSize) {
  if (path == nullptr || path[0] == '\0' || out == nullptr || id == nullptr ||
      label == nullptr || icon == nullptr) {
    return false;
  }

  copyLabel(label, labelSize, filenameForPath(path));
  copyIcon(icon, iconSize, label);
  bool iconFont = false;
  readAppMetadata(path, label, labelSize, icon, iconSize, iconFont);
  snprintf(id, idSize, "pink:%08lx",
           static_cast<unsigned long>(hashPath(path)));

  PinkExecutableEntry entry;
  entry.id = id;
  entry.path = const_cast<char *>(path);
  entry.label = label;
  entry.icon = icon;
  entry.iconFont = iconFont;
  entry.category = MENU_APPS;
  pinkApp.launch(entry);

  *out = AppDefinition{};
  out->id = id;
  out->label = label;
  out->icon = icon;
  out->category = MENU_APPS;
  out->runtime = pinkExecutableRuntime();
  out->visible = visiblePinkApp;
  out->iconFont = iconFont;
  out->behavior = pinkBehavior();
  return true;
}

bool pinkExecutableLaunchPathChecked(const char *path, AppDefinition *out,
                                     char *id, size_t idSize, char *label,
                                     size_t labelSize, char *icon,
                                     size_t iconSize) {
  if (!pinkExecutableLaunchPath(path, out, id, idSize, label, labelSize, icon,
                                iconSize)) {
    return false;
  }
  if (!pinkApp.loaded()) {
    pinkExecutableStop();
    return false;
  }
  return true;
}

bool pinkExecutableLaunchById(const char *id) {
  if (pinkApps == nullptr || id == nullptr || id[0] == '\0') {
    return false;
  }
  for (size_t i = 0; i < pinkAppCount; i++) {
    if (strcmp(pinkApps[i].id, id) == 0) {
      pinkApp.launch(pinkApps[i]);
      return true;
    }
  }
  return false;
}

void pinkExecutableStop() { pinkApp.stop(); }

void pinkExecutableRestoreState(const uint8_t *data, size_t size) {
  if (data != nullptr && size > 0U) {
    pinkApp.restoreContext(data, size);
  }
}

size_t pinkExecutablePeriodicTaskCount() {
  size_t count = 0;
  for (size_t i = 0; i < pinkAppCount; i++) {
    if (pinkApps[i].packedPeriodicSchedule != 0) {
      count++;
    }
  }
  return count;
}

bool pinkExecutablePeriodicTaskAt(size_t index,
                                  PinkExecutablePeriodicTask &out) {
  for (size_t i = 0; i < pinkAppCount; i++) {
    const PinkExecutableEntry &entry = pinkApps[i];
    if (entry.packedPeriodicSchedule == 0) {
      continue;
    }
    if (index-- != 0) {
      continue;
    }
    out = PinkExecutablePeriodicTask{};
    if (!copyText(out.id, sizeof(out.id), entry.id) ||
        !copyText(out.path, sizeof(out.path), entry.path)) {
      return false;
    }
    out.schedule = entry.periodic;
    out.packedSchedule = entry.packedPeriodicSchedule;
    return true;
  }
  return false;
}

PeriodicTaskAction pinkExecutableRunPeriodic(
    const PinkExecutablePeriodicTask &task, int64_t localUnix,
    uint16_t effectiveIntervalMinutes, Adafruit_GFX *display, bool &dirty) {
  return pinkApp.runPeriodic(task, localUnix, effectiveIntervalMinutes,
                             display, dirty);
}
