#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/builtin_apps.h"
#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif
#include "ui/components/menu_button_consumer.h"
#include "sys/shell_buttons.h"
#include "sys/touch_input.h"
#include "ui/shell_layout.h"
#include "ui/text_input_controller.h"
#include "sys/device_clock.h"
#include "sys/battery_monitor.h"
#include "sys/environment_monitor.h"
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

AppDisplay display;
TouchInput touch;
TextInputController textInput;

Screen screen = SCREEN_HOME;
ActiveApp *activeApp = nullptr;
const AppDefinition *activeDefinition = nullptr;
bool dirty = true;
bool confirmQuit = false;
bool activePausedForDialog = false;
MenuState menuState = {MENU_GAMES, 0};
unsigned long quitDialogOpenedAt = 0;
int64_t lastHomeMinute = -1;

void switchTo(Screen next, ActiveApp *nextApp = nullptr,
              const AppDefinition *nextDefinition = nullptr) {
  if (!activePausedForDialog && activeDefinition != nullptr &&
      activeDefinition != nextDefinition &&
      activeDefinition->behavior.onExit != nullptr) {
    activeDefinition->behavior.onExit();
  }
  screen = next;
  activeApp = nextApp;
  activeDefinition = nextDefinition;
  activePausedForDialog = false;
  if (screen == SCREEN_HOME) {
    batteryMonitor.refresh();
    environmentMonitor.refresh();
  }
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onEnter != nullptr) {
    activeDefinition->behavior.onEnter();
  }
  confirmQuit = false;
  dirty = true;
}

void switchKeyboardMode();

bool isSessionScreen() {
  return activeApp != nullptr && activeApp->hasActiveSession();
}

void openQuitDialog() {
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onExit != nullptr) {
    activeDefinition->behavior.onExit();
    activePausedForDialog = true;
  }
  confirmQuit = true;
  quitDialogOpenedAt = millis();
  dirty = true;
}

bool applyAppEventResult(AppEventResult result) {
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
    ESP.restart();
    return true;
  }
  return false;
}

bool handleAppEvent(AppEventHandler handler) {
  if (handler == nullptr) {
    return false;
  }
  return applyAppEventResult(handler());
}

void handleMenuButton() {
  if (confirmQuit) {
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

void handleMenuDoubleButton() {
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

void handleMenuLongButton() {
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

void switchKeyboardMode() {
  switchTo(textInput.nextMode(screen));
}

void handlePowerButton(AppEventHandler overrideHandler) {
  if (screen == SCREEN_HOME) {
    resetContactLinks();
    switchTo(SCREEN_CONTACT_LINKS, contactLinksRuntime());
    return;
  }
  if (screen == SCREEN_CONTACT_LINKS) {
    switchTo(SCREEN_HOME);
    return;
  }

  if (handleAppEvent(overrideHandler)) {
    return;
  }

  if (activeApp != nullptr) {
    return;
  }
  if (screen == SCREEN_MENU) {
    return;
  }
  switchKeyboardMode();
}

void handlePowerSingleButton() {
  AppEventHandler handler = nullptr;
  if (activeDefinition != nullptr) {
    handler = activeDefinition->behavior.onPower;
  }
  handlePowerButton(handler);
}

void handlePowerDoubleButton() {
  AppEventHandler handler = nullptr;
  if (activeDefinition != nullptr) {
    handler = activeDefinition->behavior.onPowerDouble;
  }
  handlePowerButton(handler);
}

void handlePowerLongButton() {
  if (activeDefinition != nullptr &&
      handleAppEvent(activeDefinition->behavior.onPowerLong)) {
    return;
  }
  applyAppEventResult(AppEventResult::Reboot);
}

ShellData currentShellData(char *dateText, size_t dateSize, char *timeText,
                           size_t timeSize) {
  dateText[0] = '\0';
  timeText[0] = '\0';
  if (deviceClock.isSet()) {
    deviceClock.formatDate(dateText, dateSize);
    deviceClock.formatTime(timeText, timeSize);
  }

  const BatterySnapshot &battery = batteryMonitor.snapshot();
  const EnvironmentSnapshot &env = environmentMonitor.snapshot();

  ShellData data = {};
  data.status.dateText = dateText;
  data.status.timeText = timeText;
  data.status.battery = &battery;
  data.status.wifiIcon = wifiStatusIcon();
  data.battery = &battery;
  data.environment = &env;
  return data;
}

void drawHome() {
  char dateText[16];
  char timeText[16];
  ShellData data =
      currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));
  drawHomeScreen(display, data);
}

bool handleQuitDialogTouch(const TouchPoint &point) {
  if (millis() - quitDialogOpenedAt < 350) {
    return true;
  }
  if (quitDialogHitYes(point)) {
    switchTo(SCREEN_MENU);
    return true;
  }
  if (quitDialogHitNo(point)) {
    confirmQuit = false;
    if (activePausedForDialog && activeDefinition != nullptr &&
        activeDefinition->behavior.onEnter != nullptr) {
      activeDefinition->behavior.onEnter();
    }
    activePausedForDialog = false;
    dirty = true;
    return true;
  }
  return true;
}

void drawMenu() {
  clampMenuState(menuState, apps, appCount);
  drawAppMenu(display, menuState, apps, appCount);
}

void launchApp(const AppDefinition &app) {
  if (app.reset != nullptr) {
    app.reset();
  }
  if (app.launch != nullptr) {
    app.launch();
  }
  switchTo(app.screen, app.runtime, &app);
}

void handleMenuTouch(const TouchPoint &point) {
  bool stateChanged = false;
  const AppDefinition *selected =
      hitTestAppMenu(point, menuState, apps, appCount, stateChanged);
  if (stateChanged) {
    dirty = true;
  }
  if (selected != nullptr) {
    launchApp(*selected);
  }
}

void drawActiveScreen() {
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

void redrawActiveScreenPartial() {
  display.lock();
  display.clear();
  drawActiveScreen();
  display.flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
  display.unlock();
}

bool handleScreenTouch(const TouchPoint &point) {
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

void render() {
  display.lock();
  display.clear();
  drawActiveScreen();
  if (confirmQuit) {
    drawQuitDialog(display);
  }
  display.flush();
  display.unlock();
  dirty = false;
  if (screen == SCREEN_HOME) {
    lastHomeMinute = deviceClock.localMinuteIndex();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  display.begin();
  touch.begin();
  batteryMonitor.refresh();
  environmentMonitor.refresh();

  shellButtonsBegin({handleMenuButton, handleMenuDoubleButton,
                     handleMenuLongButton, handlePowerSingleButton,
                     handlePowerDoubleButton, handlePowerLongButton});

  resetApps();
  dirty = true;
}

void loop() {
  shellButtonsDispatch();

  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onRawTouch != nullptr && !confirmQuit) {
    TouchEvent event;
    while (touch.readEvent(event)) {
      activeDefinition->behavior.onRawTouch(event);
    }
  } else {
    TouchPoint point;
    if (touch.read(point)) {
      if (confirmQuit) {
        handleQuitDialogTouch(point);
        delay(1);
        return;
      }
      if (handleScreenTouch(point)) {
        redrawActiveScreenPartial();
      }
    }
  }

  if (dirty) {
    render();
  }

  if (activeApp != nullptr && activeApp->update()) {
    dirty = true;
  }

  int64_t currentHomeMinute = deviceClock.localMinuteIndex();
  if (screen == SCREEN_HOME && currentHomeMinute >= 0 &&
      currentHomeMinute != lastHomeMinute) {
    batteryMonitor.refresh();
    environmentMonitor.refresh();
    lastHomeMinute = currentHomeMinute;
    dirty = true;
  }

  if (dirty) {
    render();
  }
  delay(1);
}
