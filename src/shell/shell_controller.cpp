#include "shell/shell_controller.h"
#include "shell/connectivity.h"
#include "shell/shell_context.h"
#include "sys/battery_monitor.h"
#include "sys/device_clock.h"
#include "sys/environment_monitor.h"
#include "sys/global.h"
#include "sys/pink_executable.h"
#include "sys/power_control.h"
#include "sys/sd_storage.h"
#include "sys/system_config.h"
#include "ui/components/menu_button_consumer.h"
#include "ui/shell_layout.h"
#include <Arduino.h>
#include <cstring>

ShellController::ShellController(AppDisplay &display, TouchInput &touch,
                                 TextInputController &textInput)
    : display(display), touch(touch), textInput(textInput) {}

void ShellController::setHooks(const ShellControllerHooks &nextHooks) {
  hooks = nextHooks;
}

Screen ShellController::currentScreen() const { return screen; }

bool ShellController::shouldRedrawForBatteryRefresh() const {
  return screen == SCREEN_HOME || quickSettings.isOpen() || confirmQuit;
}

unsigned long
ShellController::inactivityDeepSleepMs(unsigned long defaultMs) const {
  if (activeDefinition != nullptr &&
      activeDefinition->inactivityDeepSleepMs > 0) {
    return activeDefinition->inactivityDeepSleepMs;
  }
  return defaultMs;
}

void ShellController::markDirty() { dirty = true; }

void ShellController::clampMenu() { clampMenuState(menuState); }

bool ShellController::sameAppDefinition(
    const AppDefinition *current, const AppDefinition *nextDefinition) const {
  if (current == nullptr || nextDefinition == nullptr) {
    return false;
  }
  if (current->id == nullptr || nextDefinition->id == nullptr) {
    return current == nextDefinition;
  }
  return strcmp(current->id, nextDefinition->id) == 0;
}

void ShellController::storeActiveDefinition(const AppDefinition &definition) {
  activeDefinitionEntry.definition = definition;
  activeDefinitionEntry.id[0] = '\0';
  activeDefinitionEntry.label[0] = '\0';
  activeDefinitionEntry.icon[0] = '\0';

  if (definition.id != nullptr) {
    strncpy(activeDefinitionEntry.id, definition.id,
            sizeof(activeDefinitionEntry.id) - 1);
    activeDefinitionEntry.id[sizeof(activeDefinitionEntry.id) - 1] = '\0';
    activeDefinitionEntry.definition.id = activeDefinitionEntry.id;
  }
  if (definition.label != nullptr) {
    strncpy(activeDefinitionEntry.label, definition.label,
            sizeof(activeDefinitionEntry.label) - 1);
    activeDefinitionEntry.label[sizeof(activeDefinitionEntry.label) - 1] =
        '\0';
    activeDefinitionEntry.definition.label = activeDefinitionEntry.label;
  }
  if (definition.icon != nullptr) {
    strncpy(activeDefinitionEntry.icon, definition.icon,
            sizeof(activeDefinitionEntry.icon) - 1);
    activeDefinitionEntry.icon[sizeof(activeDefinitionEntry.icon) - 1] = '\0';
    activeDefinitionEntry.definition.icon = activeDefinitionEntry.icon;
  }

  activeDefinition = &activeDefinitionEntry.definition;
}

void ShellController::switchTo(Screen next, ActiveApp *nextApp,
                               const AppDefinition *nextDefinition) {
  const bool sameDefinition =
      sameAppDefinition(activeDefinition, nextDefinition);
  const AppDefinition *previousDefinition = activeDefinition;
  if (!activePausedForDialog && previousDefinition != nullptr &&
      !sameDefinition && previousDefinition->behavior.onExit != nullptr) {
    previousDefinition->behavior.onExit();
  }
  if (previousDefinition != nullptr && !sameDefinition &&
      previousDefinition->behavior.onRelease != nullptr) {
    previousDefinition->behavior.onRelease();
  }
  screen = next;
  activeApp = nextApp;
  if (nextDefinition != nullptr) {
    storeActiveDefinition(*nextDefinition);
  } else {
    activeDefinition = nullptr;
    activeDefinitionEntry = AppCatalogEntry{};
  }
  activePausedForDialog = false;
  if (screen == SCREEN_HOME) {
    refreshHome();
  }
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onEnter != nullptr) {
    activeDefinition->behavior.onEnter();
  }
  confirmQuit = false;
  quickSettings.close();
  dirty = true;
}

bool ShellController::isSessionScreen() const {
  return activeApp != nullptr && activeApp->hasActiveSession();
}

void ShellController::noteUserActivity() {
  if (hooks.noteUserActivity != nullptr) {
    hooks.noteUserActivity(hooks.context);
  }
}

void ShellController::refreshHome() {
  if (hooks.refreshHome != nullptr) {
    hooks.refreshHome(hooks.context);
  }
}

void ShellController::openQuitDialog() {
  quickSettings.close();
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onExit != nullptr) {
    activeDefinition->behavior.onExit();
    activePausedForDialog = true;
  }
  confirmQuit = true;
  quitDialogOpenedAt = millis();
  dirty = true;
}

void ShellController::cancelQuitDialog() {
  confirmQuit = false;
  if (activePausedForDialog && activeDefinition != nullptr &&
      activeDefinition->behavior.onEnter != nullptr) {
    activeDefinition->behavior.onEnter();
  }
  activePausedForDialog = false;
  dirty = true;
}

void ShellController::openQuickSettings() {
  if (confirmQuit) {
    cancelQuitDialog();
  }
  quickSettings.open();
  dirty = true;
}

void ShellController::closeQuickSettings() {
  quickSettings.close();
  dirty = true;
}

bool ShellController::applyAppEventResult(AppEventResult result) {
  switch (result) {
  case AppEventResult::Unhandled:
    return false;
  case AppEventResult::Handled:
    return true;
  case AppEventResult::Dirty:
    dirty = true;
    return true;
  case AppEventResult::QuitRequested:
    openQuitDialog();
    return true;
  case AppEventResult::GoHome:
    switchTo(SCREEN_HOME);
    return true;
  case AppEventResult::GoMenu:
    switchTo(SCREEN_MENU);
    return true;
  case AppEventResult::Reboot:
    prepareForPowerTransition();
    rebootDevice();
    return true;
  }
  return false;
}

bool ShellController::handleAppEvent(AppEventHandler handler) {
  if (handler == nullptr) {
    return false;
  }
  return applyAppEventResult(handler());
}

void ShellController::handleMenuButton() {
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (confirmQuit) {
    return;
  }
  if (screen == SCREEN_HOME) {
    launchMenuButtonApp();
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onMenu)) {
    return;
  }
  if (handleActiveMenuButton()) {
    dirty = true;
    return;
  }
  if (textInput.toggleCaps(screen)) {
    dirty = true;
    return;
  }
  if (isSessionScreen()) {
    openQuitDialog();
    return;
  }
  switchTo(screen == SCREEN_MENU ? SCREEN_HOME : SCREEN_MENU);
}

void ShellController::handleMenuDoubleButton() {
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (confirmQuit) {
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onMenuDouble)) {
    return;
  }
  if (handleActiveMenuDoubleButton()) {
    dirty = true;
    return;
  }
  if (textInput.isScreen(screen)) {
    switchKeyboardMode();
  }
}

void ShellController::handleMenuLongButton() {
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (confirmQuit) {
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onMenuLong)) {
    return;
  }
  if (handleActiveMenuLongButton()) {
    dirty = true;
    return;
  }
  if (textInput.isScreen(screen)) {
    return;
  }
  if (isSessionScreen()) {
    openQuitDialog();
    return;
  }
  if (activeApp != nullptr) {
    switchTo(SCREEN_MENU);
  }
}

void ShellController::switchKeyboardMode() {
  switchTo(textInput.nextMode(screen));
}

void ShellController::handlePowerButton(AppEventHandler overrideHandler) {
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (screen == SCREEN_HOME) {
    switchTo(SCREEN_MENU);
    return;
  }
  if (screen == SCREEN_MENU) {
    switchTo(SCREEN_HOME);
    return;
  }
  if (handleAppEvent(overrideHandler)) {
    return;
  }

  if (activeApp != nullptr) {
    return;
  }
  switchKeyboardMode();
}

void ShellController::handlePowerSingleButton() {
  AppEventHandler handler = nullptr;
  if (activeDefinition != nullptr) {
    handler = activeDefinition->behavior.onPower;
  }
  handlePowerButton(handler);
}

void ShellController::handlePowerDoubleButton() {
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (confirmQuit) {
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onPowerDouble)) {
    return;
  }
  if (textInput.isScreen(screen)) {
    return;
  }
  saveRetainedContextForSleep();
  enterSleepClockDeepSleep();
}

void ShellController::handlePowerLongButton() {
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (confirmQuit) {
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onPowerLong)) {
    return;
  }
  openQuickSettings();
}

ShellData ShellController::currentShellData(char *dateText, size_t dateSize,
                                            char *timeText, size_t timeSize) {
  dateText[0] = '\0';
  timeText[0] = '\0';
  if (deviceClock.isSet()) {
    deviceClock.formatDate(dateText, dateSize);
    deviceClock.formatTime(timeText, timeSize);
  }

  const BatterySnapshot &battery = batteryMonitor.snapshot();
  const EnvironmentSnapshot &env = environmentMonitor.snapshot();
  const SdStorageSnapshot &sd = sdStorageSnapshot();

  ShellData data = {};
  data.status.dateText = dateText;
  data.status.timeText = timeText;
  data.status.battery = &battery;
  data.status.wifiIcon = wifiStatusIcon();
  data.battery = &battery;
  data.environment = &env;
  data.sd = &sd;
  return data;
}

void ShellController::drawHome() {
  char dateText[16];
  char timeText[16];
  ShellData data =
      currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));
  drawHomeScreen(display, data);
}

bool ShellController::handleQuitDialogTouch(const TouchPoint &point) {
  if (millis() - quitDialogOpenedAt < 350) {
    return true;
  }
  if (quitDialogHitYes(point)) {
    switchTo(SCREEN_MENU);
    return true;
  }
  if (quitDialogHitNo(point)) {
    cancelQuitDialog();
    return true;
  }
  return true;
}

QuickSettingsActions ShellController::quickSettingsActions() {
  QuickSettingsActions actions;
  actions.context = this;
  actions.showActionFeedback = showQuickSettingsActionFeedbackThunk;
  actions.renderPowerOff = renderPowerOffScreenThunk;
  actions.prepareForPowerTransition = prepareForPowerTransitionThunk;
  actions.saveRetainedContext = saveRetainedContextThunk;
  actions.enterSleepClock = enterSleepClockDeepSleepThunk;
  return actions;
}

bool ShellController::handleQuickSettingsTouch(const TouchPoint &point) {
  const bool handled = quickSettings.handleTouch(point, quickSettingsActions());
  if (handled) {
    dirty = true;
  }
  return handled;
}

void ShellController::drawMenu() {
  clampMenuState(menuState);
  drawAppMenu(display, menuState, menuPressedSlot);
}

void ShellController::launchApp(const AppDefinition &app) {
  if (app.reset != nullptr) {
    app.reset();
  }
  if (app.runtime == pinkExecutableRuntime() &&
      !pinkExecutableLaunchById(app.id)) {
    return;
  }
  if (app.launch != nullptr) {
    app.launch();
  }
  switchTo(SCREEN_APP, app.runtime, &app);
}

bool ShellController::launchAppById(const char *id) {
  AppCatalogEntry app;
  if (!findAppById(id, app)) {
    return false;
  }
  launchApp(app.definition);
  return true;
}

void ShellController::launchMenuButtonApp() {
  const char *target = systemConfig().home.sunBtnApp;
  if (target != nullptr && target[0] != '\0' && launchAppById(target)) {
    return;
  }
  launchAppById("contact_links");
}

void ShellController::handleMenuTouch(const TouchPoint &point) {
  bool stateChanged = false;
  int8_t hitSlot = -1;
  AppCatalogEntry selected;
  const bool hasSelection =
      hitTestAppMenu(point, menuState, selected, stateChanged, &hitSlot);
  if (stateChanged) {
    dirty = true;
  }
  if (hasSelection) {
    menuPressedSlot = hitSlot;
    redrawActiveScreenPartial();
    delay(MENU_PRESS_HIGHLIGHT_MS);
    menuPressedSlot = -1;
    launchApp(selected.definition);
  }
}

bool ShellController::handlePendingFilesPinkLaunch() {
  char path[288];
  AppCatalogEntry app;
  if (!consumeFilesPinkLaunch(path, sizeof(path))) {
    return false;
  }
  if (!pinkExecutableLaunchPath(path, &app.definition, app.id, sizeof(app.id),
                                app.label, sizeof(app.label), app.icon,
                                sizeof(app.icon))) {
    return false;
  }
  switchTo(SCREEN_APP, app.definition.runtime, &app.definition);
  return true;
}

void ShellController::drawActiveScreen() {
  if (activeApp != nullptr) {
    activeApp->draw(display);
    return;
  }

  switch (screen) {
  case SCREEN_HOME:
    drawHome();
    break;
  case SCREEN_KEYBOARD:
  case SCREEN_T9_KEYBOARD:
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    textInput.draw(display, screen);
    break;
  case SCREEN_MENU:
    drawMenu();
    break;
  default:
    break;
  }
}

void ShellController::redrawActiveScreenPartial() {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = EPD_WIDTH;
  int16_t h = EPD_HEIGHT;
  const bool hasRegion =
      activeApp != nullptr && activeApp->consumeDirtyRegion(&x, &y, &w, &h);

  display.lock();
  if (hasRegion) {
    display.fillRect(x, y, w, h, 0);
    activeApp->draw(display);
    display.flushPartial(x, y, w, h);
  } else {
    display.clear();
    drawActiveScreen();
    display.flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
  }
  display.unlock();
}

bool ShellController::handleScreenTouch(const TouchPoint &point) {
  if (activeApp != nullptr) {
    return activeApp->handleTouch(point);
  }

  switch (screen) {
  case SCREEN_KEYBOARD:
  case SCREEN_T9_KEYBOARD:
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    return textInput.handleTouch(screen, point);
  case SCREEN_MENU:
    handleMenuTouch(point);
    return false;
  default:
    return false;
  }
}

bool ShellController::dispatchTouch() {
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onRawTouch != nullptr && !confirmQuit &&
      !quickSettings.isOpen()) {
    TouchEvent event;
    while (touch.readEvent(event)) {
      noteUserActivity();
      activeDefinition->behavior.onRawTouch(event);
    }
    return false;
  }

  TouchPoint point;
  if (!touch.read(point)) {
    return false;
  }

  noteUserActivity();
  if (quickSettings.isOpen()) {
    handleQuickSettingsTouch(point);
    return true;
  }
  if (confirmQuit) {
    handleQuitDialogTouch(point);
    return true;
  }
  const bool screenTouched = handleScreenTouch(point);
  if (handlePendingFilesPinkLaunch()) {
    return true;
  }
  if (screenTouched) {
    redrawActiveScreenPartial();
  }
  return false;
}

void ShellController::render() {
  display.lock();
  display.clear();
  drawActiveScreen();
  if (confirmQuit) {
    char dateText[16];
    char timeText[16];
    ShellData data =
        currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));
    drawQuitDialog(display, data.status);
  } else if (quickSettings.isOpen()) {
    char dateText[16];
    char timeText[16];
    ShellData data =
        currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));
    drawPowerDialog(display, data.status, quickSettings.snapshot());
  }
  display.flush();
  display.unlock();
  dirty = false;
  if (screen == SCREEN_HOME) {
    lastHomeMinute = deviceClock.localMinuteIndex();
  }
}

void ShellController::renderIfDirty() {
  if (dirty) {
    render();
  }
}

void ShellController::showQuickSettingsActionFeedback(PowerDialogAction action) {
  char dateText[16];
  char timeText[16];
  ShellData data =
      currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));

  display.lock();
  display.clear();
  drawActiveScreen();
  drawPowerDialog(display, data.status, quickSettings.snapshot(), action);
  display.flush();
  display.unlock();
  delay(180);
}

bool ShellController::updateActiveApp() {
  if (activeApp != nullptr && activeApp->update()) {
    dirty = true;
    return true;
  }
  return false;
}

void ShellController::handleCatalogChanged() {
  clampMenuState(menuState);
  dirty = true;
}

void ShellController::refreshHomeMinuteIfNeeded() {
  const int64_t currentHomeMinute = deviceClock.localMinuteIndex();
  if (screen == SCREEN_HOME && currentHomeMinute >= 0 &&
      currentHomeMinute != lastHomeMinute) {
    refreshHome();
    lastHomeMinute = currentHomeMinute;
    dirty = true;
  }
}

void ShellController::saveRetainedContextForSleep() {
  shellContextSaveForSleep(screen, textInput.isScreen(screen), activeDefinition,
                           menuState);
}

void ShellController::restoreRetainedContextAfterSleep() {
  ShellContextRestoreActions actions;
  actions.context = this;
  actions.launchApp = restoreLaunchApp;
  actions.switchTo = restoreSwitchTo;
  actions.activeDefinition = restoreActiveDefinition;
  if (shellContextRestoreAfterSleep(menuState, actions)) {
    dirty = true;
  }
  clampMenuState(menuState);
}

void ShellController::prepareForPowerTransition() {
  if (hooks.prepareForPowerTransition != nullptr) {
    hooks.prepareForPowerTransition(hooks.context);
  }
}

void ShellController::enterSleepClockDeepSleep() {
  if (hooks.enterSleepClockDeepSleep != nullptr) {
    hooks.enterSleepClockDeepSleep(hooks.context);
  }
}

void ShellController::renderPowerOffScreen() {
  if (hooks.renderPowerOffScreen != nullptr) {
    hooks.renderPowerOffScreen(hooks.context);
  }
}

void ShellController::showQuickSettingsActionFeedbackThunk(
    void *context, PowerDialogAction action) {
  static_cast<ShellController *>(context)->showQuickSettingsActionFeedback(
      action);
}

void ShellController::prepareForPowerTransitionThunk(void *context) {
  static_cast<ShellController *>(context)->prepareForPowerTransition();
}

void ShellController::enterSleepClockDeepSleepThunk(void *context) {
  static_cast<ShellController *>(context)->enterSleepClockDeepSleep();
}

void ShellController::renderPowerOffScreenThunk(void *context) {
  static_cast<ShellController *>(context)->renderPowerOffScreen();
}

void ShellController::saveRetainedContextThunk(void *context) {
  static_cast<ShellController *>(context)->saveRetainedContextForSleep();
}

void ShellController::restoreLaunchApp(void *context,
                                       const AppDefinition &app) {
  static_cast<ShellController *>(context)->launchApp(app);
}

void ShellController::restoreSwitchTo(void *context, Screen nextScreen) {
  static_cast<ShellController *>(context)->switchTo(nextScreen);
}

const AppDefinition *ShellController::restoreActiveDefinition(void *context) {
  return static_cast<ShellController *>(context)->activeDefinition;
}
