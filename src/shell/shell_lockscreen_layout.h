#ifndef SHELL_LOCKSCREEN_LAYOUT_H
#define SHELL_LOCKSCREEN_LAYOUT_H

#include "sys/app_display.h"

/// User-configurable presentation state for the lockscreen/sleep clock.
struct LockscreenLayoutConfig {
  char topIcon = 'S';
  bool showDate = true;
};

/// Reloads lockscreen presentation config after settings changes.
void lockscreenLayoutConfigReload();
/// Returns the active lockscreen configuration.
const LockscreenLayoutConfig &lockscreenLayoutConfig();
/// Draws the simple deep-sleep transition frame.
void drawLockscreenSleepScreen(AppDisplay &display);
/// Draws the low-power lockscreen clock frame.
void drawLockscreenClockScreen(AppDisplay &display, const char *timeText,
                               const char *dateText);

#endif
