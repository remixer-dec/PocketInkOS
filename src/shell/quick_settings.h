#ifndef SHELL_QUICK_SETTINGS_H
#define SHELL_QUICK_SETTINGS_H

#include "sys/touch_input.h"
#include "ui/shell_layout.h"

typedef void (*QuickSettingsActionFeedbackRenderer)(void *context,
                                                    PowerDialogAction action);
typedef void (*QuickSettingsCallback)(void *context);

struct QuickSettingsActions {
  void *context = nullptr;
  QuickSettingsActionFeedbackRenderer showActionFeedback = nullptr;
  QuickSettingsCallback renderPowerOff = nullptr;
  QuickSettingsCallback prepareForPowerTransition = nullptr;
  QuickSettingsCallback saveRetainedContext = nullptr;
  QuickSettingsCallback enterSleepClock = nullptr;
};

class QuickSettingsController {
public:
  void open(PowerDialogPage page = PowerDialogPage::Power);
  void close();
  bool isOpen() const;
  PowerDialogSnapshot snapshot() const;
  bool handleTouch(const TouchPoint &point, const QuickSettingsActions &actions);

private:
  PowerDialogPage page = PowerDialogPage::Power;
  unsigned long openedAt = 0;
  bool openFlag = false;

  void showActionFeedback(const QuickSettingsActions &actions,
                          PowerDialogAction action) const;
  PowerDialogPage previousPage() const;
  PowerDialogPage nextPage() const;
};

#endif
