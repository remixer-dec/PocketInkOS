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

enum ShellLayoutMode : uint8_t {
  SHELL_LAYOUT_DEFAULT,
  SHELL_LAYOUT_ULTRA_LARGE,
};

enum ShellTemperatureUnit : uint8_t {
  SHELL_TEMP_CELSIUS,
  SHELL_TEMP_FAHRENHEIT,
};

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

struct ShellLayoutConfig {
  ShellLayoutMode layout = SHELL_LAYOUT_DEFAULT;
  ShellTemperatureUnit temperatureUnit = SHELL_TEMP_CELSIUS;
  uint16_t homeWidgets = 0;
  char lockscreenTopIcon = 'S';
  bool lockscreenDate = true;
};

struct ShellData {
  StatusBarSnapshot status;
  const BatterySnapshot *battery;
  const EnvironmentSnapshot *environment;
  const SdStorageSnapshot *sd;
};

MenuCategory previousMenuCategory(MenuCategory category);
MenuCategory nextMenuCategory(MenuCategory category);
const char *menuCategoryTitle(MenuCategory category);

void clampMenuState(MenuState &state);
void moveMenuPrevious(MenuState &state);
void moveMenuNext(MenuState &state);

void shellLayoutConfigReload();
const ShellLayoutConfig &shellLayoutConfig();
bool shellHomeWidgetEnabled(uint16_t widget);

void drawHomeScreen(AppDisplay &display, const ShellData &data);
void drawAppMenu(AppDisplay &display, const MenuState &state,
                 int8_t pressedSlot = -1);
bool hitTestAppMenu(const TouchPoint &point, MenuState &state,
                    AppCatalogEntry &selected, bool &stateChanged,
                    int8_t *hitSlot = nullptr);

void drawQuitDialog(AppDisplay &display, const StatusBarSnapshot &status);
bool quitDialogHitYes(const TouchPoint &point);
bool quitDialogHitNo(const TouchPoint &point);

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

enum class PowerDialogPage : uint8_t {
  Power,
  Device,
  Volume,
};

struct PowerDialogSnapshot {
  PowerDialogPage page = PowerDialogPage::Power;
  bool wifiOn = false;
  bool wifiConnected = false;
  bool bluetoothOn = false;
  uint16_t cpuMhz = 240;
  uint8_t volume = 60;
  bool muted = false;
};

void drawPowerDialog(AppDisplay &display, const StatusBarSnapshot &status,
                     const PowerDialogSnapshot &snapshot,
                     PowerDialogAction pressed = PowerDialogAction::None);
PowerDialogAction powerDialogHitAction(const TouchPoint &point,
                                       PowerDialogPage page);
void drawPowerOffScreen(AppDisplay &display);
void drawLowBatteryScreen(AppDisplay &display);
void drawDeepSleepScreen(AppDisplay &display);
void drawDeepSleepClockScreen(AppDisplay &display, const char *timeText,
                              const char *dateText);

#endif
