#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "sys/app_display.h"
#include "sys/services/battery_monitor.h"
#include <stdint.h>

/// Data needed to render the top status bar for shell and app screens.
///
/// The status bar does not query services directly; callers provide one compact
/// snapshot so app rendering remains predictable and cheap.
struct StatusBarSnapshot {
  const char *dateText;
  const char *timeText;
  const BatterySnapshot *battery;
  char wifiIcon;
  bool usbConnected;
};

/// Draws date/time, battery, and connectivity indicators into the display.
void drawStatusBar(AppDisplay &display, const StatusBarSnapshot &status);

#endif
