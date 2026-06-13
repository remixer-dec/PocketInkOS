#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <stdint.h>

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
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
  SCREEN_CONTACT_LINKS
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
  virtual bool hasActiveSession() const = 0;
};

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
typedef AppEventResult (*AppEventHandler)();
typedef void (*AppRawTouchHandler)(const TouchEvent &event);

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
};

struct AppDefinition {
  const char *id;
  const char *label;
  const char *icon;
  MenuCategory category;
  Screen screen;
  ActiveApp *runtime;
  AppCallback launch = nullptr;
  AppCallback reset = nullptr;
  AppBehavior behavior = {};
};

struct MenuState {
  MenuCategory category;
  uint8_t page;
};

#endif
