/// Owns the presentation of the screen shown while the device is entering or
/// remaining in low-power lockscreen mode. It keeps drawing separate from the
/// sleep-clock state machine and exposes the small user-configurable layout.
#ifndef SHELL_LOCKSCREEN_LAYOUT_H
#define SHELL_LOCKSCREEN_LAYOUT_H

#include "sys/app_display.h"

/// User-configurable presentation state for the lockscreen/sleep clock.
struct LockscreenLayoutConfig {
  /// Icon-font character drawn above the clock; null hides it.
  char topIcon = 'S';
  /// Whether the local date is drawn below the time.
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
