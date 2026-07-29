#include "sys/app_display.h"
#include "sys/builtin_apps.h"
#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif
#include "sys/system_config.h"
#include "shell/shell_buttons.h"
#include "sys/services/device_clock.h"
#include "sys/services/battery_monitor.h"
#include "sys/services/device_controls.h"
#include "sys/services/environment_monitor.h"
#include "sys/global.h"
#include "sys/debug_logging.h"
#include "sys/heap_debug.h"
#include "sys/services/lockscreen_guard.h"
#include "sys/services/lockscreen_content.h"
#include "sys/services/power_control.h"
#include "sys/drivers/pcf85063_clock.h"
#include "sys/pink_executable.h"
#include "sys/pink_codec.h"
#include "sys/periodic_tasks.h"
#include "sys/sleep_clock_context.h"
#include "sys/wake_trace.h"
#include "sys/services/sd_storage.h"
#include "sys/drivers/usb_status.h"
#include "sys/services/connectivity_service.h"
#include "sys/services/periodic_wake_service.h"
#include "sys/services/system_stage_service.h"
#include "sys/video/record.h"
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
static const uint32_t INACTIVITY_SLEEP_GUARD_GRACE_MS = 3000;
static const uint16_t LOW_POWER_WAKE_CPU_MHZ = 80;
bool batteryShutdownStarted = false;
bool inactivitySleepStarted = false;
bool lowPowerSleepClockWake = false;
bool usbConnectionKnown = false;
bool lastUsbConnected = false;
unsigned long lastBatteryRefreshAt = 0;
unsigned long lastUserActivityAt = 0;
unsigned long lastUserInteractionAt = 0;

bool batteryCutoffReached();
void checkInactivitySleep();
void enterInactivityDeepSleep();
unsigned long inactivityDeepSleepMs();
void noteUserActivity();
void noteUserInteraction();
void refreshBatteryAndCheckCutoff();
void shutdownForLowBattery();
void renderLowBatteryScreen();
void renderPowerOffScreen();
void refreshUsbConnectionState();
void checkWifiAutoOff();
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

SystemStageService systemStageService;
PeriodicWakeService periodicWakeService(
    display, systemStageService,
    {batteryCutoffReached, shutdownForLowBattery,
     prepareForPowerTransition});

void noteUserActivity() {
  lastUserActivityAt = millis();
}

void noteUserInteraction() {
  const unsigned long now = millis();
  lastUserActivityAt = now;
  lastUserInteractionAt = now;
  deviceControlsNoteUserActivity();
}

void refreshHomeShell(void *) {
  refreshBatteryAndCheckCutoff();
  environmentMonitor.refresh();
}

void noteUserActivityShell(void *) {
  noteUserInteraction();
}

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

void checkWifiAutoOff() {
  const uint8_t minutes = currentSystemConfig().wifi.autoOffMinutes;
  if (minutes == 0 || !wifiIsOn()) {
    return;
  }
  const unsigned long timeoutMs =
      static_cast<unsigned long>(minutes) * 60UL * 1000UL;
  if (millis() - lastUserInteractionAt < timeoutMs) {
    return;
  }
  wifiTurnOff();
  shell.markDirty();
}

void enterInactivityDeepSleep() {
  if (batteryShutdownStarted || inactivitySleepStarted) {
    return;
  }
  if (inactivityDeepSleepMs() == 0) {
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
  const uint8_t minutes = currentSystemConfig().lockscreen.inactivityMinutes;
  if (minutes == 0) {
    return 0;
  }
  return shell.inactivityDeepSleepMs(static_cast<unsigned long>(minutes) *
                                     60UL * 1000UL);
}

void checkInactivitySleep() {
  if (batteryShutdownStarted || inactivitySleepStarted) {
    return;
  }
  if (usbConnected()) {
    noteUserActivity();
    return;
  }
  const unsigned long timeoutMs = inactivityDeepSleepMs();
  if (timeoutMs == 0) {
    return;
  }
  if (millis() - lastUserActivityAt >= timeoutMs) {
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

void prepareForPowerTransition() {
  videoRecordStop();
  touch.end();
  wifiTurnOff();
  disableWirelessForSleep();
  sdStorageEnd();
}

void enterSleepClockDeepSleep() {
  periodicWakeService.enterSleepClockDeepSleep();
}

bool handleSleepClockTimerWake() {
  return periodicWakeService.handleTimerWake();
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

void logRetainedWakeTrace() {
  WakeTraceSnapshot trace;
  if (!wakeTraceLoad(trace)) {
    Serial.println("[pocketink] wake-trace invalid");
    return;
  }
  Serial.printf(
      "[pocketink] wake-trace boot=%lu timer=%lu reset=%d wake=%d "
      "previous-reset=%d previous-wake=%d phase=%s context=%s rtc=%s "
      "dispatch=%u display=%u sleep=%u retained=%lld rtc-time=%lld "
      "rendered=%lld\n",
      static_cast<unsigned long>(trace.bootCount),
      static_cast<unsigned long>(trace.timerWakeCount), trace.resetReason,
      trace.wakeupCause, trace.previousResetReason,
      trace.previousWakeupCause, wakeTracePhaseName(trace.phase),
      wakeTraceResultName(trace.sleepContext),
      wakeTraceResultName(trace.rtcRead),
      static_cast<unsigned>(trace.dispatchStage),
      static_cast<unsigned>(trace.displayStage),
      static_cast<unsigned>(trace.sleepStage),
      static_cast<long long>(trace.retainedLocalUnix),
      static_cast<long long>(trace.rtcLocalUnix),
      static_cast<long long>(trace.renderedLocalUnix));
  Serial.flush();
}

void setup() {
  lowPowerSleepClockWake = deepSleepWokeFromTimer();
  wakeTraceBeginBoot(deviceResetReason(), deviceWakeupCause(),
                     lowPowerSleepClockWake);
  debugLoggingSetEnabled(!lowPowerSleepClockWake);
  if (lowPowerSleepClockWake) {
    setCpuFrequencyMhz(LOW_POWER_WAKE_CPU_MHZ);
    keepPowerLatchOn();
    releasePowerHolds();
    prepareWakePowerDefaults();
    keepPowerLatchOn();
    configLoadDefaults();
    if (handleSleepClockTimerWake()) {
      return;
    }
    debugLoggingSetEnabled(true);
  }

  Serial.begin(115200);
  waitForSerialDebugWindow();
  logSetupStage("start");
  heapMark("boot-start");

  if (!lowPowerSleepClockWake) {
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
  }
  logSetupStage("normal boot");
  logRetainedWakeTrace();
  sleepClockContextClear();
  lockscreenNotificationClear();
  heapMark("after-sleep-clock-check");

  logSetupStage("system services");
  systemStageService.initialize();
  heapMark("after-system-services");

  logSetupStage("device controls");
  deviceControlsBegin();
  heapMark("after-device-controls");

  logSetupStage("display");
  if (!periodicWakeService.displayInitialized()) {
    display.begin();
  }
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
  int64_t localUnix = 0;
  const int32_t utcOffsetSeconds = configEffectiveUtcOffset();
  if (rtcClock.readLocalUnix(localUnix)) {
    deviceClock.restoreLocalUnix(localUnix, utcOffsetSeconds);
  }
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
  touch.startPolling();
  pinkCodecRefresh();
  pinkExecutableRefreshApps();
  refreshAppCatalog();
  heapMark("after-app-catalog");

  logSetupStage("buttons");
  shellButtonsBegin({handleMenuButton, handleMenuDoubleButton,
                     handleMenuLongButton, handlePowerSingleButton,
                     handlePowerDoubleButton, handlePowerLongButton});
  noteUserInteraction();
  heapMark("after-buttons");

  logSetupStage("apps");
  resetApps();
  heapMark("after-apps-reset");

  logSetupStage("restore context");
  char retainedPeriodicWakeAppId[24] = {};
  const bool hasPeriodicWakeApp = periodicRetainedTakeWakeApp(
      retainedPeriodicWakeAppId, sizeof(retainedPeriodicWakeAppId));
  shell.restoreRetainedContextAfterSleep();
  if (hasPeriodicWakeApp) {
    shell.wakeAppById(retainedPeriodicWakeAppId);
  }
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
    noteUserInteraction();
  }

  if (shell.dispatchTouch()) {
    delay(1);
    return;
  }

  refreshUsbConnectionState();

  deviceControlsUpdate(wifiIsOn());
  checkWifiAutoOff();

  checkInactivitySleep();
  if (inactivitySleepStarted) {
    return;
  }

  shell.renderIfDirty();
  shell.updateVideoRecording();

  if (wifiUpdate()) {
    shell.markDirty();
  }

  if (sdStorageUpdate()) {
    configReload();
    shellLayoutConfigReload();
    pinkCodecRefresh();
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
