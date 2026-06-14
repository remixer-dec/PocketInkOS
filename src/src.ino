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
#include "sys/audio_power.h"
#include "sys/device_clock.h"
#include "sys/battery_monitor.h"
#include "sys/device_controls.h"
#include "sys/environment_monitor.h"
#include "sys/power_control.h"
#include "sys/pcf85063_clock.h"
#include "sys/rtc_context.h"
#include "sys/sleep_clock_context.h"
#include "sys/sd_storage.h"
#include "sys/usb_status.h"
#include <Arduino.h>
#include <cstring>
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
bool confirmPower = false;
bool activePausedForDialog = false;
MenuState menuState = {MENU_GAMES, 0};
int8_t menuPressedSlot = -1;
PowerDialogPage powerDialogPage = PowerDialogPage::Power;
unsigned long quitDialogOpenedAt = 0;
unsigned long powerDialogOpenedAt = 0;
int64_t lastHomeMinute = -1;
RtcContextSnapshot retainedSleepContext;

static const unsigned long MENU_PRESS_HIGHLIGHT_MS = 180;
static const unsigned long BATTERY_REFRESH_INTERVAL_MS = 30000;
static const unsigned long DEFAULT_INACTIVITY_DEEP_SLEEP_MS = 120000;
static const bool SLEEP_CLOCK_RETAIN_EPD_POWER = false;

bool batteryShutdownStarted = false;
bool inactivitySleepStarted = false;
unsigned long lastBatteryRefreshAt = 0;
unsigned long lastUserActivityAt = 0;

bool batteryCutoffReached();
void checkInactivitySleep();
void enterInactivityDeepSleep();
unsigned long inactivityDeepSleepMs();
void noteUserActivity();
void refreshBatteryAndCheckCutoff();
void shutdownForLowBattery();

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
    refreshBatteryAndCheckCutoff();
    environmentMonitor.refresh();
  }
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onEnter != nullptr) {
    activeDefinition->behavior.onEnter();
  }
  confirmQuit = false;
  confirmPower = false;
  dirty = true;
}

void switchKeyboardMode();
PowerDialogSnapshot currentPowerDialogSnapshot();
PowerDialogPage previousPowerDialogPage(PowerDialogPage page);
PowerDialogPage nextPowerDialogPage(PowerDialogPage page);
void renderPowerDialogPressed(PowerDialogAction action);
void renderLowBatteryScreen();
void renderPowerOffScreen();
void renderDeepSleepScreen();
void renderSleepClockScreen(bool partialUpdate = false);
void seedSleepClockPreviousFrame(const SleepClockSnapshot &sleepClock);
uint16_t sleepClockWakeIntervalSeconds(bool clockSet, int64_t localUnix);
uint64_t sleepClockWakeIntervalUs(uint16_t wakeIntervalSeconds);
bool syncDeviceClockFromRtc();
bool waitForRtcMinuteChangeIfNeeded();
void redrawActiveScreenPartial();
void prepareForPowerTransition();
void saveRetainedContextForSleep();
void restoreRetainedContextAfterSleep();
void enterSleepClockDeepSleep();
bool handleSleepClockTimerWake();

bool isSessionScreen() {
  return activeApp != nullptr && activeApp->hasActiveSession();
}

void noteUserActivity() { lastUserActivityAt = millis(); }

bool batteryCutoffReached() {
  const BatterySnapshot &battery = batteryMonitor.snapshot();
  return battery.valid && battery.voltage <= BATTERY_EMPTY_VOLTAGE;
}

void shutdownForLowBattery() {
  if (batteryShutdownStarted) {
    return;
  }
  batteryShutdownStarted = true;
  renderLowBatteryScreen();
  prepareForPowerTransition();
  powerOffDevice();
}

void refreshBatteryAndCheckCutoff() {
  batteryMonitor.refresh();
  lastBatteryRefreshAt = millis();
  if (batteryCutoffReached()) {
    shutdownForLowBattery();
  }
}

void enterInactivityDeepSleep() {
  if (batteryShutdownStarted || inactivitySleepStarted) {
    return;
  }
  if (usbDataConnected()) {
    noteUserActivity();
    return;
  }
  inactivitySleepStarted = true;
  saveRetainedContextForSleep();
  enterSleepClockDeepSleep();
}

unsigned long inactivityDeepSleepMs() {
  if (activeDefinition != nullptr &&
      activeDefinition->inactivityDeepSleepMs > 0) {
    return activeDefinition->inactivityDeepSleepMs;
  }
  return DEFAULT_INACTIVITY_DEEP_SLEEP_MS;
}

void checkInactivitySleep() {
  if (batteryShutdownStarted || inactivitySleepStarted) {
    return;
  }
  if (millis() - lastUserActivityAt >= inactivityDeepSleepMs()) {
    enterInactivityDeepSleep();
  }
}

void openQuitDialog() {
  confirmPower = false;
  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onExit != nullptr) {
    activeDefinition->behavior.onExit();
    activePausedForDialog = true;
  }
  confirmQuit = true;
  quitDialogOpenedAt = millis();
  dirty = true;
}

void cancelQuitDialog() {
  confirmQuit = false;
  if (activePausedForDialog && activeDefinition != nullptr &&
      activeDefinition->behavior.onEnter != nullptr) {
    activeDefinition->behavior.onEnter();
  }
  activePausedForDialog = false;
  dirty = true;
}

void openPowerDialog(PowerDialogPage page = PowerDialogPage::Power) {
  if (confirmQuit) {
    cancelQuitDialog();
  }
  powerDialogPage = page;
  confirmPower = true;
  powerDialogOpenedAt = millis();
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
    prepareForPowerTransition();
    rebootDevice();
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

bool restorableShellScreen(Screen value) {
  switch (value) {
  case SCREEN_HOME:
  case SCREEN_MENU:
    return true;
  default:
    return false;
  }
}

Screen retainedScreenForSave() {
  if (textInput.isScreen(screen)) {
    return SCREEN_HOME;
  }
  if (activeDefinition != nullptr) {
    return activeDefinition->screen;
  }
  if (restorableShellScreen(screen)) {
    return screen;
  }
  return SCREEN_HOME;
}

void handleMenuButton() {
  if (confirmPower) {
    confirmPower = false;
    dirty = true;
    return;
  }
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
  if (confirmPower) {
    confirmPower = false;
    dirty = true;
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

void handleMenuLongButton() {
  if (confirmPower) {
    confirmPower = false;
    dirty = true;
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

void switchKeyboardMode() {
  switchTo(textInput.nextMode(screen));
}

void handlePowerButton(AppEventHandler overrideHandler) {
  if (confirmPower) {
    confirmPower = false;
    dirty = true;
    return;
  }
  if (screen == SCREEN_HOME) {
    resetContactLinks();
    switchTo(SCREEN_CONTACT_LINKS, contactLinksRuntime(),
             contactLinksDefinition());
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
  if (confirmPower) {
    confirmPower = false;
    dirty = true;
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

void handlePowerLongButton() {
  openPowerDialog();
}

PowerDialogSnapshot currentPowerDialogSnapshot() {
  DeviceControlSnapshot controls = deviceControlSnapshot();
  PowerDialogSnapshot snapshot;
  snapshot.page = powerDialogPage;
  snapshot.wifiOn = wifiIsOn();
  snapshot.wifiConnected = wifiIsConnected();
  snapshot.bluetoothOn = controls.bluetoothOn;
  snapshot.cpuMhz = controls.cpuMhz;
  snapshot.volume = controls.volume;
  snapshot.muted = controls.muted;
  return snapshot;
}

PowerDialogPage previousPowerDialogPage(PowerDialogPage page) {
  switch (page) {
  case PowerDialogPage::Power:
    return PowerDialogPage::Volume;
  case PowerDialogPage::Device:
    return PowerDialogPage::Power;
  case PowerDialogPage::Volume:
    return PowerDialogPage::Device;
  }
  return PowerDialogPage::Power;
}

PowerDialogPage nextPowerDialogPage(PowerDialogPage page) {
  switch (page) {
  case PowerDialogPage::Power:
    return PowerDialogPage::Device;
  case PowerDialogPage::Device:
    return PowerDialogPage::Volume;
  case PowerDialogPage::Volume:
    return PowerDialogPage::Power;
  }
  return PowerDialogPage::Power;
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
    cancelQuitDialog();
    return true;
  }
  return true;
}

bool handlePowerDialogTouch(const TouchPoint &point) {
  if (millis() - powerDialogOpenedAt < 350) {
    return true;
  }

  switch (powerDialogHitAction(point, powerDialogPage)) {
  case PowerDialogAction::PreviousPage:
    powerDialogPage = previousPowerDialogPage(powerDialogPage);
    dirty = true;
    return true;
  case PowerDialogAction::NextPage:
    powerDialogPage = nextPowerDialogPage(powerDialogPage);
    dirty = true;
    return true;
  case PowerDialogAction::Reboot:
    renderPowerDialogPressed(PowerDialogAction::Reboot);
    prepareForPowerTransition();
    rebootDevice();
    return true;
  case PowerDialogAction::PowerOff:
    renderPowerDialogPressed(PowerDialogAction::PowerOff);
    renderPowerOffScreen();
    prepareForPowerTransition();
    powerOffDevice();
    return true;
  case PowerDialogAction::DeepSleep:
    saveRetainedContextForSleep();
    renderPowerDialogPressed(PowerDialogAction::DeepSleep);
    enterSleepClockDeepSleep();
    return true;
  case PowerDialogAction::WifiToggle:
    renderPowerDialogPressed(PowerDialogAction::WifiToggle);
    wifiToggle();
    dirty = true;
    return true;
  case PowerDialogAction::BluetoothToggle:
    renderPowerDialogPressed(PowerDialogAction::BluetoothToggle);
    // Bluetooth is not wired to an app yet; keep the displayed OFF state inert.
    dirty = true;
    return true;
  case PowerDialogAction::CpuCycle:
    renderPowerDialogPressed(PowerDialogAction::CpuCycle);
    cycleCpuFrequency();
    dirty = true;
    return true;
  case PowerDialogAction::VolumeDown:
    renderPowerDialogPressed(PowerDialogAction::VolumeDown);
    volumeDown();
    dirty = true;
    return true;
  case PowerDialogAction::VolumeMute:
    renderPowerDialogPressed(PowerDialogAction::VolumeMute);
    toggleMute();
    dirty = true;
    return true;
  case PowerDialogAction::VolumeUp:
    renderPowerDialogPressed(PowerDialogAction::VolumeUp);
    volumeUp();
    dirty = true;
    return true;
  case PowerDialogAction::None:
    return true;
  }
  return true;
}

void drawMenu() {
  clampMenuState(menuState, apps, appCount);
  drawAppMenu(display, menuState, apps, appCount, menuPressedSlot);
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
  int8_t hitSlot = -1;
  const AppDefinition *selected =
      hitTestAppMenu(point, menuState, apps, appCount, stateChanged, &hitSlot);
  if (stateChanged) {
    dirty = true;
  }
  if (selected != nullptr) {
    menuPressedSlot = hitSlot;
    redrawActiveScreenPartial();
    delay(MENU_PRESS_HIGHLIGHT_MS);
    menuPressedSlot = -1;
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
    char dateText[16];
    char timeText[16];
    ShellData data =
        currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));
    drawQuitDialog(display, data.status);
  } else if (confirmPower) {
    char dateText[16];
    char timeText[16];
    ShellData data =
        currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));
    drawPowerDialog(display, data.status, currentPowerDialogSnapshot());
  }
  display.flush();
  display.unlock();
  dirty = false;
  if (screen == SCREEN_HOME) {
    lastHomeMinute = deviceClock.localMinuteIndex();
  }
}

void renderPowerDialogPressed(PowerDialogAction action) {
  char dateText[16];
  char timeText[16];
  ShellData data =
      currentShellData(dateText, sizeof(dateText), timeText, sizeof(timeText));

  display.lock();
  display.clear();
  drawActiveScreen();
  drawPowerDialog(display, data.status, currentPowerDialogSnapshot(), action);
  display.flush();
  display.unlock();
  delay(180);
}

void renderPowerOffScreen() {
  display.lock();
  display.clear();
  drawPowerOffScreen(display);
  display.flush();
  display.unlock();
}

void renderLowBatteryScreen() {
  display.lock();
  display.clear();
  drawLowBatteryScreen(display);
  display.flush();
  display.unlock();
}

void renderDeepSleepScreen() {
  display.lock();
  display.clear();
  drawDeepSleepScreen(display);
  display.flush();
  display.unlock();
}

void renderSleepClockScreen(bool partialUpdate) {
  char timeText[16];
  char dateText[16];
  deviceClock.formatTime(timeText, sizeof(timeText));
  deviceClock.formatDate(dateText, sizeof(dateText));

  display.lock();
  display.clear();
  drawDeepSleepClockScreen(display, timeText, dateText);
  if (partialUpdate) {
    display.flushPartial(0, 0, EPD_WIDTH, EPD_HEIGHT);
  } else {
    display.flush();
  }
  display.unlock();
}

void prepareForPowerTransition() { sdStorageEnd(); }

uint16_t sleepClockWakeIntervalSeconds(bool clockSet, int64_t localUnix) {
  if (!clockSet) {
    return 60;
  }

  const int second = static_cast<int>((localUnix % 60 + 60) % 60);
  return second == 0 ? 60 : static_cast<uint16_t>(60 - second);
}

uint64_t sleepClockWakeIntervalUs(uint16_t wakeIntervalSeconds) {
  return static_cast<uint64_t>(wakeIntervalSeconds) * 1000ULL * 1000ULL;
}

bool syncDeviceClockFromRtc() {
#if ENABLE_RTC_CLOCK
  touchI2cBegin();
  rtcClock.begin();
  return rtcClock.readToDeviceClock();
#else
  return false;
#endif
}

bool waitForRtcMinuteChangeIfNeeded() {
  int64_t localUnix = 0;
  uint16_t millisIntoSecond = 0;
  if (!deviceClock.snapshotLocalUnixMillis(localUnix, millisIntoSecond)) {
    return false;
  }

  const int second = static_cast<int>((localUnix % 60 + 60) % 60);
  (void)millisIntoSecond;
  const bool atLastRtcSecondOfMinute = second == 59;
  if (!atLastRtcSecondOfMinute) {
    return false;
  }

  const int64_t previousMinute = localUnix / 60;
  const unsigned long startedAt = millis();
  while (millis() - startedAt < 1500) {
    delay(25);
    if (!syncDeviceClockFromRtc()) {
      continue;
    }
    int64_t refreshedUnix = 0;
    if (deviceClock.snapshotLocalUnix(refreshedUnix) &&
        refreshedUnix / 60 != previousMinute) {
      return true;
    }
  }
  return false;
}

void enterSleepClockDeepSleep() {
  int64_t localUnix = 0;
  const bool clockSet = deviceClock.snapshotLocalUnix(localUnix);
  const uint16_t wakeIntervalSeconds =
      sleepClockWakeIntervalSeconds(clockSet, localUnix);
  sleepClockContextStart(clockSet, localUnix, wakeIntervalSeconds);
  renderSleepClockScreen();
  prepareForPowerTransition();
  enterDeepSleep(sleepClockWakeIntervalUs(wakeIntervalSeconds),
                 SLEEP_CLOCK_RETAIN_EPD_POWER);
}

void seedSleepClockPreviousFrame(const SleepClockSnapshot &sleepClock) {
  if (sleepClock.clockSet) {
    deviceClock.restoreLocalUnix(sleepClock.localUnix);
  }

  display.lock();
  display.clear();
  char timeText[16];
  char dateText[16];
  deviceClock.formatTime(timeText, sizeof(timeText));
  deviceClock.formatDate(dateText, sizeof(dateText));
  drawDeepSleepClockScreen(display, timeText, dateText);
  display.seedPartialBothImages();
  display.unlock();
}

bool handleSleepClockTimerWake() {
  SleepClockSnapshot sleepClock;
  if (!deepSleepWokeFromTimer() || !sleepClockContextLoad(sleepClock)) {
    return false;
  }

  if (SLEEP_CLOCK_RETAIN_EPD_POWER) {
    display.beginRetainedPartial();
  } else {
    display.beginColdPartial();
    seedSleepClockPreviousFrame(sleepClock);
  }

  if (!syncDeviceClockFromRtc() && sleepClock.clockSet) {
    deviceClock.restoreLocalUnix(sleepClock.localUnix +
                                 sleepClock.wakeIntervalSeconds);
  }
  waitForRtcMinuteChangeIfNeeded();

  refreshBatteryAndCheckCutoff();
  if (batteryShutdownStarted) {
    return true;
  }

  renderSleepClockScreen(true);
  if (syncDeviceClockFromRtc() && waitForRtcMinuteChangeIfNeeded()) {
    renderSleepClockScreen(true);
  }

  int64_t localUnix = 0;
  bool clockSet = syncDeviceClockFromRtc();
  if (!clockSet) {
    clockSet = deviceClock.snapshotLocalUnix(localUnix);
  } else {
    clockSet = deviceClock.snapshotLocalUnix(localUnix);
  }
  const uint16_t wakeIntervalSeconds =
      sleepClockWakeIntervalSeconds(clockSet, localUnix);
  sleepClockContextUpdate(clockSet, localUnix, wakeIntervalSeconds);
  prepareForPowerTransition();
  enterDeepSleep(sleepClockWakeIntervalUs(wakeIntervalSeconds),
                 SLEEP_CLOCK_RETAIN_EPD_POWER);
  return true;
}

void saveRetainedContextForSleep() {
  retainedSleepContext = RtcContextSnapshot{};
  retainedSleepContext.navigation.screen = retainedScreenForSave();
  retainedSleepContext.navigation.menuCategory = menuState.category;
  retainedSleepContext.navigation.menuPage = menuState.page;
  retainedSleepContext.system.wifiOn = wifiIsOn();
  retainedSleepContext.system.clockSet =
      deviceClock.snapshotLocalUnix(retainedSleepContext.system.clockLocalUnix);

  if (activeDefinition != nullptr) {
    retainedSleepContext.navigation.hasActiveApp = true;
    strncpy(retainedSleepContext.navigation.appId, activeDefinition->id,
            RTC_CONTEXT_APP_ID_SIZE - 1);
    retainedSleepContext.navigation.appId[RTC_CONTEXT_APP_ID_SIZE - 1] = '\0';

    if (activeDefinition->behavior.onSaveContext != nullptr) {
      size_t length = activeDefinition->behavior.onSaveContext(
          retainedSleepContext.app.appData, RTC_CONTEXT_APP_CAPACITY);
      if (length > RTC_CONTEXT_APP_CAPACITY) {
        length = RTC_CONTEXT_APP_CAPACITY;
      }
      retainedSleepContext.app.appDataLength = static_cast<uint16_t>(length);
    }
  }

  rtcContextSave(retainedSleepContext);
}

void restoreRetainedContextAfterSleep() {
  if (!rtcContextLoad(retainedSleepContext)) {
    return;
  }

  if (!deviceClock.isSet() && retainedSleepContext.system.clockSet) {
    deviceClock.restoreLocalUnix(retainedSleepContext.system.clockLocalUnix);
  }

  restoreWifiOn(retainedSleepContext.system.wifiOn);
  menuState.category = retainedSleepContext.navigation.menuCategory;
  menuState.page = retainedSleepContext.navigation.menuPage;
  clampMenuState(menuState, apps, appCount);

  if (retainedSleepContext.navigation.hasActiveApp) {
    const AppDefinition *app = findAppById(retainedSleepContext.navigation.appId);
    if (app != nullptr) {
      launchApp(*app);
      if (app->behavior.onRestoreContext != nullptr) {
        app->behavior.onRestoreContext(retainedSleepContext.app.appData,
                                       retainedSleepContext.app.appDataLength);
      }
      rtcContextClear();
      dirty = true;
      return;
    }
  }

  if (restorableShellScreen(retainedSleepContext.navigation.screen)) {
    switchTo(retainedSleepContext.navigation.screen);
  }

  rtcContextClear();
  dirty = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  keepPowerLatchOn();
  releasePowerHolds();
  keepPowerLatchOn();
  if (handleSleepClockTimerWake()) {
    return;
  }
  sleepClockContextClear();
  audioPowerBegin();
  deviceControlsBegin();
  display.begin();
  touch.begin();
#if ENABLE_RTC_CLOCK
  rtcClock.begin();
  rtcClock.readToDeviceClock();
#endif
  refreshBatteryAndCheckCutoff();
  if (batteryShutdownStarted) {
    return;
  }
  environmentMonitor.refresh();
  sdStorageBegin();

  shellButtonsBegin({handleMenuButton, handleMenuDoubleButton,
                     handleMenuLongButton, handlePowerSingleButton,
                     handlePowerDoubleButton, handlePowerLongButton});
  noteUserActivity();

  resetApps();
  restoreRetainedContextAfterSleep();
  dirty = true;
}

void loop() {
  if (shellButtonsDispatch()) {
    noteUserActivity();
  }

  if (activeDefinition != nullptr &&
      activeDefinition->behavior.onRawTouch != nullptr && !confirmQuit &&
      !confirmPower) {
    TouchEvent event;
    while (touch.readEvent(event)) {
      noteUserActivity();
      activeDefinition->behavior.onRawTouch(event);
    }
  } else {
    TouchPoint point;
    if (touch.read(point)) {
      noteUserActivity();
      if (confirmPower) {
        handlePowerDialogTouch(point);
        delay(1);
        return;
      }
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

  checkInactivitySleep();
  if (inactivitySleepStarted) {
    return;
  }

  if (dirty) {
    render();
  }

  if (wifiUpdate()) {
    dirty = true;
  }

  if (sdStorageUpdate()) {
    clampMenuState(menuState, apps, appCount);
    dirty = true;
  }

  if (activeApp != nullptr && activeApp->update()) {
    dirty = true;
  }

  checkInactivitySleep();
  if (inactivitySleepStarted) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastBatteryRefreshAt >= BATTERY_REFRESH_INTERVAL_MS) {
    refreshBatteryAndCheckCutoff();
    if (batteryShutdownStarted) {
      return;
    }
    if (screen == SCREEN_HOME || confirmPower || confirmQuit) {
      dirty = true;
    }
  }

  int64_t currentHomeMinute = deviceClock.localMinuteIndex();
  if (screen == SCREEN_HOME && currentHomeMinute >= 0 &&
      currentHomeMinute != lastHomeMinute) {
    refreshBatteryAndCheckCutoff();
    if (batteryShutdownStarted) {
      return;
    }
    environmentMonitor.refresh();
    lastHomeMinute = currentHomeMinute;
    dirty = true;
  }

  if (dirty) {
    render();
  }
  delay(1);
}
