#ifndef PINK_APP_API_H
#define PINK_APP_API_H

#include <stddef.h>
#include <stdint.h>

static const uint32_t PINK_EXECUTABLE_MAGIC = 0x4b4e4950UL;
static const uint16_t PINK_EXECUTABLE_ABI_VERSION = 7;
static const uint16_t PINK_EXECUTABLE_HEADER_SIZE = 28;
static const size_t PINK_EXECUTABLE_MAX_IMAGE_BYTES = 64U * 1024U;

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
};

#endif
