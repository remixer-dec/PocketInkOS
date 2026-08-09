#ifndef SHELL_DIALOG_LAYOUT_H
#define SHELL_DIALOG_LAYOUT_H

#include "sys/app_display.h"
#include "sys/services/touch_input.h"
#include "ui/status_bar.h"
#include <stdint.h>

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
  HotspotToggle,
  HttpServerToggle,
  FtpServerToggle,
  BluetoothToggle,
  CpuCycle,
  VolumeDown,
  VolumeMute,
  VolumeUp,
  Screenshot,
  VideoToggle,
  InvertToggle,
};

/// Paged groups in the power/quick-settings dialog.
enum class PowerDialogPage : uint8_t {
  Power,
  Device,
  Network,
  Volume,
  Screen,
};

/// Live device state displayed by the power/quick-settings dialog.
struct PowerDialogSnapshot {
  PowerDialogPage page = PowerDialogPage::Power;
  bool wifiOn = false;
  bool wifiConnected = false;
  bool bluetoothOn = false;
  bool hotspotOn = false;
  bool httpServerOn = false;
  bool ftpServerOn = false;
  bool httpServerAvailable = false;
  bool ftpServerAvailable = false;
  bool networkUnavailableNotice = false;
  uint16_t cpuMhz = 240;
  uint8_t volume = 60;
  bool muted = false;
  bool recording = false;
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

#endif
