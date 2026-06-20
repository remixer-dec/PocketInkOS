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
