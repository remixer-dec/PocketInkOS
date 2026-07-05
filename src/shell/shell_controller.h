#ifndef SHELL_CONTROLLER_H
#define SHELL_CONTROLLER_H

#include "shell/quick_settings.h"
#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/builtin_apps.h"
#include "sys/touch_input.h"
#include "ui/shell_layout.h"
#include "ui/text_input_controller.h"

struct ShellControllerHooks {
  void *context = nullptr;
  void (*refreshHome)(void *context) = nullptr;
  void (*noteUserActivity)(void *context) = nullptr;
  void (*prepareForPowerTransition)(void *context) = nullptr;
  void (*enterSleepClockDeepSleep)(void *context) = nullptr;
  void (*renderPowerOffScreen)(void *context) = nullptr;
};

class ShellController {
public:
  ShellController(AppDisplay &display, TouchInput &touch,
                  TextInputController &textInput);

  void setHooks(const ShellControllerHooks &hooks);

  Screen currentScreen() const;
  bool shouldRedrawForBatteryRefresh() const;
  unsigned long inactivityDeepSleepMs(unsigned long defaultMs) const;

  void markDirty();
  void clampMenu();
  void switchTo(Screen next, ActiveApp *nextApp = nullptr,
                const AppDefinition *nextDefinition = nullptr);

  void handleMenuButton();
  void handleMenuDoubleButton();
  void handleMenuLongButton();
  void handlePowerSingleButton();
  void handlePowerDoubleButton();
  void handlePowerLongButton();

  bool dispatchTouch();
  bool updateActiveApp();
  bool handlePendingFilesPinkLaunch();
  void handleCatalogChanged();
  void refreshHomeMinuteIfNeeded();
  void renderIfDirty();

  void saveRetainedContextForSleep();
  void restoreRetainedContextAfterSleep();

private:
  AppDisplay &display;
  TouchInput &touch;
  TextInputController &textInput;
  ShellControllerHooks hooks;

  Screen screen = SCREEN_HOME;
  ActiveApp *activeApp = nullptr;
  const AppDefinition *activeDefinition = nullptr;
  AppCatalogEntry activeDefinitionEntry = {};
  bool dirty = true;
  bool confirmQuit = false;
  bool activePausedForDialog = false;
  MenuState menuState = {MENU_GAMES, 0};
  int8_t menuPressedSlot = -1;
  unsigned long quitDialogOpenedAt = 0;
  int64_t lastHomeMinute = -1;
  QuickSettingsController quickSettings;

  static const unsigned long MENU_PRESS_HIGHLIGHT_MS = 180;

  bool sameAppDefinition(const AppDefinition *current,
                         const AppDefinition *nextDefinition) const;
  void storeActiveDefinition(const AppDefinition &definition);
  bool isSessionScreen() const;
  void noteUserActivity();
  void refreshHome();

  void openQuitDialog();
  void cancelQuitDialog();
  void openQuickSettings();
  void closeQuickSettings();
  bool applyAppEventResult(AppEventResult result);
  bool handleAppEvent(AppEventHandler handler);
  void handlePowerButton(AppEventHandler overrideHandler);
  void switchKeyboardMode();

  ShellData currentShellData(char *dateText, size_t dateSize, char *timeText,
                             size_t timeSize);
  void drawHome();
  bool handleQuitDialogTouch(const TouchPoint &point);
  bool handleQuickSettingsTouch(const TouchPoint &point);
  QuickSettingsActions quickSettingsActions();
  void drawMenu();
  void launchApp(const AppDefinition &app);
  bool launchAppById(const char *id);
  void launchMenuButtonApp();
  void handleMenuTouch(const TouchPoint &point);
  void drawActiveScreen();
  void redrawActiveScreenPartial();
  bool handleScreenTouch(const TouchPoint &point);
  void render();
  void showQuickSettingsActionFeedback(PowerDialogAction action);

  void prepareForPowerTransition();
  void enterSleepClockDeepSleep();
  void renderPowerOffScreen();

  static void showQuickSettingsActionFeedbackThunk(void *context,
                                                   PowerDialogAction action);
  static void prepareForPowerTransitionThunk(void *context);
  static void enterSleepClockDeepSleepThunk(void *context);
  static void renderPowerOffScreenThunk(void *context);
  static void saveRetainedContextThunk(void *context);

  static void restoreLaunchApp(void *context, const AppDefinition &app);
  static void restoreSwitchTo(void *context, Screen screen);
  static const AppDefinition *restoreActiveDefinition(void *context);
};

#endif
