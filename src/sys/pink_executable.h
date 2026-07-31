#ifndef PINK_EXECUTABLE_H
#define PINK_EXECUTABLE_H

#include "sys/app_runtime.h"
#include "sys/agentfiesta/pink_api.h"
#include "sys/pink_app_metadata.h"
#include <FS.h>
#include <stddef.h>
#include <stdint.h>

/// Magic value that marks a binary as a .pink executable.
static const uint32_t PINK_EXECUTABLE_MAGIC = 0x4b4e4950UL; // "PINK"
/// ABI version emitted by the current .pink build tooling.
static const uint16_t PINK_EXECUTABLE_ABI_VERSION = 17;
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
  /// Packed static periodic schedule; zero means no periodic hook.
  uint32_t periodicSchedule;
};
static_assert(sizeof(PinkExecutableHeader) == PINK_EXECUTABLE_HEADER_SIZE,
              "Pink executable header layout changed");

/// Reports whether the base header uses a supported .pink format and ABI.
bool pinkExecutableHeaderSupported(const PinkExecutableHeader &header);

/// Allocation and validation policy for one image using the .pink executable
/// layout. Apps and libraries share the layout but have different memory caps.
struct PinkExecutableLoadPolicy {
  size_t maxCodeBytes = 0;
  size_t maxRodataBytes = 0;
  size_t maxImageBytes = 0;
  uint32_t maxRelocations = 0;
  bool rodataInPsram = false;
};

/// Memory owned by one successfully loaded .pink image.
struct PinkExecutableImage {
  uint8_t *code = nullptr;
  uint8_t *rodata = nullptr;
  void *entryPoint = nullptr;
};

enum class PinkExecutableLoadResult : uint8_t {
  Loaded,
  InvalidExecutable,
  InvalidRelocations,
  SeekFailed,
  NoExecutableMemory,
  NoDataMemory,
  ReadFailed,
};

/// Loads code and rodata and applies relocations using the shared .pink layout.
PinkExecutableLoadResult pinkExecutableLoadImage(
    File &file, const PinkExecutableHeader &header,
    const PinkExecutableLoadPolicy &policy, PinkExecutableImage &image);
/// Releases all memory owned by image.
void pinkExecutableUnloadImage(PinkExecutableImage &image);

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
  PINK_EVENT_PERIODIC = 17,
};

enum PinkFont : uint8_t {
  PINK_FONT_DEFAULT = 0,
  PINK_FONT_ICON_8 = 1,
  PINK_FONT_ICON_12 = 2,
};

enum PinkApiStatus : int8_t {
  PINK_API_ERROR = -1,
  PINK_API_DENIED = -2,
  PINK_API_UNSUPPORTED = -3,
  PINK_API_PENDING = 0,
  PINK_API_OK = 1,
  PINK_API_END = 2,
};

enum PinkFeature : uint32_t {
  PINK_FEATURE_READ_FILES = 1U << 0U,
  PINK_FEATURE_WRITE_FILES = 1U << 1U,
  PINK_FEATURE_DELETE_FILES = 1U << 2U,
  PINK_FEATURE_AUDIO_CAPTURE = 1U << 3U,
  PINK_FEATURE_GPIO = 1U << 4U,
  PINK_FEATURE_SENSORS = 1U << 5U,
  PINK_FEATURE_WIFI_SCAN = 1U << 6U,
  PINK_FEATURE_AGENTFIESTA = 1U << 7U,
  PINK_FEATURE_RETURNABLE_JS = 1U << 8U,
  PINK_FEATURE_LAN_SERVER = 1U << 9U,
};

enum PinkFileAuthorization : uint8_t {
  PINK_AUTHORIZE_FILE_WRITE = 1U << 0U,
  PINK_AUTHORIZE_FILE_DELETE = 1U << 1U,
};

/// Callback for one transient entry returned by fileList. Returning false
/// stops enumeration, allowing callers to enforce their own visit bounds.
typedef bool (*PinkDirectoryEntryHandler)(const char *name, bool directory,
                                          uint32_t size, void *context);

struct PinkImageTransform {
  float a = 1.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
  float tx = 0.0f;
  float ty = 0.0f;
};

struct PinkSpriteFrame {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;

  bool valid() const {
    return width != 0U && height != 0U &&
           static_cast<uint32_t>(x) + width <= 0x7fffU &&
           static_cast<uint32_t>(y) + height <= 0x7fffU;
  }
};

/// Uniform, row-major spritesheet description. Rows and columns are zero-based.
struct PinkSpritesheet {
  uint16_t frameWidth = 0;
  uint16_t frameHeight = 0;
  uint16_t frameCount = 0;
  uint16_t rowCount = 0;

  static PinkSpritesheet defineSpritesheet(uint16_t width, uint16_t height,
                                           uint16_t frames,
                                           uint16_t rows) {
    PinkSpritesheet sheet;
    if (width == 0U || height == 0U || frames == 0U || rows == 0U ||
        rows > frames) {
      return sheet;
    }
    const uint32_t columns =
        (static_cast<uint32_t>(frames) + rows - 1U) / rows;
    if (columns * width > 0x7fffU ||
        static_cast<uint32_t>(rows) * height > 0x7fffU) {
      return sheet;
    }
    sheet.frameWidth = width;
    sheet.frameHeight = height;
    sheet.frameCount = frames;
    sheet.rowCount = rows;
    return sheet;
  }

  uint16_t columnCount() const {
    return frameCount != 0U && rowCount != 0U
               ? static_cast<uint16_t>(
                     (static_cast<uint32_t>(frameCount) + rowCount - 1U) /
                     rowCount)
               : 0U;
  }

  PinkSpriteFrame spriteAt(uint16_t row, uint16_t column) const {
    const uint16_t columns = columnCount();
    const uint32_t index = static_cast<uint32_t>(row) * columns + column;
    if (frameWidth == 0U || frameHeight == 0U || row >= rowCount ||
        column >= columns || index >= frameCount) {
      return {};
    }
    return {static_cast<uint16_t>(column * frameWidth),
            static_cast<uint16_t>(row * frameHeight), frameWidth,
            frameHeight};
  }
};

struct PinkSensorSnapshot {
  uint8_t temperatureValid = 0;
  uint8_t humidityValid = 0;
  uint8_t cpuTemperatureValid = 0;
  uint8_t reserved = 0;
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
  float cpuTemperatureC = 0.0f;
};

struct PinkWifiNetwork {
  char ssid[33] = {};
  int32_t rssi = -127;
};

enum PinkHttpMethod : uint8_t {
  PINK_HTTP_GET = 0,
  PINK_HTTP_POST = 1,
  PINK_HTTP_PUT = 2,
  PINK_HTTP_DELETE = 3,
};

struct PinkHttpRequest {
  const uint8_t *body = nullptr;
  size_t bodySize = 0;
  uint32_t requestId = 0;
  uint8_t method = PINK_HTTP_GET;
  char path[192] = {};
  char query[384] = {};
  char contentType[96] = {};
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
  /// Periodic-event metadata appended in ABI 12.
  int64_t periodicLocalUnix = 0;
  uint16_t periodicIntervalMinutes = 0;
  uint8_t periodicWakeLevel = 0;
  uint8_t periodicDisplayMode = 0;
  uint8_t periodicAction = 0;
  uint8_t periodicReserved[3] = {};
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
  int8_t (*drawImagePath)(const char *path, int16_t x, int16_t y, int16_t w,
                          int16_t h, uint8_t dither, bool scaleToFit,
                          const PinkImageTransform *transform);
  int8_t (*drawImageBuffer)(const uint8_t *data, size_t size,
                            const char *format, int16_t x, int16_t y,
                            int16_t w, int16_t h, uint8_t dither,
                            bool scaleToFit,
                            const PinkImageTransform *transform);
  int8_t (*fileRead)(const char *path, size_t offset, uint8_t *out,
                     size_t capacity, size_t *outRead, size_t *outFileSize);
  int8_t (*fileWrite)(const char *path, const uint8_t *data, size_t size,
                      bool append);
  int8_t (*filePicker)(const char *initialPath, const char *extensionFilter,
                       char *outPath, size_t outSize);
  bool (*openApp)(const char *appId);
  int8_t (*gpioConfigure)(uint8_t pin, uint8_t mode);
  int8_t (*gpioRead)(uint8_t pin, int *value);
  int8_t (*gpioWrite)(uint8_t pin, bool high);
  bool (*sensorSnapshot)(PinkSensorSnapshot *out, bool refresh);
  int8_t (*wifiScan)();
  size_t (*wifiNetworkCount)();
  bool (*wifiNetworkAt)(size_t index, PinkWifiNetwork *out);
  size_t (*fontCount)();
  bool (*fontInfo)(size_t index, char *name, size_t nameSize,
                   uint8_t *nominalSize);
  bool (*setFontByName)(const char *name);
  void *reservedAbi9Capture[5];
  int32_t (*audioLoadPath)(const char *path);
  bool (*audioFormatSupported)(const char *format);
  int32_t (*audioLoadBuffer)(const uint8_t *data, size_t size,
                             const char *format);
  int8_t (*audioPlay)(uint32_t handle);
  int8_t (*audioStop)(uint32_t handle);
  int8_t (*audioUpdate)(uint32_t handle);
  void (*audioRelease)(uint32_t handle);
  void *reservedAbi9DatePicker[4];
  bool (*drawQr)(const char *text, int16_t x, int16_t y, int16_t size,
                 uint16_t color);
  void *reservedAbi9Keyboard[6];
  int8_t (*fileDelete)(const char *path);
  int8_t (*audioCaptureWav)(uint32_t maxDurationMs, const uint8_t **data,
                            size_t *size);
  int8_t (*getDateInput)(int initialYear, int initialMonth, int initialDay,
                         int *year, int *month, int *day);
  int8_t (*getKeyboardInput)(const char *initialText, uint8_t layout,
                             size_t maxLength, char *out, size_t outSize);
  int8_t (*audioCapturePcm16)(uint32_t maxDurationMs, const uint8_t **data,
                              size_t *size);
  int8_t (*drawVectorPath)(const char *path, int16_t x, int16_t y, int16_t w,
                           int16_t h, const PinkImageTransform *transform);
  int8_t (*drawVectorString)(const char *svg, size_t size, int16_t x,
                             int16_t y, int16_t w, int16_t h,
                             const PinkImageTransform *transform);
  /// ABI 13: cacheEntries is an LRU capacity; zero streams without caching.
  int8_t (*drawImagePathCached)(
      const char *path, int16_t x, int16_t y, int16_t w, int16_t h,
      uint8_t dither, bool scaleToFit, uint8_t cacheEntries,
      const PinkImageTransform *transform);
  int8_t (*drawImageBufferCached)(
      const uint8_t *data, size_t size, const char *format, int16_t x,
      int16_t y, int16_t w, int16_t h, uint8_t dither, bool scaleToFit,
      uint8_t cacheEntries, const PinkImageTransform *transform);
  /// ABI 14: versioned AgentFiesta service table. The pointer is present in
  /// offline builds; AI submissions then return UNSUPPORTED.
  const PinkAgentFiestaApi *agentFiesta;
  /// ABI 15: grants paths inside /misc/agentfiesta/<app-name>/. Passing a null
  /// path during a touch event arms one exact /bin/js/*.js export grant; the
  /// later non-null call consumes it.
  int8_t (*authorizeFile)(const char *path, uint8_t operations);
  /// ABI 15: takes ownership of a PSRAM state blob on success, launches one
  /// source path, and restores the blob to this .pink app on MENU-long.
  int8_t (*launchPocketInkJsReturnable)(const char *sourcePath,
                                        uint8_t *ownedState,
                                        size_t stateSize);
  /// ABI 15: enumerates one directory without exposing provider internals.
  int8_t (*fileList)(const char *path, PinkDirectoryEntryHandler handler,
                     void *context);
  /// ABI 16: draws one native-size spritesheet cell before affine transforms.
  int8_t (*drawSpritePath)(const char *path, const PinkSpriteFrame *frame,
                           int16_t x, int16_t y, uint8_t dither,
                           uint8_t cacheEntries,
                           const PinkImageTransform *transform);
  int8_t (*drawSpriteBuffer)(const uint8_t *data, size_t size,
                             const char *format,
                             const PinkSpriteFrame *frame, int16_t x,
                             int16_t y, uint8_t dither, uint8_t cacheEntries,
                             const PinkImageTransform *transform);
  /// ABI 17: permissioned LAN HTTP server. Dynamic requests are polled from
  /// UPDATE so app code never runs in the networking callback context.
  int8_t (*httpStart)(uint16_t port, const char *staticDirectory,
                      const char *username, const char *password);
  int8_t (*httpRoute)(uint8_t method, const char *path);
  int8_t (*httpNextRequest)(PinkHttpRequest *out);
  int8_t (*httpRespond)(uint32_t requestId, int status,
                        const char *contentType, const uint8_t *body,
                        size_t bodySize);
  bool (*localAddress)(char *out, size_t outSize);
};

inline bool pinkSpritesSupported(const PinkHost *host) {
  return host != nullptr && host->abiVersion >= 16U &&
         host->hostSize >=
             offsetof(PinkHost, drawSpriteBuffer) +
                 sizeof(PinkHost::drawSpriteBuffer) &&
         host->drawSpritePath != nullptr && host->drawSpriteBuffer != nullptr;
}

inline bool pinkHttpSupported(const PinkHost *host) {
  return host != nullptr && host->abiVersion >= 17U &&
         host->hostSize >=
             offsetof(PinkHost, localAddress) +
                 sizeof(PinkHost::localAddress) &&
         host->httpStart != nullptr && host->httpRoute != nullptr &&
         host->httpNextRequest != nullptr && host->httpRespond != nullptr &&
         host->localAddress != nullptr;
}

inline int8_t pinkDrawSpritePath(
    const PinkHost *host, const char *path, const PinkSpriteFrame &frame,
    int16_t x, int16_t y, uint8_t dither = 1U, uint8_t cacheEntries = 0U,
    const PinkImageTransform *transform = nullptr) {
  if (!frame.valid()) {
    return PINK_API_ERROR;
  }
  return pinkSpritesSupported(host)
             ? host->drawSpritePath(path, &frame, x, y, dither, cacheEntries,
                                    transform)
             : static_cast<int8_t>(PINK_API_UNSUPPORTED);
}

inline int8_t pinkDrawSpriteBuffer(
    const PinkHost *host, const uint8_t *data, size_t size, const char *format,
    const PinkSpriteFrame &frame, int16_t x, int16_t y,
    uint8_t dither = 1U, uint8_t cacheEntries = 0U,
    const PinkImageTransform *transform = nullptr) {
  if (!frame.valid()) {
    return PINK_API_ERROR;
  }
  return pinkSpritesSupported(host)
             ? host->drawSpriteBuffer(data, size, format, &frame, x, y,
                                      dither, cacheEntries, transform)
             : static_cast<int8_t>(PINK_API_UNSUPPORTED);
}

/// Copyable description of one installed executable's static periodic hook.
struct PinkExecutablePeriodicTask {
  char id[24] = {};
  char path[192] = {};
  PeriodicTaskSchedule schedule;
  uint32_t packedSchedule = 0;
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
/// Counts every discovered .pink app across all launcher categories.
size_t pinkExecutableAllCount();
/// Copies a .pink app by discovery order without launcher visibility filtering.
bool pinkExecutableDefinitionAllAt(size_t index, AppDefinition *out, char *id,
                                   size_t idSize, char *label,
                                   size_t labelSize, char *icon,
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
/// Returnable-test variant that rejects and cleans up a failed executable load.
bool pinkExecutableLaunchPathChecked(const char *path, AppDefinition *out,
                                     char *id, size_t idSize, char *label,
                                     size_t labelSize, char *icon,
                                     size_t iconSize);
/// Stops the active .pink app and releases its loaded image.
void pinkExecutableStop();

/// Restores one opaque state blob through PINK_EVENT_RESTORE_STATE.
void pinkExecutableRestoreState(const uint8_t *data, size_t size);

/// One-entry mailbox used while a .pink owner is unloaded for a PocketInkJS
/// test. The mailbox owns the state blob after a successful queue operation.
bool returnablePocketInkJsQueue(const char *ownerPath, const char *sourcePath,
                                uint8_t *ownedState, size_t stateSize);
bool returnablePocketInkJsRequested();
bool returnablePocketInkJsTakeSource(char *out, size_t outSize);
bool returnablePocketInkJsReturnPending();
bool returnablePocketInkJsOwnerPath(char *out, size_t outSize);
bool returnablePocketInkJsState(const uint8_t *&data, size_t &size);
/// Appends only the first bounded PocketInkJS diagnostic to the returned state.
bool returnablePocketInkJsSetError(const char *error);
void returnablePocketInkJsFinish();

/// Counts installed executables with valid static periodic metadata.
size_t pinkExecutablePeriodicTaskCount();
/// Copies one installed static periodic task by periodic-task index.
bool pinkExecutablePeriodicTaskAt(size_t index,
                                  PinkExecutablePeriodicTask &out);
/// Loads, validates, invokes, and releases one executable periodic hook.
PeriodicTaskAction pinkExecutableRunPeriodic(
    const PinkExecutablePeriodicTask &task, int64_t localUnix,
    uint16_t effectiveIntervalMinutes, Adafruit_GFX *display, bool &dirty);

#endif
