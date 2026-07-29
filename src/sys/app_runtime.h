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

/// Tells the shell what an app hook did and what it should do next.
enum class AppEventResult : uint8_t {
  /// The shell continues with its normal handling for this event.
  Unhandled,
  /// The event is consumed; the display is unchanged.
  Handled,
  /// The event is consumed and the shell redraws the display.
  Dirty,
  /// Open the quit confirmation dialog.
  QuitRequested,
  /// Return to the home screen.
  GoHome,
  /// Return to the app menu.
  GoMenu,
  /// Restart the device.
  Reboot,
};

/// Services available when a periodic hook runs during a deep-sleep wake.
enum class PeriodicWakeLevel : uint8_t {
  /// Time, battery status, and no other services.
  Basic = 1,
  /// Core services and SD storage; Wi-Fi stays off.
  System = 2,
  /// System services plus an attempt to reconnect Wi-Fi.
  Network = 3,
};

/// Display access a periodic hook requests; separate from its wake level.
enum class PeriodicDisplayMode : uint8_t {
  /// Do not initialize the display; context.display is null.
  None = 0,
  /// Initialize for a partial update, avoiding a full-screen flash.
  Partial = 1,
  /// Initialize for a normal full-screen refresh.
  Full = 2,
};

/// Tells the periodic wake service what to do after this hook call.
enum class PeriodicTaskAction : uint8_t {
  /// Finish this hook for this wake.
  Complete = 0,
  /// At Basic, re-run this hook at System; otherwise finish it.
  RequestSystem = 1,
  /// Before Network, re-run this hook at Network after the Wi-Fi attempt.
  RequestNetwork = 2,
  /// Continue into the normal interactive boot path.
  WakeDevice = 3,
  /// Continue normal boot with this app in the foreground.
  WakeApp = 4,
};

/// Read-only capabilities and clock state supplied to a periodic hook.
struct PeriodicTaskContext {
  /// Stable identifier of the app whose hook is running; null for system tasks.
  const char *appId = nullptr;
  int64_t localUnix = 0;
  uint16_t intervalMinutes = 0;
  PeriodicWakeLevel wakeLevel = PeriodicWakeLevel::Basic;
  PeriodicDisplayMode displayMode = PeriodicDisplayMode::None;
  bool clockSet = false;
  bool wifiPreferred = false;
  bool wifiConnected = false;
  bool lowBattery = false;
  /// Non-null only when the app registered a display mode for this hook.
  Adafruit_GFX *display = nullptr;
};

/// Static schedule for the single periodic hook an app may register.
struct PeriodicTaskSchedule {
  /// Requested cadence in whole minutes; zero means no registered hook.
  uint16_t intervalMinutes = 0;
  PeriodicWakeLevel wakeLevel = PeriodicWakeLevel::Basic;
  PeriodicDisplayMode displayMode = PeriodicDisplayMode::None;
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
/// Receives each queued touch event instead of ActiveApp::handleTouch while
/// active; shell dialogs and overlays still take priority.
typedef void (*AppRawTouchHandler)(const TouchEvent &event);
/// Saves app-owned state into a caller-provided buffer.
typedef size_t (*AppSaveContextHandler)(uint8_t *buffer, size_t capacity);
/// Restores app-owned state from a previously saved buffer.
typedef void (*AppRestoreContextHandler)(const uint8_t *buffer, size_t length);
/// Runs one periodic app hook at the currently initialized wake tier.
typedef PeriodicTaskAction (*AppPeriodicHandler)(
    const PeriodicTaskContext &context);
/// Draws an app-owned lockscreen widget inside the supplied centered bounds.
typedef void (*AppWidgetDrawHandler)(Adafruit_GFX &gfx, const uint8_t *value,
                                     size_t valueSize, int16_t x, int16_t y,
                                     int16_t width, int16_t height);

/// Static lockscreen widget metadata kept with an app definition in flash.
struct AppWidgetDefinition {
  const char *label = nullptr;
  AppWidgetDrawHandler draw = nullptr;
};

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
  AppPeriodicHandler onPeriodic = nullptr;
  PeriodicTaskSchedule periodic = {};
  AppWidgetDefinition widget = {};
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
