#ifndef PERIODIC_WAKE_SERVICE_H
#define PERIODIC_WAKE_SERVICE_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/periodic_tasks.h"
#include "sys/sleep_clock_context.h"

class SystemStageService;

/// App-level actions needed when a periodic wake reaches a terminal state.
struct PeriodicWakeHooks {
  /// Reports whether the current battery level requires an immediate shutdown.
  bool (*batteryCutoffReached)() = nullptr;
  /// Displays the low-battery state and powers the device down.
  void (*shutdownForLowBattery)() = nullptr;
  /// Saves app-owned state before shutdown or deep sleep.
  void (*prepareForPowerTransition)() = nullptr;
};

/// Coordinates the staged hardware initialization and dispatch for timer wakes.
/// Scheduling and retained-state rules remain in periodic_tasks.
class PeriodicWakeService {
public:
  /// Binds the shared display, staged initializer, and app lifecycle hooks.
  PeriodicWakeService(AppDisplay &display, SystemStageService &systemStage,
                      const PeriodicWakeHooks &hooks);

  /// Rebuilds the boot-local registry of OS-owned periodic callbacks.
  void registerSystemTasks(uint8_t timeSyncIntervalHours = 24);
  /// Enters the minute-aligned sleep-clock cycle after an interactive session.
  void enterSleepClockDeepSleep();
  /// Handles a sleep-clock timer wake. False continues into a normal boot.
  bool handleTimerWake();

  /// Reports whether this wake initialized the display hardware.
  bool displayInitialized() const;

private:
  AppDisplay &display;
  SystemStageService &systemStage;
  PeriodicWakeHooks hooks;
  PeriodicDisplayMode initializedDisplay = PeriodicDisplayMode::None;

  static PeriodicTaskAction
  runTimeSync(const PeriodicTaskContext &context);
  static uint16_t wakeIntervalSeconds(bool clockSet, int64_t localUnix);
  static uint64_t wakeIntervalUs(uint16_t wakeIntervalSeconds);
  static bool syncClockFromRtc(int32_t utcOffsetSeconds);
  static bool waitForRtcMinuteChangeIfNeeded();

  void initializeNetworkStage(bool wifiPreferred);
  void initializeDisplay(PeriodicDisplayMode mode,
                         const SleepClockSnapshot &sleepClock);
  void flushDisplay(PeriodicDisplayMode mode);
  void seedSleepClockPreviousFrame(const SleepClockSnapshot &sleepClock);
  void renderSleepClockScreen(bool partialUpdate, ConfigTimeFormat timeFormat,
                              bool timeEstimated);
  bool finishTimerWake(const SleepClockSnapshot &sleepClock, bool clockSet,
                       int64_t localUnix, bool keepPeriodicDisplay);
  void prepareForPowerTransition();
};

#endif
