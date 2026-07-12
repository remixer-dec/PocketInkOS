#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include "sys/services/touch_input.h"
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#ifndef ENABLE_TTS
#define ENABLE_TTS 0
#endif

/// Enumerates shell screens. Apps use SCREEN_APP plus AppDefinition metadata.
enum Screen {
  SCREEN_HOME = 0,
  SCREEN_T9_KEYBOARD = 2,
  SCREEN_QWERTY_ZOOM_KEYBOARD = 3,
  SCREEN_MENU = 4,
  SCREEN_APP = 5
};

/// Groups launcher entries into menu pages.
enum MenuCategory {
  MENU_GAMES,
  MENU_APPS
#if ENABLE_NETWORK_APPS
  ,
  MENU_NETWORK
#endif
};

/// Normalized outcome for app hooks and runtime button handlers.
enum class AppEventResult : uint8_t {
  Unhandled,
  Handled,
  Dirty,
  QuitRequested,
  GoHome,
  GoMenu,
  Reboot,
};

/// Common runtime interface used to draw apps and dispatch touch input.
class ActiveApp {
public:
  virtual ~ActiveApp() = default;
  virtual void draw(Adafruit_GFX &gfx) = 0;
  virtual bool handleTouch(const TouchPoint &point) = 0;
  virtual bool update() { return false; }
  virtual AppEventResult consumePendingEvent() {
    return AppEventResult::Unhandled;
  }
  virtual bool consumeDirtyRegion(int16_t *, int16_t *, int16_t *, int16_t *) {
    return false;
  }
  virtual bool hasActiveSession() const = 0;
};

/// Adapts an app object with touch support to the ActiveApp runtime contract.
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
  AppEventResult consumePendingEvent() override {
    if constexpr (requires { app.consumePendingEvent(); }) {
      return app.consumePendingEvent();
    }
    return AppEventResult::Unhandled;
  }
  bool consumeDirtyRegion(int16_t *x, int16_t *y, int16_t *w,
                          int16_t *h) override {
    if constexpr (requires { app.consumeDirtyRegion(x, y, w, h); }) {
      return app.consumeDirtyRegion(x, y, w, h);
    }
    return false;
  }
  bool hasActiveSession() const override { return app.hasActiveSession(); }

private:
  T &app;
};

/// Adapts an app object that draws itself and exposes session state, while
/// handling input through its own app-specific path instead of ActiveApp.
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

/// Pointer to a parameterless app hook such as onEnter, onExit, launch, or reset.
typedef void (*AppCallback)();
/// Reports whether an app should appear in the launcher, such as when the
/// Files app is only available while an SD card is mounted.
typedef bool (*AppVisibleHandler)();
/// Handles a button event such as onMenu or onPowerLong and returns an AppEventResult like GoHome or Reboot.
typedef AppEventResult (*AppEventHandler)();
/// Receives raw touch events before higher-level app dispatch.
typedef void (*AppRawTouchHandler)(const TouchEvent &event);
/// Saves app-owned state into a caller-provided buffer.
typedef size_t (*AppSaveContextHandler)(uint8_t *buffer, size_t capacity);
/// Restores app-owned state from a previously saved buffer.
typedef void (*AppRestoreContextHandler)(const uint8_t *buffer, size_t length);

/// Bundles runtime callbacks for one app, including lifecycle hooks like onEnter,
/// onExit, and onRelease, button handlers such as onMenu and onPowerLong, raw touch
/// handling, and state save/restore.
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
  AppCallback onRelease = nullptr;
};

/// Declares one app entry together with launcher metadata, including optional
/// icon-font rendering and visibility conditions, the per-launch reset/launch
/// hooks that run before switchTo(), and the AppBehavior callback bundle used
/// after the app becomes active.
struct AppDefinition {
  const char *id;
  const char *label;
  const char *icon;
  MenuCategory category;
  ActiveApp *runtime;
  AppVisibleHandler visible = nullptr;
  bool iconFont = false;
  AppCallback launch = nullptr;
  AppCallback reset = nullptr;
  AppBehavior behavior = {};
  uint32_t inactivityDeepSleepMs = 0;
};

/// Stores the current launcher category and page for returning to the menu.
struct MenuState {
  MenuCategory category;
  uint8_t page;
};

#endif
