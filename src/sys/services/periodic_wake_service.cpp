#include "sys/services/periodic_wake_service.h"

#ifndef ENABLE_NETWORK_APPS
#define ENABLE_NETWORK_APPS 1
#endif

#include "shell/shell_lockscreen_layout.h"
#include "sys/drivers/pcf85063_clock.h"
#include "sys/drivers/shared_i2c_bus.h"
#include "sys/pink_periodic_tasks.h"
#include "sys/services/battery_monitor.h"
#include "sys/services/connectivity_service.h"
#include "sys/services/device_clock.h"
#include "sys/services/power_control.h"
#include "sys/services/sd_storage.h"
#include "sys/services/system_stage_service.h"
#include "sys/services/time_sync.h"
#include "sys/system_config.h"

#include <Arduino.h>
#include <cstring>

extern "C" bool setCpuFrequencyMhz(uint32_t cpu_freq_mhz);

namespace {

constexpr uint16_t kWakeCpuMhz = 80;
constexpr unsigned long kWifiConnectTimeoutMaxMs = 16000;
constexpr bool kRetainEpdPower = false;

PeriodicDisplayMode maximumDisplayMode(PeriodicDisplayMode a,
                                       PeriodicDisplayMode b) {
  return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

} // namespace

PeriodicWakeService::PeriodicWakeService(AppDisplay &nextDisplay,
                                         SystemStageService &nextSystemStage,
                                         const PeriodicWakeHooks &nextHooks)
    : display(nextDisplay), systemStage(nextSystemStage), hooks(nextHooks) {}

void PeriodicWakeService::registerSystemTasks(uint8_t timeSyncIntervalHours) {
  periodicSystemTasksReset();
#if ENABLE_NETWORK_APPS
  static const PeriodicSystemTaskDefinition timeSyncTasks[] = {
      {"system.time_sync",
       {1U * 60U, PeriodicWakeLevel::Network, PeriodicDisplayMode::None},
       runTimeSync},
      {"system.time_sync",
       {3U * 60U, PeriodicWakeLevel::Network, PeriodicDisplayMode::None},
       runTimeSync},
      {"system.time_sync",
       {6U * 60U, PeriodicWakeLevel::Network, PeriodicDisplayMode::None},
       runTimeSync},
      {"system.time_sync",
       {12U * 60U, PeriodicWakeLevel::Network, PeriodicDisplayMode::None},
       runTimeSync},
      {"system.time_sync",
       {24U * 60U, PeriodicWakeLevel::Network, PeriodicDisplayMode::None},
       runTimeSync},
  };
  if (timeSyncIntervalHours != 0) {
    size_t index = 4;
    if (timeSyncIntervalHours == 1) {
      index = 0;
    } else if (timeSyncIntervalHours == 3) {
      index = 1;
    } else if (timeSyncIntervalHours == 6) {
      index = 2;
    } else if (timeSyncIntervalHours == 12) {
      index = 3;
    }
    periodicSystemTaskRegister(timeSyncTasks[index]);
  }
#else
  (void)timeSyncIntervalHours;
#endif
}

PeriodicTaskAction
PeriodicWakeService::runTimeSync(const PeriodicTaskContext &context) {
#if ENABLE_NETWORK_APPS
  if (context.wifiPreferred && context.wifiConnected) {
    timeSync.reset();
    timeSync.update(true);
  }
#else
  (void)context;
#endif
  return PeriodicTaskAction::Complete;
}

uint16_t PeriodicWakeService::wakeIntervalSeconds(bool clockSet,
                                                  int64_t localUnix) {
  if (!clockSet) {
    return 60;
  }
  const int second = static_cast<int>((localUnix % 60 + 60) % 60);
  return second == 0 ? 60 : static_cast<uint16_t>(60 - second);
}

uint64_t PeriodicWakeService::wakeIntervalUs(uint16_t intervalSeconds) {
  return static_cast<uint64_t>(intervalSeconds) * 1000ULL * 1000ULL;
}

bool PeriodicWakeService::syncClockFromRtc(int32_t utcOffsetSeconds) {
#if ENABLE_RTC_CLOCK
  // Touch and RTC share I2C. Release touch reset while reading, then isolate it.
  pinMode(EPD_TP_RST_PIN, OUTPUT);
  digitalWrite(EPD_TP_RST_PIN, HIGH);
  delay(5);
  sharedI2cBegin();
  rtcClock.begin();
  int64_t localUnix = 0;
  const bool updated = rtcClock.readLocalUnix(localUnix);
  if (updated) {
    deviceClock.restoreLocalUnix(localUnix, utcOffsetSeconds);
  }
  digitalWrite(EPD_TP_RST_PIN, LOW);
  return updated;
#else
  (void)utcOffsetSeconds;
  return false;
#endif
}

bool PeriodicWakeService::waitForRtcMinuteChangeIfNeeded() {
  int64_t localUnix = 0;
  uint16_t millisIntoSecond = 0;
  if (!deviceClock.snapshotLocalUnixMillis(localUnix, millisIntoSecond)) {
    return false;
  }

  const int second = static_cast<int>((localUnix % 60 + 60) % 60);
  (void)millisIntoSecond;
  if (second != 59) {
    return false;
  }

  const int64_t previousMinute = localUnix / 60;
  const unsigned long startedAt = millis();
  while (millis() - startedAt < 1500) {
    delay(25);
    if (!syncClockFromRtc(deviceClock.utcOffsetSeconds())) {
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

void PeriodicWakeService::initializeNetworkStage(bool wifiPreferred) {
  setCpuFrequencyMhz(PERIODIC_WIFI_CPU_MHZ);
  if (!wifiPreferred) {
    return;
  }
  wifiTurnOn();
  const unsigned long startedAt = millis();
  while (wifiConnectionInProgress() && !wifiIsConnected() &&
         millis() - startedAt < kWifiConnectTimeoutMaxMs) {
    wifiUpdate();
    delay(25);
  }
}

void PeriodicWakeService::seedSleepClockPreviousFrame(
    const SleepClockSnapshot &sleepClock) {
  if (sleepClock.clockSet) {
    deviceClock.restoreLocalUnix(sleepClock.localUnix,
                                 sleepClock.utcOffsetSeconds);
  }

  display.lock();
  display.clear();
  char timeText[16];
  char dateText[16];
  deviceClock.formatTime(timeText, sizeof(timeText), sleepClock.timeFormat);
  if (sleepClock.timeFormat == CONFIG_TIME_12H) {
    char *space = strchr(timeText, ' ');
    if (space != nullptr) {
      memmove(space, space + 1, strlen(space));
    }
  }
  deviceClock.formatDate(dateText, sizeof(dateText));
  drawLockscreenClockScreen(display, timeText, dateText);
  display.seedPartialBothImages();
  display.unlock();
}

void PeriodicWakeService::initializeDisplay(
    PeriodicDisplayMode mode, const SleepClockSnapshot &sleepClock) {
  if (mode == PeriodicDisplayMode::None ||
      static_cast<uint8_t>(initializedDisplay) >= static_cast<uint8_t>(mode)) {
    return;
  }
  if (mode == PeriodicDisplayMode::Full || !sleepClock.previousFrameValid) {
    display.begin();
    initializedDisplay = PeriodicDisplayMode::Full;
    return;
  }
  if (kRetainEpdPower) {
    display.beginRetainedPartial();
  } else {
    display.beginColdPartial();
    seedSleepClockPreviousFrame(sleepClock);
  }
  initializedDisplay = PeriodicDisplayMode::Partial;
}

void PeriodicWakeService::flushDisplay(PeriodicDisplayMode mode) {
  if (mode == PeriodicDisplayMode::None) {
    return;
  }
  display.lock();
  if (initializedDisplay == PeriodicDisplayMode::Full) {
    display.flush();
  } else {
    display.flushPartial();
  }
  display.unlock();
}

void PeriodicWakeService::renderSleepClockScreen(
    bool partialUpdate, ConfigTimeFormat timeFormat) {
  char timeText[16];
  char dateText[16];
  deviceClock.formatTime(timeText, sizeof(timeText), timeFormat);
  if (timeFormat == CONFIG_TIME_12H) {
    char *space = strchr(timeText, ' ');
    if (space != nullptr) {
      memmove(space, space + 1, strlen(space));
    }
  }
  deviceClock.formatDate(dateText, sizeof(dateText));

  display.lock();
  display.clear();
  drawLockscreenClockScreen(display, timeText, dateText);
  if (partialUpdate) {
    display.flushPartial();
  } else {
    display.flush();
  }
  display.unlock();
}

void PeriodicWakeService::prepareForPowerTransition() {
  if (hooks.prepareForPowerTransition != nullptr) {
    hooks.prepareForPowerTransition();
  }
}

void PeriodicWakeService::enterSleepClockDeepSleep() {
  int64_t localUnix = 0;
  const bool clockSet = deviceClock.snapshotLocalUnix(localUnix);
  const PeriodicWakePolicy policy =
      periodicPolicyFromConfig(currentSystemConfig());
  PinkPeriodicPlan pinkPlan;
  if (policy.minIntervalMinutes != 0 && sdStorageMounted()) {
    pinkPlan = pinkPeriodicPlan(localUnix / 60, localUnix / 60,
                                policy.minIntervalMinutes);
  }
  periodicRetainedPrepareForSleep(policy, wifiIsOn(), pinkPlan.anyRegistered,
                                  pinkPlan.nextDueMinute);
  const uint16_t intervalSeconds = wakeIntervalSeconds(clockSet, localUnix);
  const ConfigTimeFormat timeFormat = currentSystemConfig().time.timeFormat;
  sleepClockContextStart(clockSet, localUnix, deviceClock.utcOffsetSeconds(),
                         intervalSeconds, timeFormat);
  renderSleepClockScreen(false, timeFormat);
  prepareForPowerTransition();
  enterDeepSleep(wakeIntervalUs(intervalSeconds), kRetainEpdPower);
  rebootDevice();
}

bool PeriodicWakeService::handleTimerWake() {
  SleepClockSnapshot sleepClock;
  if (!deepSleepWokeFromTimer() || !sleepClockContextLoad(sleepClock)) {
    return false;
  }

  setCpuFrequencyMhz(kWakeCpuMhz);
  disableWirelessForSleep();

  bool clockSet = syncClockFromRtc(sleepClock.utcOffsetSeconds);
  if (!clockSet && sleepClock.clockSet) {
    deviceClock.restoreLocalUnix(
        sleepClock.localUnix + sleepClock.wakeIntervalSeconds,
        sleepClock.utcOffsetSeconds);
    clockSet = true;
  }
  if (waitForRtcMinuteChangeIfNeeded()) {
    clockSet = true;
  }

  int64_t localUnix = 0;
  if (!deviceClock.snapshotLocalUnix(localUnix)) {
    clockSet = false;
  }

  PeriodicRetainedSnapshot retained;
  if (!periodicRetainedLoad(retained)) {
    retained.policy = periodicPolicyFromConfig(currentSystemConfig());
  }
  registerSystemTasks(retained.policy.timeSyncIntervalHours);

  batteryMonitor.refresh();
  if (hooks.batteryCutoffReached != nullptr &&
      hooks.batteryCutoffReached()) {
    initializeDisplay(PeriodicDisplayMode::Partial, sleepClock);
    if (hooks.shutdownForLowBattery != nullptr) {
      hooks.shutdownForLowBattery();
    }
    return true;
  }
  const BatterySnapshot &batteryAtWake = batteryMonitor.snapshot();
  const bool lowBattery =
      batteryAtWake.valid &&
      batteryAtWake.percentage <= PERIODIC_LOW_BATTERY_PERCENT;
  const bool tasksSuppressed =
      retained.policy.disableOnLowBattery && lowBattery;
  const int64_t localMinute = localUnix / 60;
  const bool dispatchClaimed =
      clockSet && periodicRetainedClaimDispatchMinute(localMinute);

  PeriodicWakeSession session(localUnix, retained.lastDispatchMinute, clockSet,
                              retained.policy, retained.wifiPreferred,
                              lowBattery);
  bool wakeDevice = false;
  bool keepPeriodicDisplay = false;

  if (dispatchClaimed && !tasksSuppressed && session.hasPendingTasks() &&
      session.nextWakeLevel() == PeriodicWakeLevel::Basic) {
    const PeriodicDisplayMode mode =
        session.displayModeFor(PeriodicWakeLevel::Basic);
    initializeDisplay(mode, sleepClock);
    session.dispatch(PeriodicWakeLevel::Basic,
                     mode == PeriodicDisplayMode::None ? nullptr : &display,
                     false);
    const PeriodicDisplayMode update = session.takeDisplayUpdate();
    flushDisplay(update);
    keepPeriodicDisplay = update != PeriodicDisplayMode::None;
  }

  const bool retainedPinkDue =
      retained.policy.minIntervalMinutes != 0 && dispatchClaimed &&
      !tasksSuppressed && retained.pinkRegistered &&
      retained.pinkNextDueMinute >= 0 &&
      localMinute >= retained.pinkNextDueMinute;
  if (dispatchClaimed && !tasksSuppressed &&
      (session.hasPendingTasks() || retainedPinkDue)) {
    systemStage.initialize();
    PeriodicWakePolicy activePolicy =
        periodicPolicyFromConfig(currentSystemConfig());
    setCpuFrequencyMhz(PERIODIC_TASK_CPU_MHZ);

    PinkPeriodicPlan pinkPlan;
    const int64_t pinkPreviousMinute =
        retained.pinkNextDueMinute >= 0 ? retained.pinkNextDueMinute - 1
                                        : retained.lastDispatchMinute;
    if (sdStorageMounted()) {
      pinkPlan = pinkPeriodicPlan(
          localMinute, pinkPreviousMinute, activePolicy.minIntervalMinutes);
    } else if (retained.pinkRegistered) {
      // A removed card is silent and retried infrequently, not every minute.
      pinkPlan.anyRegistered = true;
      pinkPlan.nextDueMinute = localMinute + 60;
    }
    periodicRetainedUpdate(activePolicy, pinkPlan.anyRegistered,
                           pinkPlan.nextDueMinute);

    PeriodicDisplayMode systemDisplay = PeriodicDisplayMode::None;
    if (session.hasPendingTasks() &&
        session.nextWakeLevel() == PeriodicWakeLevel::System) {
      systemDisplay = session.displayModeFor(PeriodicWakeLevel::System);
    }
    if (pinkPlan.dueAtSystem) {
      systemDisplay =
          maximumDisplayMode(systemDisplay, pinkPlan.systemDisplayMode);
    }
    initializeDisplay(systemDisplay, sleepClock);

    if (session.hasPendingTasks() &&
        session.nextWakeLevel() == PeriodicWakeLevel::System) {
      session.dispatch(PeriodicWakeLevel::System,
                       systemDisplay == PeriodicDisplayMode::None
                           ? nullptr
                           : &display,
                       false);
      const PeriodicDisplayMode update = session.takeDisplayUpdate();
      flushDisplay(update);
      keepPeriodicDisplay =
          keepPeriodicDisplay || update != PeriodicDisplayMode::None;
    }
    if (pinkPlan.dueAtSystem) {
      const PinkPeriodicRunResult result = pinkRunDuePeriodic(
          localUnix, pinkPreviousMinute, activePolicy.minIntervalMinutes,
          PeriodicWakeLevel::System,
          systemDisplay == PeriodicDisplayMode::None ? nullptr : &display);
      if (result.displayUpdated) {
        flushDisplay(pinkPlan.systemDisplayMode);
        keepPeriodicDisplay = true;
      }
      wakeDevice = wakeDevice || result.wakeDevice;
      if (result.wakeAppId[0] != '\0') {
        periodicRetainedRememberWakeApp(result.wakeAppId);
      }
    }

    const bool networkDue =
        (session.hasPendingTasks() &&
         session.nextWakeLevel() == PeriodicWakeLevel::Network) ||
        pinkPlan.dueAtNetwork;
    if (networkDue) {
      initializeNetworkStage(retained.wifiPreferred);
      PeriodicDisplayMode networkDisplay = PeriodicDisplayMode::None;
      if (session.hasPendingTasks() &&
          session.nextWakeLevel() == PeriodicWakeLevel::Network) {
        networkDisplay = session.displayModeFor(PeriodicWakeLevel::Network);
      }
      if (pinkPlan.dueAtNetwork) {
        networkDisplay =
            maximumDisplayMode(networkDisplay, pinkPlan.networkDisplayMode);
      }
      initializeDisplay(networkDisplay, sleepClock);

      if (session.hasPendingTasks() &&
          session.nextWakeLevel() == PeriodicWakeLevel::Network) {
        session.dispatch(PeriodicWakeLevel::Network,
                         networkDisplay == PeriodicDisplayMode::None
                             ? nullptr
                             : &display,
                         wifiIsConnected());
        const PeriodicDisplayMode update = session.takeDisplayUpdate();
        flushDisplay(update);
        keepPeriodicDisplay =
            keepPeriodicDisplay || update != PeriodicDisplayMode::None;
      }
      if (pinkPlan.dueAtNetwork) {
        const PinkPeriodicRunResult result = pinkRunDuePeriodic(
            localUnix, pinkPreviousMinute, activePolicy.minIntervalMinutes,
            PeriodicWakeLevel::Network,
            networkDisplay == PeriodicDisplayMode::None ? nullptr : &display);
        if (result.displayUpdated) {
          flushDisplay(pinkPlan.networkDisplayMode);
          keepPeriodicDisplay = true;
        }
        wakeDevice = wakeDevice || result.wakeDevice;
        if (result.wakeAppId[0] != '\0') {
          periodicRetainedRememberWakeApp(result.wakeAppId);
        }
      }
    }

    if (sdStorageMounted()) {
      pinkPlan = pinkPeriodicPlan(
          localMinute, localMinute, activePolicy.minIntervalMinutes);
      periodicRetainedUpdate(activePolicy, pinkPlan.anyRegistered,
                             pinkPlan.nextDueMinute);
    }
  }

  wakeDevice = wakeDevice || session.shouldWakeDevice();
  if (hooks.batteryCutoffReached != nullptr &&
      hooks.batteryCutoffReached()) {
    initializeDisplay(PeriodicDisplayMode::Partial, sleepClock);
    if (hooks.shutdownForLowBattery != nullptr) {
      hooks.shutdownForLowBattery();
    }
    return true;
  }
  if (wakeDevice) {
    return false;
  }

  if (!keepPeriodicDisplay) {
    initializeDisplay(PeriodicDisplayMode::Partial, sleepClock);
    renderSleepClockScreen(
        initializedDisplay == PeriodicDisplayMode::Partial,
        sleepClock.timeFormat);
  }

  if (!deviceClock.snapshotLocalUnix(localUnix)) {
    clockSet = false;
  }
  const uint16_t intervalSeconds = wakeIntervalSeconds(clockSet, localUnix);
  sleepClockContextUpdate(clockSet, localUnix,
                          deviceClock.utcOffsetSeconds(), intervalSeconds,
                          sleepClock.timeFormat, !keepPeriodicDisplay);
  prepareForPowerTransition();
  enterDeepSleep(wakeIntervalUs(intervalSeconds), kRetainEpdPower);
  rebootDevice();
  return true;
}

bool PeriodicWakeService::displayInitialized() const {
  return initializedDisplay != PeriodicDisplayMode::None;
}
