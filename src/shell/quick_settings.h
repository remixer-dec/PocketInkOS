#ifndef SHELL_QUICK_SETTINGS_H
#define SHELL_QUICK_SETTINGS_H

#include "shell/shell_dialog_layout.h"
#include "sys/touch_input.h"

/// Shell-owned side effect invoked by quick settings without coupling to globals.
typedef void (*QuickSettingsCallback)(void *context);

/// Capability table for actions that QuickSettingsController can request.
///
/// The controller owns dialog state and hit testing; the shell provides these
/// callbacks for operations that affect power, retained context, or rendering.
struct QuickSettingsActions {
  void *context = nullptr;
  QuickSettingsCallback renderPowerOff = nullptr;
  QuickSettingsCallback prepareForPowerTransition = nullptr;
  QuickSettingsCallback saveRetainedContext = nullptr;
  QuickSettingsCallback enterSleepClock = nullptr;
};

/// Small state machine for the power/quick-settings dialog.
///
/// ShellController opens this on long power press, asks it for a render snapshot,
/// and forwards touches here so page navigation and device actions stay together.
class QuickSettingsController {
public:
  /// Opens the dialog on a page, resetting transient pressed feedback.
  void open(PowerDialogPage page = PowerDialogPage::Power);
  /// Closes the dialog when the shell leaves quick settings.
  void close();
  /// Indicates whether the shell should render and route touches to the dialog.
  bool isOpen() const;
  /// Builds render data from current dialog page and live device state.
  PowerDialogSnapshot snapshot() const;
  /// Converts a touch into page movement or an action for the shell to run.
  PowerDialogAction handleTouch(const TouchPoint &point);
  /// Runs an action after the shell has rendered pressed feedback.
  void runAction(PowerDialogAction action,
                 const QuickSettingsActions &actions) const;

private:
  PowerDialogPage page = PowerDialogPage::Power;
  unsigned long openedAt = 0;
  bool openFlag = false;

  PowerDialogPage previousPage() const;
  PowerDialogPage nextPage() const;
};

#endif
