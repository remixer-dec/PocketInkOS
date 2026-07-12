/// Bridges the two physical device buttons into shell-level click, double-click,
/// and long-press callbacks. A background task samples the GPIO buttons;
/// shellButtonsDispatch() safely delivers the queued events on the main loop.
#ifndef SHELL_BUTTONS_H
#define SHELL_BUTTONS_H

/// Callback signature for a recognized hardware-button gesture.
typedef void (*ShellButtonCallback)();

/// Shell actions assigned to each gesture on the menu and power buttons.
struct ShellButtonHandlers {
  /// Single press of the menu/boot button.
  ShellButtonCallback onMenu = nullptr;
  /// Double press of the menu/boot button.
  ShellButtonCallback onMenuDouble = nullptr;
  /// Press held past the configured menu long-press threshold.
  ShellButtonCallback onMenuLong = nullptr;
  /// Single press of the power/back button.
  ShellButtonCallback onPower = nullptr;
  /// Double press of the power/back button.
  ShellButtonCallback onPowerDouble = nullptr;
  /// Press held past the configured power long-press threshold.
  ShellButtonCallback onPowerLong = nullptr;
};

/// Configures button timing, callbacks, and the background GPIO polling task.
void shellButtonsBegin(const ShellButtonHandlers &handlers);
/// Delivers all queued gestures on the caller's thread; returns true on activity.
bool shellButtonsDispatch();

#endif
