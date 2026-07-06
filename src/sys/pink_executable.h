#ifndef PINK_EXECUTABLE_H
#define PINK_EXECUTABLE_H

#include "sys/app_runtime.h"
#include <stddef.h>
#include <stdint.h>

/// Magic value that marks a binary as a .pink executable.
static const uint32_t PINK_EXECUTABLE_MAGIC = 0x4b4e4950UL; // "PINK"
/// ABI version emitted by the current .pink build tooling.
static const uint16_t PINK_EXECUTABLE_ABI_VERSION = 8;
/// Oldest .pink ABI version this firmware still accepts.
static const uint16_t PINK_EXECUTABLE_MIN_ABI_VERSION = 7;
/// Size in bytes of PinkExecutableHeader.
static const uint16_t PINK_EXECUTABLE_HEADER_SIZE = 28;
/// Maximum code plus rodata image size loaded for one .pink app.
static const size_t PINK_EXECUTABLE_MAX_IMAGE_BYTES = 64U * 1024U;
/// Maximum number of discovered .pink apps exposed to the launcher.
static const size_t PINK_EXECUTABLE_MAX_APPS = 255;
/// Relocation flag indicating a pointer into the rodata segment.
static const uint32_t PINK_RELOCATION_RODATA = 0x80000000UL;
/// Mask for the relocation offset bits.
static const uint32_t PINK_RELOCATION_OFFSET_MASK = 0x7fffffffUL;

/// On-disk header for a .pink executable image.
struct PinkExecutableHeader {
  /// Must equal PINK_EXECUTABLE_MAGIC.
  uint32_t magic;
  /// ABI version required by the app image.
  uint16_t abiVersion;
  /// Header size written by the toolchain.
  uint16_t headerSize;
  /// Executable code segment size in bytes.
  uint32_t codeSize;
  /// Read-only data segment size in bytes.
  uint32_t rodataSize;
  /// Entry-point offset inside the code segment.
  uint32_t entryOffset;
  /// Number of relocation records following the image.
  uint32_t relocationCount;
  /// Reserved for future header fields.
  uint32_t reserved;
};

enum PinkEventType : uint8_t {
  PINK_EVENT_START = 1,
  PINK_EVENT_STOP = 2,
  PINK_EVENT_DRAW = 3,
  PINK_EVENT_TOUCH = 4,
  PINK_EVENT_UPDATE = 5,
  PINK_EVENT_TOUCH_DOWN = 6,
  PINK_EVENT_TOUCH_MOVE = 7,
  PINK_EVENT_TOUCH_UP = 8,
  PINK_EVENT_MENU_BUTTON = 9,
  PINK_EVENT_MENU_DOUBLE = 10,
  PINK_EVENT_MENU_LONG = 11,
  PINK_EVENT_POWER_BUTTON = 12,
  PINK_EVENT_POWER_DOUBLE = 13,
  PINK_EVENT_POWER_LONG = 14,
  PINK_EVENT_SAVE_STATE = 15,
  PINK_EVENT_RESTORE_STATE = 16,
};

enum PinkFont : uint8_t {
  PINK_FONT_DEFAULT = 0,
  PINK_FONT_ICON_8 = 1,
  PINK_FONT_ICON_12 = 2,
};

struct PinkEvent {
  uint8_t type = 0;
  uint8_t handled = 0;
  uint8_t dirty = 0;
  uint8_t reserved = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t *data = nullptr;
  size_t dataLength = 0;
  size_t dataCapacity = 0;
  size_t eventSize = 0;
  uint32_t featureFlags = 0;
};

struct PinkHost {
  uint16_t abiVersion;
  uint16_t screenWidth;
  uint16_t screenHeight;
  uint16_t reserved;
  const char *appId;
  const char *appPath;
  uint8_t *memory;
  size_t memorySize;
  uint32_t (*millis)();
  void (*delayMs)(uint32_t ms);
  void (*log)(const char *message);
  bool (*networkConnected)();
  void (*keepAwake)();
  void (*deepSleepPreventAcquire)();
  void (*deepSleepPreventRelease)();
  void (*setFont)(uint8_t font);
  void (*setTextSize)(uint8_t size);
  void (*setTextColor)(uint16_t color);
  void (*setCursor)(int16_t x, int16_t y);
  void (*print)(const char *text);
  void (*drawPixel)(int16_t x, int16_t y, uint16_t color);
  void (*drawLine)(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                   uint16_t color);
  void (*drawRect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void (*fillRect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void (*fillScreen)(uint16_t color);
  void *(*psramAlloc)(size_t bytes);
  void (*psramFree)(void *ptr);
  bool (*configGet)(const char *key, char *out, size_t outSize);
  bool (*configSet)(const char *key, const char *value);
  size_t hostSize;
  uint32_t featureFlags;
  uint32_t reserved2;
};

typedef void (*PinkEntryPoint)(const PinkHost *host, PinkEvent *event);

/// Returns the runtime adapter used when a .pink app is active.
ActiveApp *pinkExecutableRuntime();
/// Re-scans storage for available .pink apps.
void pinkExecutableRefreshApps();
/// Counts visible .pink apps in one launcher category.
size_t pinkExecutableCount(MenuCategory category);
/// Copies one visible .pink app definition into caller-provided buffers.
bool pinkExecutableDefinitionAt(MenuCategory category, size_t index,
                                AppDefinition *out, char *id, size_t idSize,
                                char *label, size_t labelSize, char *icon,
                                size_t iconSize);
/// Copies a discovered .pink app definition by stable id, including entries
/// hidden from the launcher because they are the quick-access app.
bool pinkExecutableDefinitionById(const char *id, AppDefinition *out,
                                  char *idOut, size_t idSize, char *label,
                                  size_t labelSize, char *icon,
                                  size_t iconSize);
/// Launches a discovered .pink app by stable id.
bool pinkExecutableLaunchById(const char *id);
/// Launches a .pink executable at an explicit filesystem path.
bool pinkExecutableLaunchPath(const char *path, AppDefinition *out, char *id,
                              size_t idSize, char *label, size_t labelSize,
                              char *icon, size_t iconSize);
/// Stops the active .pink app and releases its loaded image.
void pinkExecutableStop();

#endif
