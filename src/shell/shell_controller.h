#ifndef SHELL_CONTROLLER_H
#define SHELL_CONTROLLER_H

#include "shell/quick_settings.h"
#include "shell/shell_dialog_layout.h"
#include "shell/shell_home_layout.h"
#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/builtin_apps.h"
#include "sys/touch_input.h"
#include "ui/text_input_controller.h"

/// Hooks from the sketch runtime into ShellController.
///
/// These keep platform-level behaviors, such as power transitions and home data
/// refresh, outside the controller while still allowing the shell to coordinate
/// them from button and quick-settings flows.
struct ShellControllerHooks {
  void *context = nullptr;
  void (*refreshHome)(void *context) = nullptr;
  void (*noteUserActivity)(void *context) = nullptr;
  void (*prepareForPowerTransition)(void *context) = nullptr;
  void (*enterSleepClockDeepSleep)(void *context) = nullptr;
  void (*renderPowerOffScreen)(void *context) = nullptr;
};

/// Main coordinator for the firmware shell.
///
/// The sketch owns hardware services, then calls this controller for button
/// events, touch dispatch, active app updates, retained context, and redraws.
/// This keeps screen routing and app lifecycle in one place.
class ShellController {
public:
  /// Binds shell coordination to the shared display, touch, and text services.
  ShellController(AppDisplay &display, TouchInput &touch,
                  TextInputController &textInput);

  /// Installs runtime hooks used for power transitions and home refreshes.
  void setHooks(const ShellControllerHooks &hooks);

  /// Exposes the active screen for code that must branch outside the controller.
  Screen currentScreen() const;
  /// Lets the main loop redraw the status area when battery cadence requires it.
  bool shouldRedrawForBatteryRefresh() const;
  /// Applies shell layout/config policy to the default inactivity timeout.
  unsigned long inactivityDeepSleepMs(unsigned long defaultMs) const;

  /// Schedules a redraw after external state changes.
  void markDirty();
  /// Repairs the current menu cursor after catalog/category changes.
  void clampMenu();
  /// Central transition point for screen changes and app activation.
  void switchTo(Screen next, ActiveApp *nextApp = nullptr,
                const AppDefinition *nextDefinition = nullptr);

  /// Routes a single menu-button press through dialog, app, and menu behavior.
  void handleMenuButton();
  /// Routes a double menu-button press through active app or shell defaults.
  void handleMenuDoubleButton();
  /// Routes a long menu-button press through active app or shell defaults.
  void handleMenuLongButton();
  /// Routes a single power-button press through app overrides or shell actions.
  void handlePowerSingleButton();
  /// Routes a double power-button press through app overrides or shell actions.
  void handlePowerDoubleButton();
  /// Opens quick settings or delegates long power press to the active app.
  void handlePowerLongButton();

  /// Pulls pending touch input and routes it to the visible screen.
  bool dispatchTouch();
  /// Gives the active app a loop tick and applies its event result.
  bool updateActiveApp();
  /// Bridges file-manager .pink launch requests into app activation.
  bool handlePendingFilesPinkLaunch();
  /// Revalidates active/menu state after built-in or .pink catalog changes.
  void handleCatalogChanged();
  /// Updates the home clock without forcing redraws every loop iteration.
  void refreshHomeMinuteIfNeeded();
  /// Performs the pending full or partial render work.
  void renderIfDirty();

  /// Captures resumable shell state before deep sleep.
  void saveRetainedContextForSleep();
  /// Restores resumable shell state after wake.
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
  bool screenshotPending = false;
  char screenshotNotice[24] = {};
  unsigned long screenshotNoticeUntil = 0;
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
  void requestScreenshot();
  void showScreenshotNotice(const char *message);

  void prepareForPowerTransition();
  void enterSleepClockDeepSleep();
  void renderPowerOffScreen();

  static void prepareForPowerTransitionThunk(void *context);
  static void enterSleepClockDeepSleepThunk(void *context);
  static void renderPowerOffScreenThunk(void *context);
  static void saveRetainedContextThunk(void *context);
  static void requestScreenshotThunk(void *context);

  static void restoreLaunchApp(void *context, const AppDefinition &app);
  static void restoreSwitchTo(void *context, Screen screen);
  static const AppDefinition *restoreActiveDefinition(void *context);
};

#endif
