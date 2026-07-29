#include "sys/services/lockscreen_content.h"

#include "sys/builtin_apps.h"
#include "sys/system_config.h"

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR RTC_DATA_ATTR
#endif

#ifndef RTC_FAST_ATTR
#define RTC_FAST_ATTR RTC_NOINIT_ATTR
#endif

namespace {

constexpr uint16_t kMagic = 0x4c43U;
constexpr size_t kAppIdCapacity = 24;
constexpr size_t kNotificationLineCapacity = 32;
constexpr int16_t kContentX = 4;
constexpr int16_t kContentY = 8;
constexpr int16_t kContentWidth = 192;
constexpr int16_t kContentHeight = 64;

struct RetainedLockscreenContent {
  uint16_t magic;
  uint16_t checksum;
  uint8_t valueSize;
  uint8_t notificationActive;
  uint8_t redrawRequested;
  uint8_t reserved;
  char widgetAppId[kAppIdCapacity];
  uint8_t value[LOCKSCREEN_WIDGET_VALUE_CAPACITY];
  char first[kNotificationLineCapacity];
  char second[kNotificationLineCapacity];
};

RTC_FAST_ATTR RetainedLockscreenContent retained;

uint16_t checksum(const RetainedLockscreenContent &content) {
  const uint8_t *bytes =
      reinterpret_cast<const uint8_t *>(&content.valueSize);
  const size_t length =
      sizeof(content) - offsetof(RetainedLockscreenContent, valueSize);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return static_cast<uint16_t>((hash >> 16U) ^ hash);
}

void commit() {
  retained.magic = kMagic;
  retained.checksum = checksum(retained);
}

bool load() {
  return retained.magic == kMagic && retained.checksum == checksum(retained) &&
         retained.valueSize <= LOCKSCREEN_WIDGET_VALUE_CAPACITY;
}

void reset() {
  memset(&retained, 0, sizeof(retained));
  commit();
}

void copyText(char *out, size_t outSize, const char *text) {
  if (outSize == 0) {
    return;
  }
  strncpy(out, text != nullptr ? text : "", outSize - 1);
  out[outSize - 1] = '\0';
}

const AppDefinition *selectedWidget() {
  if (!load() || retained.widgetAppId[0] == '\0') {
    return nullptr;
  }
  const size_t count = builtInAppDefinitionCount();
  for (size_t i = 0; i < count; i++) {
    const AppDefinition *app = builtInAppDefinitionAt(i);
    if (app != nullptr && app->id != nullptr &&
        strcmp(app->id, retained.widgetAppId) == 0 &&
        app->behavior.widget.draw != nullptr) {
      return app;
    }
  }
  return nullptr;
}

void centeredText(Adafruit_GFX &gfx, const char *text, int16_t baseline) {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.getTextBounds(text, 0, baseline, &x, &y, &width, &height);
  gfx.setCursor((200 - static_cast<int16_t>(width)) / 2 - x, baseline);
  gfx.print(text);
}

bool combinedNotificationFits(Adafruit_GFX &gfx, char *combined,
                              size_t combinedSize) {
  if (retained.second[0] == '\0') {
    copyText(combined, combinedSize, retained.first);
  } else {
    snprintf(combined, combinedSize, "%s %s", retained.first,
             retained.second);
  }
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.getTextBounds(combined, 0, 0, &x, &y, &width, &height);
  return width <= kContentWidth;
}

void drawNotification(Adafruit_GFX &gfx) {
  gfx.setFont();
  gfx.setTextColor(1);
  gfx.setTextSize(1);
  char combined[kNotificationLineCapacity * 2];
  if (combinedNotificationFits(gfx, combined, sizeof(combined))) {
    centeredText(gfx, combined, kContentY + 36);
    return;
  }
  centeredText(gfx, retained.first, kContentY + 25);
  centeredText(gfx, retained.second, kContentY + 43);
}

} // namespace

void lockscreenContentPrepareForSleep() {
  if (!load()) {
    reset();
  }
  char configured[kAppIdCapacity] = {};
  configGet("lockscreen.widget", configured, sizeof(configured));
  if (strcmp(configured, "none") == 0) {
    configured[0] = '\0';
  }
  if (strcmp(configured, retained.widgetAppId) != 0) {
    copyText(retained.widgetAppId, sizeof(retained.widgetAppId), configured);
    retained.valueSize = 0;
    memset(retained.value, 0, sizeof(retained.value));
  }
  retained.redrawRequested = 0;
  commit();
}

void lockscreenNotificationClear() {
  if (!load()) {
    reset();
    return;
  }
  retained.notificationActive = 0;
  retained.first[0] = '\0';
  retained.second[0] = '\0';
  retained.redrawRequested = 0;
  commit();
}

bool lockscreenWidgetUpdate(const char *appId, const void *value,
                            size_t valueSize) {
  if (!load() || appId == nullptr ||
      strcmp(appId, retained.widgetAppId) != 0 ||
      valueSize > LOCKSCREEN_WIDGET_VALUE_CAPACITY ||
      (valueSize != 0 && value == nullptr)) {
    return false;
  }
  const bool changed =
      retained.valueSize != valueSize ||
      (valueSize != 0 && memcmp(retained.value, value, valueSize) != 0);
  if (!changed) {
    return true;
  }
  retained.valueSize = static_cast<uint8_t>(valueSize);
  if (valueSize != 0) {
    memcpy(retained.value, value, valueSize);
  }
  retained.redrawRequested = retained.notificationActive == 0 ? 1 : 0;
  commit();
  return true;
}

bool lockscreenNotify(const char *first, const char *second) {
  if (first == nullptr || first[0] == '\0') {
    return false;
  }
  if (!load()) {
    reset();
  }
  copyText(retained.first, sizeof(retained.first), first);
  copyText(retained.second, sizeof(retained.second), second);
  retained.notificationActive = 1;
  retained.redrawRequested = 1;
  commit();
  return true;
}

bool drawLockscreenContent(Adafruit_GFX &gfx) {
  if (!load()) {
    return false;
  }
  if (retained.notificationActive != 0) {
    drawNotification(gfx);
    return true;
  }
  const AppDefinition *app = selectedWidget();
  if (app == nullptr || retained.valueSize == 0) {
    return false;
  }
  app->behavior.widget.draw(gfx, retained.value, retained.valueSize, kContentX,
                            kContentY, kContentWidth, kContentHeight);
  return true;
}

bool lockscreenContentTakeRedrawRequest() {
  if (!load() || retained.redrawRequested == 0) {
    return false;
  }
  retained.redrawRequested = 0;
  commit();
  return true;
}
