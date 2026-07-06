#ifndef SHELL_LAYOUT_H
#define SHELL_LAYOUT_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/battery_monitor.h"
#include "sys/builtin_apps.h"
#include "sys/environment_monitor.h"
#include "sys/sd_storage.h"
#include "sys/touch_input.h"
#include "ui/status_bar.h"
#include <stddef.h>
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

/// User-configurable presentation state for shell-owned screens.
///
/// This separates persistent layout choices from live sensor/app data so render
/// functions can stay deterministic and easy to test.
struct ShellLayoutConfig {
  ShellLayoutMode layout = SHELL_LAYOUT_DEFAULT;
  ShellTemperatureUnit temperatureUnit = SHELL_TEMP_CELSIUS;
  uint16_t homeWidgets = 0;
  char lockscreenTopIcon = 'S';
  bool lockscreenDate = true;
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

/// Walks categories backward for button navigation.
MenuCategory previousMenuCategory(MenuCategory category);
/// Walks categories forward for button navigation.
MenuCategory nextMenuCategory(MenuCategory category);
/// Provides the short category label used by the app menu.
const char *menuCategoryTitle(MenuCategory category);

/// Keeps menu state valid after app catalog or category contents change.
void clampMenuState(MenuState &state);
/// Applies one backward menu step with category-aware wrapping.
void moveMenuPrevious(MenuState &state);
/// Applies one forward menu step with category-aware wrapping.
void moveMenuNext(MenuState &state);

/// Reloads presentation config after settings changes.
void shellLayoutConfigReload();
/// Shares the active presentation config with shell render paths.
const ShellLayoutConfig &shellLayoutConfig();
/// Checks a home-widget bit against the active layout config.
bool shellHomeWidgetEnabled(uint16_t widget);

/// Renders the home surface from a live ShellData snapshot.
void drawHomeScreen(AppDisplay &display, const ShellData &data);
/// Renders the category app menu and optional pressed-slot feedback.
void drawAppMenu(AppDisplay &display, const MenuState &state,
                 int8_t pressedSlot = -1);
/// Converts a touch into menu movement or an app selection.
bool hitTestAppMenu(const TouchPoint &point, MenuState &state,
                    AppCatalogEntry &selected, bool &stateChanged,
                    int8_t *hitSlot = nullptr);

/// Overlays the app quit confirmation dialog.
void drawQuitDialog(AppDisplay &display, const StatusBarSnapshot &status);
/// Hit-tests the quit confirmation action.
bool quitDialogHitYes(const TouchPoint &point);
/// Hit-tests the quit cancel action.
bool quitDialogHitNo(const TouchPoint &point);

/// Semantic actions emitted by the power/quick-settings dialog.
enum class PowerDialogAction : uint8_t {
  None,
  PreviousPage,
  NextPage,
  Reboot,
  PowerOff,
  DeepSleep,
  WifiToggle,
  BluetoothToggle,
  CpuCycle,
  VolumeDown,
  VolumeMute,
  VolumeUp,
};

/// Paged groups in the power/quick-settings dialog.
enum class PowerDialogPage : uint8_t {
  Power,
  Device,
  Volume,
};

/// Live device state displayed by the power/quick-settings dialog.
struct PowerDialogSnapshot {
  PowerDialogPage page = PowerDialogPage::Power;
  bool wifiOn = false;
  bool wifiConnected = false;
  bool bluetoothOn = false;
  uint16_t cpuMhz = 240;
  uint8_t volume = 60;
  bool muted = false;
};

/// Renders the power/quick-settings dialog from the supplied snapshot.
void drawPowerDialog(AppDisplay &display, const StatusBarSnapshot &status,
                     const PowerDialogSnapshot &snapshot,
                     PowerDialogAction pressed = PowerDialogAction::None);
/// Converts a dialog touch into a semantic quick-settings action.
PowerDialogAction powerDialogHitAction(const TouchPoint &point,
                                       PowerDialogPage page);
/// Draws the final user-visible frame before powering off.
void drawPowerOffScreen(AppDisplay &display);
/// Draws the battery-protection screen used before forced sleep/shutdown.
void drawLowBatteryScreen(AppDisplay &display);
/// Draws the transition frame before entering deep sleep.
void drawDeepSleepScreen(AppDisplay &display);
/// Draws the low-power clock frame shown while entering sleep-clock mode.
void drawDeepSleepClockScreen(AppDisplay &display, const char *timeText,
                              const char *dateText);

#endif
