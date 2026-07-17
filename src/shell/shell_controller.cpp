#include "shell/shell_controller.h"
#include "sys/services/connectivity_service.h"
#include "shell/shell_dialog_layout.h"
#include "shell/shell_home_layout.h"
#include "shell/shell_menu_layout.h"
#include "shell/shell_context.h"
#include "shell/toast.h"
#include "sys/services/battery_monitor.h"
#include "sys/agentfiesta/agentfiesta.h"
#include "sys/ext_app_api/app_api.h"
#include "sys/services/device_clock.h"
#include "sys/services/environment_monitor.h"
#include "sys/global.h"
#include "sys/pink_executable.h"
#include "sys/pocketinkjs_app.h"
#include "sys/services/power_control.h"
#include "shell/screenshot.h"
#include "sys/services/sd_storage.h"
#include "sys/video/record.h"
#include "sys/system_config.h"
#include "sys/drivers/usb_status.h"
#include <Arduino.h>
#include <cstring>

namespace {

constexpr uint8_t kMaxRawTouchEventsPerDispatch = 16;

} // namespace

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
  if (videoRecording()) {
    return 0;
  }
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
  if (filesLaunchReturnOwner != nullptr &&
      activeApp == filesLaunchReturnOwner && nextApp != activeApp) {
    filesLaunchReturnOwner = nullptr;
  }
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
  appOverlayVisible = appApiOverlayActive();
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
  if (appApiOverlayActive()) {
    if (appApiHandleOverlayMenuButton()) {
      dirty = true;
    }
    return;
  }
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
  if (activeApp == PocketInkJSAppRuntime() &&
      returnablePocketInkJsReturnPending()) {
    returnFromReturnablePocketInkJs();
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onMenu)) {
    return;
  }
  if (returnToFilesAfterLaunch()) {
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
  if (appApiOverlayActive()) {
    return;
  }
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
  if (textInput.isScreen(screen)) {
    switchKeyboardMode();
  }
}

void ShellController::handleMenuLongButton() {
  if (appApiOverlayActive()) {
    return;
  }
  if (quickSettings.isOpen()) {
    closeQuickSettings();
    return;
  }
  if (confirmQuit) {
    return;
  }
  if (activeApp == PocketInkJSAppRuntime() &&
      returnablePocketInkJsReturnPending()) {
    returnFromReturnablePocketInkJs();
    return;
  }
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onMenuLong)) {
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
  if (appApiOverlayActive()) {
    if (appApiHandleOverlayPowerButton()) {
      dirty = true;
    }
    return;
  }
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
  if (appApiOverlayActive()) {
    return;
  }
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
  if (appApiOverlayActive()) {
    return;
  }
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
  const SdStorageSnapshot &sd = currentSdStorageSnapshot();

  ShellData data = {};
  data.status.dateText = dateText;
  data.status.timeText = timeText;
  data.status.battery = &battery;
  data.status.wifiIcon = wifiStatusIcon();
  data.status.usbConnected = usbConnected();
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
    if (returnToFilesAfterLaunch()) {
      return true;
    }
    switchTo(SCREEN_MENU);
    return true;
  }
  if (quitDialogHitNo(point)) {
    cancelQuitDialog();
    return true;
  }
  return true;
}

bool ShellController::returnToFilesAfterLaunch() {
  if (filesLaunchReturnOwner == nullptr || activeApp != filesLaunchReturnOwner) {
    return false;
  }
  AppCatalogEntry files;
  if (!findAppById("files", files)) {
    filesLaunchReturnOwner = nullptr;
    return false;
  }
  filesLaunchReturnOwner = nullptr;
  // Do not call launchApp(): Files deliberately keeps its path and selection
  // across release(), and reset() would discard the folder we are returning to.
  switchTo(SCREEN_APP, files.definition.runtime, &files.definition);
  return true;
}

QuickSettingsActions ShellController::quickSettingsActions() {
  QuickSettingsActions actions;
  actions.context = this;
  actions.renderPowerOff = renderPowerOffScreenThunk;
  actions.prepareForPowerTransition = prepareForPowerTransitionThunk;
  actions.saveRetainedContext = saveRetainedContextThunk;
  actions.enterSleepClock = enterSleepClockDeepSleepThunk;
  actions.captureScreenshot = requestScreenshotThunk;
  actions.toggleVideoRecording = requestVideoToggleThunk;
  actions.toggleScreenInversion = toggleScreenInversionThunk;
  return actions;
}

bool ShellController::handleQuickSettingsTouch(const TouchPoint &point) {
  const PowerDialogAction action = quickSettings.handleTouch(point);
  if (action != PowerDialogAction::None) {
    showQuickSettingsActionFeedback(action);
    quickSettings.runAction(action, quickSettingsActions());
  }
  dirty = true;
  return true;
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

bool ShellController::wakeAppById(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }
  if (activeDefinition != nullptr && activeDefinition->id != nullptr &&
      strcmp(activeDefinition->id, id) == 0) {
    dirty = true;
    return true;
  }
  return launchAppById(id);
}

void ShellController::launchMenuButtonApp() {
  const char *target = currentSystemConfig().home.sunBtnApp;
  if (target != nullptr && target[0] != '\0') {
    launchAppById(target);
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
  if (takePendingPinkExecLaunchPath(path, sizeof(path))) {
    if (!pinkExecutableLaunchPath(path, &app.definition, app.id, sizeof(app.id),
                                  app.label, sizeof(app.label), app.icon,
                                  sizeof(app.icon))) {
      return false;
    }
  } else {
    if (!takePendingPocketInkJSLaunchPath(path, sizeof(path))) {
      return false;
    }
    if (!PocketInkJSAppLaunchPath(path, &app.definition, app.id, sizeof(app.id),
                            app.label, sizeof(app.label), app.icon,
                            sizeof(app.icon))) {
      return false;
    }
  }
  filesLaunchReturnOwner = app.definition.runtime;
  switchTo(SCREEN_APP, app.definition.runtime, &app.definition);
  return true;
}

bool ShellController::launchPendingReturnablePocketInkJs() {
  if (!returnablePocketInkJsRequested()) {
    return false;
  }
  char path[288] = {};
  AppCatalogEntry app;
  if (!returnablePocketInkJsTakeSource(path, sizeof(path))) {
    returnablePocketInkJsFinish();
    dirty = true;
    return true;
  }

  // The opaque state is already in PSRAM, so release the owner before the VM
  // asks for its workspace. A failed launch immediately reconstructs it.
  pinkExecutableStop();
  char launchError[120] = {};
  if (!PocketInkJSAppLaunchTestPath(path, &app.definition, app.id,
                                    sizeof(app.id), app.label,
                                    sizeof(app.label), app.icon,
                                    sizeof(app.icon), launchError,
                                    sizeof(launchError))) {
    returnablePocketInkJsSetError(
        launchError[0] != '\0' ? launchError : "Test failed");
    returnFromReturnablePocketInkJs();
    dirty = true;
    return true;
  }
  switchTo(SCREEN_APP, app.definition.runtime, &app.definition);
  return true;
}

bool ShellController::returnFromReturnablePocketInkJs() {
  char path[288] = {};
  const uint8_t *state = nullptr;
  size_t stateSize = 0;
  if (!returnablePocketInkJsOwnerPath(path, sizeof(path)) ||
      !returnablePocketInkJsState(state, stateSize)) {
    returnablePocketInkJsFinish();
    return false;
  }

  PocketInkJSAppStop();
  AppCatalogEntry app;
  if (!pinkExecutableLaunchPathChecked(
          path, &app.definition, app.id, sizeof(app.id), app.label,
          sizeof(app.label), app.icon, sizeof(app.icon))) {
    returnablePocketInkJsFinish();
    return false;
  }
  switchTo(SCREEN_APP, app.definition.runtime, &app.definition);
  pinkExecutableRestoreState(state, stateSize);
  returnablePocketInkJsFinish();
  dirty = true;
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
  if (!display.ready()) {
    dirty = true;
    return;
  }
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
    if (!display.flushPartialAsync()) {
      display.flushPartial();
    }
  } else {
    display.clear();
    drawActiveScreen();
    if (!display.flushPartialAsync()) {
      display.flushPartial();
    }
  }
  display.unlock();
}

bool ShellController::handleScreenTouch(const TouchPoint &point) {
  if (activeApp != nullptr) {
    return activeApp->handleTouch(point);
  }

  switch (screen) {
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
  if (appApiOverlayActive()) {
    TouchPoint point;
    if (!touch.read(point)) {
      return false;
    }
    noteUserActivity();
    appApiHandleOverlayTouch(point);
    appOverlayVisible = appApiOverlayActive();
    if (!appOverlayVisible) {
      TouchPoint discarded;
      uint8_t discardedCount = 0;
      while (discardedCount < kMaxRawTouchEventsPerDispatch &&
             touch.read(discarded)) {
        discardedCount++;
      }
    }
    dirty = true;
    return true;
  }
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onRawTouch != nullptr && !confirmQuit &&
      !quickSettings.isOpen()) {
    TouchEvent event;
    uint8_t handled = 0;
    // The core-0 producer can refill this queue while a raw handler is slow.
    while (handled < kMaxRawTouchEventsPerDispatch && touch.readEvent(event)) {
      noteUserActivity();
      activeDefinition->behavior.onRawTouch(event);
      handled++;
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
  if (appApiOverlayActive()) {
    appOverlayVisible = true;
    dirty = true;
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
  if (appApiOverlayActive()) {
    appApiDrawOverlay(display);
  } else if (confirmQuit) {
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
  drawShellToast(display);
  if (!display.flushAsync()) {
    display.flush();
  }
  bool redrawForShellNotice = false;
  if (screenshotPending) {
    const ScreenshotResult result = saveFramebufferScreenshot(display);
    screenshotPending = false;
    const char *message = screenshotResultMessage(result);
    if (message[0] != '\0') {
      showShellToast(message);
      redrawForShellNotice = true;
    }
  }
  if (videoTogglePending) {
    const VideoRecordResult result = videoRecordToggle(display);
    videoTogglePending = false;
    const char *message = videoRecordResultMessage(result);
    if (message[0] != '\0') {
      showShellToast(message);
      redrawForShellNotice = true;
    }
  }
  display.unlock();
  dirty = redrawForShellNotice;
  if (screen == SCREEN_HOME) {
    lastHomeMinute = deviceClock.localMinuteIndex();
  }
}

void ShellController::renderIfDirty() {
  if (updateShellToast()) {
    dirty = true;
  }
  if (dirty && display.ready()) {
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

void ShellController::requestScreenshot() {
  quickSettings.close();
  screenshotPending = true;
  dirty = true;
}

void ShellController::requestVideoToggle() {
  quickSettings.close();
  videoTogglePending = true;
  dirty = true;
}

void ShellController::toggleScreenInversion() {
  display.toggleInverted();
  dirty = true;
}

void ShellController::updateVideoRecording() {
  if (!videoRecording()) {
    return;
  }
  display.lock();
  const VideoRecordResult result = videoRecordUpdate(display);
  display.unlock();
  if (result == VideoRecordResult::None) {
    return;
  }
  showShellToast(videoRecordResultMessage(result));
  noteUserActivity();
  dirty = true;
}

bool ShellController::updateActiveApp() {
  if (activeApp == nullptr) {
    return false;
  }

  if (appApiUpdateOnMainCore()) {
    dirty = true;
  }
  bool handled = false;
  if (activeApp->update()) {
    dirty = true;
    handled = true;
  }
  if (applyAppEventResult(activeApp->consumePendingEvent())) {
    handled = true;
  }
  if (launchPendingReturnablePocketInkJs()) {
    return true;
  }
  const bool overlayActive = appApiOverlayActive();
  if (overlayActive != appOverlayVisible) {
    appOverlayVisible = overlayActive;
    dirty = true;
    handled = true;
  }
  char requestedApp[24];
  if (appApiTakeOpenAppRequest(requestedApp, sizeof(requestedApp))) {
    if (launchAppById(requestedApp)) {
      handled = true;
    } else {
      agentfiesta::failHandoff("Agent handoff target is not installed");
      dirty = true;
    }
  }
  return handled;
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
  if (activeDefinition != nullptr) {
    if (activeDefinition->behavior.onExit != nullptr) {
      activeDefinition->behavior.onExit();
    }
    if (activeDefinition->behavior.onRelease != nullptr) {
      activeDefinition->behavior.onRelease();
    }
    activeDefinition = nullptr;
    activeApp = nullptr;
  }
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

void ShellController::requestScreenshotThunk(void *context) {
  static_cast<ShellController *>(context)->requestScreenshot();
}

void ShellController::requestVideoToggleThunk(void *context) {
  static_cast<ShellController *>(context)->requestVideoToggle();
}

void ShellController::toggleScreenInversionThunk(void *context) {
  static_cast<ShellController *>(context)->toggleScreenInversion();
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
