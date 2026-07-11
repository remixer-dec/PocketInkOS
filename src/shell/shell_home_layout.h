#ifndef SHELL_HOME_LAYOUT_H
#define SHELL_HOME_LAYOUT_H

#include "sys/app_display.h"
#include "sys/services/battery_monitor.h"
#include "sys/services/environment_monitor.h"
#include "sys/services/sd_storage.h"
#include "ui/status_bar.h"
#include <stdint.h>

/// Home-screen density profile loaded from shell configuration.
enum ShellLayoutMode : uint8_t {
  SHELL_LAYOUT_DEFAULT,
  SHELL_LAYOUT_ULTRA_LARGE,
};

/// Display unit for temperature values shown on shell surfaces.
enum ShellTemperatureUnit : uint8_t {
  SHELL_TEMP_CELSIUS,
  SHELL_TEMP_FAHRENHEIT,
};

/// Bitmask for optional home widgets so low-memory config can stay compact.
enum ShellWidget : uint16_t {
  SHELL_WIDGET_TEMP = 1U << 0,
  SHELL_WIDGET_HUM = 1U << 1,
  SHELL_WIDGET_CPU_TEMP = 1U << 2,
  SHELL_WIDGET_SD = 1U << 3,
  SHELL_WIDGET_BATTERY = 1U << 4,
  SHELL_WIDGET_NOTIFICATIONS = 1U << 5,
  SHELL_WIDGET_LOGO_TEXT = 1U << 6,
  SHELL_WIDGET_BUTTON_HELP = 1U << 7,
  SHELL_WIDGET_WALLPAPER = 1U << 8,
  SHELL_WIDGET_WEATHER = 1U << 9,
};

/// User-configurable presentation state for the home screen.
///
/// This separates persistent layout choices from live sensor/app data so render
/// functions can stay deterministic and easy to test.
struct ShellLayoutConfig {
  ShellLayoutMode layout = SHELL_LAYOUT_DEFAULT;
  ShellTemperatureUnit temperatureUnit = SHELL_TEMP_CELSIUS;
  uint16_t homeWidgets = 0;
};

/// Live data passed into shell renderers for one frame.
///
/// Pointers are non-owning; callers keep sensor/storage snapshots in their
/// service modules and pass the current view here.
struct ShellData {
  StatusBarSnapshot status;
  const BatterySnapshot *battery;
  const EnvironmentSnapshot *environment;
  const SdStorageSnapshot *sd;
};

/// Reloads both home and lockscreen presentation config after settings changes.
void shellLayoutConfigReload();
/// Reloads only home presentation config.
void shellHomeLayoutConfigReload();
/// Shares the active presentation config with shell render paths.
const ShellLayoutConfig &shellLayoutConfig();
/// Checks a home-widget bit against the active layout config.
bool shellHomeWidgetEnabled(uint16_t widget);

/// Renders the home surface from a live ShellData snapshot.
void drawHomeScreen(AppDisplay &display, const ShellData &data);

#endif
