#include "sys/app_display.h"
#include "sys/builtin_apps.h"
#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif
#include "sys/system_config.h"
#include "sys/shell_buttons.h"
#include "sys/audio_power.h"
#include "sys/device_clock.h"
#include "sys/battery_monitor.h"
#include "sys/device_controls.h"
#include "sys/environment_monitor.h"
#include "sys/global.h"
#include "sys/heap_debug.h"
#include "sys/lockscreen_guard.h"
#include "sys/power_control.h"
#include "sys/pcf85063_clock.h"
#include "sys/pink_executable.h"
#include "sys/sleep_clock_context.h"
#include "sys/sd_storage.h"
#include "sys/usb_status.h"
#include "shell/connectivity.h"
#include "shell/shell_dialog_layout.h"
#include "shell/shell_home_layout.h"
#include "shell/shell_controller.h"
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

extern "C" bool setCpuFrequencyMhz(uint32_t cpu_freq_mhz);

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define POCKETINK_HAS_ROM_PRINTF 1
#else
#define POCKETINK_HAS_ROM_PRINTF 0
#endif

AppDisplay display;
TouchInput touch;
TextInputController textInput;
ShellController shell(display, touch, textInput);

static const unsigned long BATTERY_REFRESH_INTERVAL_MS = 30000;
static const unsigned long DEFAULT_INACTIVITY_DEEP_SLEEP_MS = 120000;
static const uint32_t INACTIVITY_SLEEP_GUARD_GRACE_MS = 3000;
static const uint16_t SLEEP_CLOCK_WAKE_CPU_MHZ = 80;
static const bool SLEEP_CLOCK_RETAIN_EPD_POWER = false;

bool batteryShutdownStarted = false;
bool inactivitySleepStarted = false;
bool lowPowerSleepClockWake = false;
bool usbConnectionKnown = false;
bool lastUsbConnected = false;
unsigned long lastBatteryRefreshAt = 0;
unsigned long lastUserActivityAt = 0;

bool batteryCutoffReached();
void checkInactivitySleep();
void enterInactivityDeepSleep();
unsigned long inactivityDeepSleepMs();
void noteUserActivity();
void refreshBatteryAndCheckCutoff();
void shutdownForLowBattery();
void renderLowBatteryScreen();
void renderPowerOffScreen();
void renderSleepClockScreen(bool partialUpdate = false);
void refreshUsbConnectionState();
void seedSleepClockPreviousFrame(const SleepClockSnapshot &sleepClock);
uint16_t sleepClockWakeIntervalSeconds(bool clockSet, int64_t localUnix);
uint64_t sleepClockWakeIntervalUs(uint16_t wakeIntervalSeconds);
bool syncDeviceClockFromRtc();
bool waitForRtcMinuteChangeIfNeeded();
void refreshHomeShell(void *context);
void noteUserActivityShell(void *context);
void prepareForPowerTransitionShell(void *context);
void enterSleepClockDeepSleepShell(void *context);
void renderPowerOffScreenShell(void *context);
void prepareForPowerTransition();
void enterSleepClockDeepSleep();
bool handleSleepClockTimerWake();
void handleMenuButton();
void handleMenuDoubleButton();
void handleMenuLongButton();
void handlePowerSingleButton();
void handlePowerDoubleButton();
void handlePowerLongButton();

void noteUserActivity() { lastUserActivityAt = millis(); }

void refreshHomeShell(void *) {
  refreshBatteryAndCheckCutoff();
  environmentMonitor.refresh();
}

void noteUserActivityShell(void *) { noteUserActivity(); }

void prepareForPowerTransitionShell(void *) { prepareForPowerTransition(); }

void enterSleepClockDeepSleepShell(void *) { enterSleepClockDeepSleep(); }

void renderPowerOffScreenShell(void *) { renderPowerOffScreen(); }

void handleMenuButton() { shell.handleMenuButton(); }

void handleMenuDoubleButton() { shell.handleMenuDoubleButton(); }

void handleMenuLongButton() { shell.handleMenuLongButton(); }

void handlePowerSingleButton() { shell.handlePowerSingleButton(); }

void handlePowerDoubleButton() { shell.handlePowerDoubleButton(); }

void handlePowerLongButton() { shell.handlePowerLongButton(); }

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

void refreshUsbConnectionState() {
  const bool connected = usbConnected();
  if (!usbConnectionKnown) {
    usbConnectionKnown = true;
    lastUsbConnected = connected;
    return;
  }
  if (connected == lastUsbConnected) {
    return;
  }
  lastUsbConnected = connected;
  if (connected) {
    noteUserActivity();
  }
  shell.markDirty();
}

void enterInactivityDeepSleep() {
  if (batteryShutdownStarted || inactivitySleepStarted) {
    return;
  }
  if (usbConnected()) {
    noteUserActivity();
    return;
  }
  if (inactivitySleepBlocked(INACTIVITY_SLEEP_GUARD_GRACE_MS)) {
    noteUserActivity();
    return;
  }
  inactivitySleepStarted = true;
  shell.saveRetainedContextForSleep();
  enterSleepClockDeepSleep();
}

unsigned long inactivityDeepSleepMs() {
  return shell.inactivityDeepSleepMs(DEFAULT_INACTIVITY_DEEP_SLEEP_MS);
}

void checkInactivitySleep() {
  if (batteryShutdownStarted || inactivitySleepStarted) {
    return;
  }
  if (usbConnected()) {
    noteUserActivity();
    return;
  }
  if (millis() - lastUserActivityAt >= inactivityDeepSleepMs()) {
    enterInactivityDeepSleep();
  }
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

void prepareForPowerTransition() {
  wifiTurnOff();
  disableWirelessForSleep();
  sdStorageEnd();
}

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
  // The RTC shares the touch I2C bus; release touch reset after deep sleep so
  // it cannot hold the shared lines in an undefined state.
  pinMode(EPD_TP_RST_PIN, OUTPUT);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(5);
  touchI2cBegin();
  rtcClock.begin();
  const bool updated = rtcClock.readToDeviceClock();
  digitalWrite(EPD_TP_RST_PIN, LOW);
  return updated;
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
  rebootDevice();
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

  setCpuFrequencyMhz(SLEEP_CLOCK_WAKE_CPU_MHZ);
  disableWirelessForSleep();

  if (SLEEP_CLOCK_RETAIN_EPD_POWER) {
    display.beginRetainedPartial();
  } else {
    display.beginColdPartial();
    seedSleepClockPreviousFrame(sleepClock);
  }

  bool clockSet = syncDeviceClockFromRtc();
  if (!clockSet && sleepClock.clockSet) {
    deviceClock.restoreLocalUnix(sleepClock.localUnix +
                                 sleepClock.wakeIntervalSeconds);
    clockSet = true;
  }
  if (waitForRtcMinuteChangeIfNeeded()) {
    clockSet = true;
  }

  refreshBatteryAndCheckCutoff();
  if (batteryShutdownStarted) {
    return true;
  }

  renderSleepClockScreen(true);

  int64_t localUnix = 0;
  if (!deviceClock.snapshotLocalUnix(localUnix)) {
    clockSet = false;
  }
  const uint16_t wakeIntervalSeconds =
      sleepClockWakeIntervalSeconds(clockSet, localUnix);
  sleepClockContextUpdate(clockSet, localUnix, wakeIntervalSeconds);
  prepareForPowerTransition();
  enterDeepSleep(sleepClockWakeIntervalUs(wakeIntervalSeconds),
                 SLEEP_CLOCK_RETAIN_EPD_POWER);
  rebootDevice();
  return true;
}

void waitForSerialDebugWindow() {
  const unsigned long startedAt = millis();
  while (!Serial && millis() - startedAt < 3000) {
    delay(10);
  }
}

void logSetupStage(const char *stage) {
  const unsigned long now = millis();
#if POCKETINK_HAS_ROM_PRINTF
  esp_rom_printf("[pocketink-rom] %lu setup %s\n", now, stage);
#endif
  Serial.printf("[pocketink] %lu setup %s\n", now, stage);
  Serial.flush();
}

void setup() {
  lowPowerSleepClockWake = deepSleepWokeFromTimer();
  if (lowPowerSleepClockWake) {
    setCpuFrequencyMhz(SLEEP_CLOCK_WAKE_CPU_MHZ);
    keepPowerLatchOn();
    releasePowerHolds();
    prepareWakePowerDefaults();
    keepPowerLatchOn();
    configLoadDefaults();
    if (handleSleepClockTimerWake()) {
      return;
    }
  }

  Serial.begin(115200);
  waitForSerialDebugWindow();
  logSetupStage("start");
  heapMark("boot-start");

  logSetupStage("power latch");
  keepPowerLatchOn();
  releasePowerHolds();
  prepareWakePowerDefaults();
  keepPowerLatchOn();
  heapMark("after-power-latch");

  logSetupStage("config defaults");
  configLoadDefaults();
  heapMark("after-config-defaults");

  logSetupStage("check sleep-clock wake");
  if (handleSleepClockTimerWake()) {
    return;
  }
  logSetupStage("normal boot");
  sleepClockContextClear();
  heapMark("after-sleep-clock-check");

  logSetupStage("sd");
  sdStorageBegin();
  heapMark("after-sd");

  logSetupStage("config");
  configBegin();
  shellLayoutConfigReload();
  heapMark("after-config");

  logSetupStage("audio");
  audioPowerBegin();
  heapMark("after-audio");

  logSetupStage("device controls");
  deviceControlsBegin();
  heapMark("after-device-controls");

  logSetupStage("display");
  display.begin();
  heapMark("after-display");

  logSetupStage("touch");
  touch.begin();
  ShellControllerHooks shellHooks;
  shellHooks.refreshHome = refreshHomeShell;
  shellHooks.noteUserActivity = noteUserActivityShell;
  shellHooks.prepareForPowerTransition = prepareForPowerTransitionShell;
  shellHooks.enterSleepClockDeepSleep = enterSleepClockDeepSleepShell;
  shellHooks.renderPowerOffScreen = renderPowerOffScreenShell;
  shell.setHooks(shellHooks);
#if ENABLE_RTC_CLOCK
  logSetupStage("rtc");
  rtcClock.begin();
  rtcClock.readToDeviceClock();
  heapMark("after-rtc");
#endif
  logSetupStage("battery");
  refreshBatteryAndCheckCutoff();
  if (batteryShutdownStarted) {
    logSetupStage("battery shutdown");
    return;
  }
  heapMark("after-battery");

  logSetupStage("environment");
  environmentMonitor.refresh();
  pinkExecutableRefreshApps();
  refreshAppCatalog();
  heapMark("after-app-catalog");

  logSetupStage("buttons");
  shellButtonsBegin({handleMenuButton, handleMenuDoubleButton,
                     handleMenuLongButton, handlePowerSingleButton,
                     handlePowerDoubleButton, handlePowerLongButton});
  noteUserActivity();
  heapMark("after-buttons");

  logSetupStage("apps");
  resetApps();
  heapMark("after-apps-reset");

  logSetupStage("restore context");
  shell.restoreRetainedContextAfterSleep();
  shell.markDirty();
  logSetupStage("ready");
  heapMark("after-ready");
#if ENABLE_NETWORK_APPS
  heapMark("pre-wifi");
  wifiUpdate();
  heapMark("after-wifi-first-update");
#endif
}

void loop() {
  if (shellButtonsDispatch()) {
    noteUserActivity();
  }

  if (shell.dispatchTouch()) {
    delay(1);
    return;
  }

  refreshUsbConnectionState();

  checkInactivitySleep();
  if (inactivitySleepStarted) {
    return;
  }

  shell.renderIfDirty();

  if (wifiUpdate()) {
    shell.markDirty();
  }

  if (sdStorageUpdate()) {
    configReload();
    shellLayoutConfigReload();
    pinkExecutableRefreshApps();
    refreshAppCatalog();
    shell.handleCatalogChanged();
  }

  shell.updateActiveApp();
  if (shell.handlePendingFilesPinkLaunch()) {
    delay(1);
    return;
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
    if (shell.shouldRedrawForBatteryRefresh()) {
      shell.markDirty();
    }
  }

  shell.refreshHomeMinuteIfNeeded();
  if (batteryShutdownStarted) {
    return;
  }

  shell.renderIfDirty();
  delay(1);
}
