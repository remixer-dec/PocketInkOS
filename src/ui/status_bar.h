#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include "sys/app_display.h"
#include "sys/services/battery_monitor.h"
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Data needed to render the top status bar when the caller owns the snapshot.
///
/// The snapshot overload takes one compact view so app rendering remains
/// predictable and cheap.
struct StatusBarSnapshot {
  const char *dateText;
  const char *timeText;
  const BatterySnapshot *battery;
  char wifiIcon;
  bool usbConnected;
};

/// Captures the live status values into caller-owned text buffers.
void captureStatusBarSnapshot(StatusBarSnapshot &status, char *dateText,
                              size_t dateSize, char *timeText,
                              size_t timeSize);
/// Draws the live date/time, battery, and connectivity indicators.
void drawStatusBar(Adafruit_GFX &display);
/// Draws date/time, battery, and connectivity indicators from a snapshot.
void drawStatusBar(Adafruit_GFX &display, const StatusBarSnapshot &status);

#endif
