#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#ifndef ENABLE_TTS
#define ENABLE_TTS 0
#endif

enum Screen {
  SCREEN_HOME,
  SCREEN_KEYBOARD,
  SCREEN_T9_KEYBOARD,
  SCREEN_QWERTY_ZOOM_KEYBOARD,
  SCREEN_MENU,
  SCREEN_TICTACTOE,
  SCREEN_MINESWEEPER,
  SCREEN_HANGMAN,
  SCREEN_SUDOKU,
  SCREEN_WORDLE,
  SCREEN_CHESS,
  SCREEN_CALCULATOR,
  SCREEN_QR,
  SCREEN_PAINT,
  SCREEN_DEGHOST,
  SCREEN_GFX,
  SCREEN_FILES,
  SCREEN_PINK_EXECUTABLE,
  SCREEN_CONTACT_LINKS
#if ENABLE_TTS
  ,
  SCREEN_TTS
#endif
#if ENABLE_NETWORK_APPS
  ,
  SCREEN_WIFI,
  SCREEN_REDDIT,
  SCREEN_HN,
  SCREEN_AI,
  SCREEN_WEATHER
#endif
};

enum MenuCategory {
  MENU_GAMES,
  MENU_APPS
#if ENABLE_NETWORK_APPS
  ,
  MENU_NETWORK
#endif
};

enum class AppEventResult : uint8_t {
  Unhandled,
  Handled,
  Dirty,
  QuitRequested,
  GoHome,
  GoMenu,
  Reboot,
};

class ActiveApp {
public:
  virtual void draw(Adafruit_GFX &gfx) = 0;
  virtual bool handleTouch(const TouchPoint &point) = 0;
  virtual bool update() { return false; }
  virtual bool consumeDirtyRegion(int16_t *, int16_t *, int16_t *, int16_t *) {
    return false;
  }
  virtual bool hasActiveSession() const = 0;
};

namespace app_runtime_detail {

template <typename T>
auto consumeDirtyRegion(T &app, int16_t *x, int16_t *y, int16_t *w,
                        int16_t *h, int)
    -> decltype(app.consumeDirtyRegion(x, y, w, h), bool()) {
  return app.consumeDirtyRegion(x, y, w, h);
}

template <typename T>
bool consumeDirtyRegion(T &, int16_t *, int16_t *, int16_t *, int16_t *, ...) {
  return false;
}

} // namespace app_runtime_detail

template <typename T, bool SupportsUpdate = false>
class AppScreen : public ActiveApp {
public:
  explicit AppScreen(T &target) : app(target) {}

  void draw(Adafruit_GFX &gfx) override { app.draw(gfx); }
  bool handleTouch(const TouchPoint &point) override {
    return app.handleTouch(point);
  }
  bool update() override {
    if constexpr (SupportsUpdate) {
      return app.update();
    }
    return false;
  }
  bool consumeDirtyRegion(int16_t *x, int16_t *y, int16_t *w,
                          int16_t *h) override {
    return app_runtime_detail::consumeDirtyRegion(app, x, y, w, h, 0);
  }
  bool hasActiveSession() const override { return app.hasActiveSession(); }

private:
  T &app;
};

template <typename T>
class TouchlessAppScreen : public ActiveApp {
public:
  explicit TouchlessAppScreen(T &target) : app(target) {}

  void draw(Adafruit_GFX &gfx) override { app.draw(gfx); }
  bool handleTouch(const TouchPoint &) override { return false; }
  bool hasActiveSession() const override { return app.hasActiveSession(); }

private:
  T &app;
};

typedef void (*AppCallback)();
typedef bool (*AppVisibleHandler)();
typedef AppEventResult (*AppEventHandler)();
typedef void (*AppRawTouchHandler)(const TouchEvent &event);
typedef size_t (*AppSaveContextHandler)(uint8_t *buffer, size_t capacity);
typedef void (*AppRestoreContextHandler)(const uint8_t *buffer, size_t length);

struct AppBehavior {
  AppCallback onEnter = nullptr;
  AppCallback onExit = nullptr;
  AppEventHandler onMenu = nullptr;
  AppEventHandler onMenuDouble = nullptr;
  AppEventHandler onMenuLong = nullptr;
  AppEventHandler onPower = nullptr;
  AppEventHandler onPowerDouble = nullptr;
  AppEventHandler onPowerLong = nullptr;
  AppRawTouchHandler onRawTouch = nullptr;
  AppSaveContextHandler onSaveContext = nullptr;
  AppRestoreContextHandler onRestoreContext = nullptr;
};

struct AppDefinition {
  const char *id;
  const char *label;
  const char *icon;
  MenuCategory category;
  Screen screen;
  ActiveApp *runtime;
  AppVisibleHandler visible = nullptr;
  bool iconFont = false;
  AppCallback launch = nullptr;
  AppCallback reset = nullptr;
  AppBehavior behavior = {};
  uint32_t inactivityDeepSleepMs = 0;
};

struct MenuState {
  MenuCategory category;
  uint8_t page;
};

#endif
