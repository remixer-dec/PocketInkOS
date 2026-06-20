#include "sys/pink_executable.h"
#include "fs/file_provider.h"
#include "sys/builtin_apps.h"
#include "sys/global.h"
#include "sys/inactivity_sleep_guard.h"
#include "sys/sd_storage.h"
#include "ui/icon_ascii_font.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include <stdio.h>

#ifndef MALLOC_CAP_EXEC
#define MALLOC_CAP_EXEC 0
#endif
#ifndef MALLOC_CAP_32BIT
#define MALLOC_CAP_32BIT 0
#endif

namespace {

static const uint16_t kPinkPathCapacity = 192;
static const uint8_t kPinkLabelCapacity = 8;
static const uint8_t kPinkIdCapacity = 16;
static const size_t kPinkMemorySize = 4096;
static const size_t kPinkMaxRelocations = 4096;

struct PinkExecutableEntry {
  char *id = nullptr;
  char *path = nullptr;
  char *label = nullptr;
  MenuCategory category = MENU_APPS;
};

struct PinkScanContext {
  size_t next = 0;
  const char *directory = nullptr;
  MenuCategory category = MENU_APPS;
};

PinkExecutableEntry *pinkApps = nullptr;
size_t pinkAppCount = 0;
size_t pinkAppCapacity = 0;
Adafruit_GFX *pinkDrawTarget = nullptr;
char activeAppId[kPinkIdCapacity] = {};
char activeAppPath[kPinkPathCapacity] = {};
uint8_t *activeMemory = nullptr;
size_t activeMemorySize = 0;
uint8_t pinkSleepGuardDepth = 0;

class PinkExecutableApp;
PinkExecutableApp *activePinkApp = nullptr;

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

void clearPinkEntries() {
  for (size_t i = 0; i < pinkAppCount; i++) {
    free(pinkApps[i].id);
    free(pinkApps[i].path);
    free(pinkApps[i].label);
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
  char id[kPinkIdCapacity];
  if (!joinPath(path, sizeof(path), scan->directory, entry.name)) {
    return true;
  }

  copyLabel(label, sizeof(label), entry.name);
  snprintf(id, sizeof(id), "pink:%08lx",
           static_cast<unsigned long>(hashPath(path)));

  PinkExecutableEntry &app = pinkApps[scan->next];
  app.id = duplicateText(id);
  app.path = duplicateText(path);
  app.label = duplicateText(label);
  if (app.id == nullptr || app.path == nullptr || app.label == nullptr) {
    free(app.id);
    free(app.path);
    free(app.label);
    app = PinkExecutableEntry{};
    return false;
  }
  app.category = scan->category;
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

uint8_t *allocateExecutableImage(size_t imageSize, size_t &allocatedSize,
                                 bool &byteWritable) {
  allocatedSize = align4(imageSize);
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
  if (message == nullptr) {
    return;
  }
  Serial.print("[pink] ");
  Serial.println(message);
}

bool hostNetworkConnected() { return wifiIsConnected(); }

void hostKeepAwake() { inactivitySleepKeepAwake(); }

void hostDeepSleepPreventAcquire() {
  if (pinkSleepGuardDepth < 255U) {
    pinkSleepGuardDepth++;
    inactivitySleepGuardAcquire();
  }
}

void hostDeepSleepPreventRelease() {
  if (pinkSleepGuardDepth > 0U) {
    pinkSleepGuardDepth--;
    inactivitySleepGuardRelease();
  }
}

void releasePinkSleepGuards() {
  while (pinkSleepGuardDepth > 0U) {
    pinkSleepGuardDepth--;
    inactivitySleepGuardRelease();
  }
}

void hostSetFont(uint8_t font) {
  if (pinkDrawTarget == nullptr) {
    return;
  }
  switch (font) {
  case PINK_FONT_ICON_8:
    pinkDrawTarget->setFont(&iconASCII8pt7b);
    break;
  case PINK_FONT_ICON_12:
    pinkDrawTarget->setFont(&iconASCII12pt7b);
    break;
  default:
    pinkDrawTarget->setFont();
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
  if (bytes == 0) {
    return nullptr;
  }
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void hostPsramFree(void *ptr) {
  if (ptr != nullptr) {
    heap_caps_free(ptr);
  }
}

PinkHost makeHost() {
  PinkHost host;
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
  return host;
}

class PinkExecutableApp : public ActiveApp {
public:
  void launch(const PinkExecutableEntry &entry) {
    unload();
    copyText(path, sizeof(path), entry.path);
    copyText(appId, sizeof(appId), entry.id);
    copyText(activeAppPath, sizeof(activeAppPath), path);
    copyText(activeAppId, sizeof(activeAppId), appId);
    for (size_t i = 0; i < sizeof(memory); i++) {
      memory[i] = 0;
    }
    activeMemory = memory;
    activeMemorySize = sizeof(memory);
    load();
    activePinkApp = this;
    sendSimpleEvent(PINK_EVENT_START);
  }

  void stop() { unload(); }

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
  uint8_t *code = nullptr;
  uint8_t *rodata = nullptr;
  size_t codeSize = 0;
  size_t rodataSize = 0;
  size_t codeAllocSize = 0;
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
    if (code != nullptr) {
      heap_caps_free(code);
      code = nullptr;
    }
    if (rodata != nullptr) {
      heap_caps_free(rodata);
      rodata = nullptr;
    }
    codeSize = 0;
    rodataSize = 0;
    codeAllocSize = 0;
    entry = nullptr;
    status[0] = '\0';
    activeAppId[0] = '\0';
    activeAppPath[0] = '\0';
    activeMemory = nullptr;
    activeMemorySize = 0;
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
    const size_t totalImageSize =
        static_cast<size_t>(header.codeSize) + header.rodataSize;
    if (header.magic != PINK_EXECUTABLE_MAGIC ||
        header.abiVersion != PINK_EXECUTABLE_ABI_VERSION ||
        header.headerSize < sizeof(PinkExecutableHeader) || header.codeSize == 0 ||
        header.codeSize > PINK_EXECUTABLE_MAX_IMAGE_BYTES ||
        header.rodataSize > PINK_EXECUTABLE_MAX_IMAGE_BYTES ||
        totalImageSize < header.codeSize ||
        totalImageSize > PINK_EXECUTABLE_MAX_IMAGE_BYTES ||
        header.entryOffset >= header.codeSize ||
        header.relocationCount > kPinkMaxRelocations) {
      copyText(status, sizeof(status), "BAD EXECUTABLE");
      return;
    }

    const size_t relocationBytes =
        static_cast<size_t>(header.relocationCount) * sizeof(uint32_t);
    if (relocationBytes / sizeof(uint32_t) != header.relocationCount) {
      copyText(status, sizeof(status), "BAD RELOCS");
      return;
    }
    const size_t codeOffset = static_cast<size_t>(header.headerSize) +
                              relocationBytes;
    if (codeOffset < header.headerSize || !file.seek(codeOffset)) {
      copyText(status, sizeof(status), "SEEK FAILED");
      return;
    }

    bool byteWritable = true;
    code =
        allocateExecutableImage(header.codeSize, codeAllocSize, byteWritable);
    if (code == nullptr) {
      copyText(status, sizeof(status), "NO EXEC MEM");
      return;
    }
    if (!readExecutableImage(file, code, header.codeSize, byteWritable)) {
      copyText(status, sizeof(status), "READ FAILED");
      heap_caps_free(code);
      code = nullptr;
      codeAllocSize = 0;
      return;
    }
    if (header.rodataSize > 0) {
      rodata = static_cast<uint8_t *>(
          heap_caps_malloc(header.rodataSize,
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      if (rodata == nullptr) {
        copyText(status, sizeof(status), "NO DATA MEM");
        heap_caps_free(code);
        code = nullptr;
        codeAllocSize = 0;
        return;
      }
      if (file.read(rodata, header.rodataSize) != header.rodataSize) {
        copyText(status, sizeof(status), "READ FAILED");
        heap_caps_free(code);
        heap_caps_free(rodata);
        code = nullptr;
        rodata = nullptr;
        codeAllocSize = 0;
        return;
      }
    }
    if (!applyExecutableRelocations(file, header, code, rodata)) {
      copyText(status, sizeof(status), "BAD RELOCS");
      heap_caps_free(code);
      if (rodata != nullptr) {
        heap_caps_free(rodata);
      }
      code = nullptr;
      rodata = nullptr;
      codeAllocSize = 0;
      return;
    }

    codeSize = header.codeSize;
    rodataSize = header.rodataSize;
    entry = reinterpret_cast<PinkEntryPoint>(code + header.entryOffset);
    Serial.printf("[pink] loaded %s code=%p codeSize=%lu rodata=%p rodataSize=%lu entry=%p relocs=%lu\n",
                  path, code, static_cast<unsigned long>(codeSize), rodata,
                  static_cast<unsigned long>(rodataSize), reinterpret_cast<void *>(entry),
                  static_cast<unsigned long>(header.relocationCount));
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
    if (event.type == PINK_EVENT_START || event.type == PINK_EVENT_DRAW ||
        event.type == PINK_EVENT_STOP) {
      Serial.printf("[pink] event %u entry=%p\n",
                    static_cast<unsigned>(event.type),
                    reinterpret_cast<void *>(entry));
    }
    pinkDrawTarget = gfx;
    PinkHost host = makeHost();
    entry(&host, &event);
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

} // namespace

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
    if (pinkApps[i].category == category) {
      count++;
    }
  }
  return count;
}

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
    if (visibleIndex == index) {
      entry = &pinkApps[i];
      break;
    }
    visibleIndex++;
  }
  if (entry == nullptr) {
    return false;
  }
  copyText(id, idSize, entry->id);
  copyText(label, labelSize, entry->label);
  copyIcon(icon, iconSize, label);
  *out = AppDefinition{};
  out->id = id;
  out->label = label;
  out->icon = icon;
  out->category = category;
  out->screen = SCREEN_PINK_EXECUTABLE;
  out->runtime = pinkExecutableRuntime();
  out->visible = visiblePinkApp;
  out->behavior = pinkBehavior();
  return true;
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
  snprintf(id, idSize, "pink:%08lx",
           static_cast<unsigned long>(hashPath(path)));

  PinkExecutableEntry entry;
  entry.id = id;
  entry.path = const_cast<char *>(path);
  entry.label = label;
  entry.category = MENU_APPS;
  pinkApp.launch(entry);

  *out = AppDefinition{};
  out->id = id;
  out->label = label;
  out->icon = icon;
  out->category = MENU_APPS;
  out->screen = SCREEN_PINK_EXECUTABLE;
  out->runtime = pinkExecutableRuntime();
  out->visible = visiblePinkApp;
  out->behavior = pinkBehavior();
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
