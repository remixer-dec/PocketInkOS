#ifndef PINK_APP_API_H
#define PINK_APP_API_H

#include <stddef.h>
#include <stdint.h>
#include "sys/agentfiesta/pink_api.h"
#include "sys/pink_app_metadata.h"

/// Magic value that marks a binary as a .pink executable.
static const uint32_t PINK_EXECUTABLE_MAGIC = 0x4b4e4950UL;
/// ABI version emitted by the current .pink build tooling.
static const uint16_t PINK_EXECUTABLE_ABI_VERSION = 16;
/// Oldest .pink ABI version the current firmware still accepts.
static const uint16_t PINK_EXECUTABLE_MIN_ABI_VERSION = 7;
static const uint16_t PINK_EXECUTABLE_HEADER_SIZE = 28;
static const size_t PINK_EXECUTABLE_MAX_IMAGE_BYTES = 64U * 1024U;

/// Events that the firmware can deliver to an external .pink app.
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

/// Fonts exposed by the firmware drawing host.
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
};

enum PinkFileAuthorization : uint8_t {
  PINK_AUTHORIZE_FILE_WRITE = 1U << 0U,
  PINK_AUTHORIZE_FILE_DELETE = 1U << 1U,
};

typedef bool (*PinkDirectoryEntryHandler)(const char *name, bool directory,
                                          uint32_t size, void *context);

enum PinkPeriodicWakeLevel : uint8_t {
  PINK_PERIODIC_SYSTEM = 2,
  PINK_PERIODIC_NETWORK = 3,
};

enum PinkPeriodicDisplayMode : uint8_t {
  PINK_PERIODIC_DISPLAY_NONE = 0,
  PINK_PERIODIC_DISPLAY_PARTIAL = 1,
  PINK_PERIODIC_DISPLAY_FULL = 2,
};

enum PinkPeriodicAction : uint8_t {
  PINK_PERIODIC_COMPLETE = 0,
  PINK_PERIODIC_WAKE_DEVICE = 3,
  PINK_PERIODIC_WAKE_APP = 4,
};

/// Declares the app's single static periodic hook at global scope.
/// The build tool stores these four bytes in the executable header; they are
/// not loaded into app memory. Omit the declaration for an app with no hook.
#define PINK_PERIODIC_TASK(intervalMinutes, wakeLevel, displayMode)            \
  extern "C" __attribute__((section(".pink_metadata"), used)) const uint8_t  \
      pinkPeriodicTaskMetadata[4] = {                                         \
          static_cast<uint8_t>((intervalMinutes) & 0xffU),                    \
          static_cast<uint8_t>(((intervalMinutes) >> 8U) & 0xffU),            \
          static_cast<uint8_t>(wakeLevel), static_cast<uint8_t>(displayMode)}

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

/// Shared event payload exchanged between the firmware and a .pink app.
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
  int64_t periodicLocalUnix = 0;
  uint16_t periodicIntervalMinutes = 0;
  uint8_t periodicWakeLevel = 0;
  uint8_t periodicDisplayMode = 0;
  uint8_t periodicAction = 0;
  uint8_t periodicReserved[3] = {};
};

/// Function table and runtime context passed to a .pink app instance.
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
  const PinkAgentFiestaApi *agentFiesta;
  /// A null path arms one /bin/js/*.js grant and is accepted only from a touch
  /// callback. Workspace paths derived from this app's filename need no arm.
  int8_t (*authorizeFile)(const char *path, uint8_t operations);
  int8_t (*launchPocketInkJsReturnable)(const char *sourcePath,
                                        uint8_t *ownedState,
                                        size_t stateSize);
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
};

inline bool pinkSpritesSupported(const PinkHost *host) {
  return host != nullptr && host->abiVersion >= 16U &&
         host->hostSize >=
             offsetof(PinkHost, drawSpriteBuffer) +
                 sizeof(PinkHost::drawSpriteBuffer) &&
         host->drawSpritePath != nullptr && host->drawSpriteBuffer != nullptr;
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

#endif
