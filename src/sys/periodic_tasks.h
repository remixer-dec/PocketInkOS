#ifndef PERIODIC_TASKS_H
#define PERIODIC_TASKS_H

#include "sys/app_runtime.h"
#include "sys/system_config.h"

#include <stddef.h>
#include <stdint.h>

/// Runtime policy copied from system config and retained for level-1 wakes.
struct PeriodicWakePolicy {
  /// Zero disables all periodic callbacks while retaining the sleep clock.
  uint16_t minIntervalMinutes = 1;
  uint8_t timeSyncIntervalHours = 24;
  bool disableOnLowBattery = true;
};

/// Small POD summary retained across deep sleep; no runtime pointers are kept.
struct PeriodicRetainedSnapshot {
  PeriodicWakePolicy policy;
  int64_t lastDispatchMinute = -1;
  int64_t pinkNextDueMinute = -1;
  uint32_t pendingWakeAppHash = 0;
  bool wifiPreferred = false;
  bool pinkRegistered = false;
};

/// One OS-owned task registration. The registry is rebuilt early on every boot.
struct PeriodicSystemTaskDefinition {
  const char *id = nullptr;
  PeriodicTaskSchedule schedule;
  AppPeriodicHandler onPeriodic = nullptr;
};

/// Compact aggregate retained for profiling without storing task-name strings.
struct PeriodicTaskStats {
  uint16_t idHash = 0;
  /// Saturating cumulative duration in 10 ms units.
  uint16_t totalDuration10Ms = 0;
  uint16_t runCount = 0;
};

/// Start sample used to attribute elapsed time to one hook.
struct PeriodicTaskMeasurement {
  uint32_t startedAtMs = 0;
  /// False when this task was the unfinished callback from an earlier boot.
  bool shouldRun = true;
};

/// Normalizes a requested cadence against the OS policy and 1-minute/1-day limits.
uint16_t periodicEffectiveIntervalMinutes(uint16_t requestedMinutes,
                                          uint16_t policyMinimumMinutes);
/// Uses epoch-aligned whole-minute buckets so schedules do not drift across sleep.
bool periodicTaskDueAtMinute(int64_t localMinute, uint16_t intervalMinutes);
/// Reports whether an aligned due minute falls in (previousMinute, currentMinute].
bool periodicTaskDueBetweenMinutes(int64_t previousMinute,
                                   int64_t currentMinute,
                                   uint16_t intervalMinutes);
/// Returns the first aligned minute strictly after currentMinute.
int64_t periodicNextDueMinute(int64_t currentMinute, uint16_t intervalMinutes);
/// Builds the low-power wake policy from the active system configuration.
PeriodicWakePolicy periodicPolicyFromConfig(const SystemConfig &config);

/// Clears the boot-local OS task registry before system services repopulate it.
void periodicSystemTasksReset();
/// Registers one static-lifetime OS task; duplicate ids replace their pointer.
bool periodicSystemTaskRegister(const PeriodicSystemTaskDefinition &task);

/// Starts/finishes compact profiling around a periodic callback.
PeriodicTaskMeasurement periodicTaskMeasurementStart(const char *taskId);
void periodicTaskMeasurementFinish(const char *taskId,
                                   const PeriodicTaskMeasurement &measurement);
/// Enumerates retained profiling aggregates for diagnostics UIs/tools.
size_t periodicTaskStatsCount();
bool periodicTaskStatsAt(size_t index, PeriodicTaskStats &out);

/// Saves current policy and external-task summary immediately before deep sleep.
void periodicRetainedPrepareForSleep(const PeriodicWakePolicy &policy,
                                     bool wifiPreferred,
                                     bool pinkRegistered,
                                     int64_t pinkNextDueMinute);
/// Loads a validated retained snapshot after a timer wake.
bool periodicRetainedLoad(PeriodicRetainedSnapshot &snapshot);
/// Refreshes policy and external-task summary after level-2 config/SD init.
void periodicRetainedUpdate(const PeriodicWakePolicy &policy,
                            bool pinkRegistered, int64_t pinkNextDueMinute);
/// Atomically claims one minute for dispatch and rejects duplicate timer wakes.
bool periodicRetainedClaimDispatchMinute(int64_t localMinute);
/// Persists a WakeApp outcome before later tasks can crash the same wake.
void periodicRetainedRememberWakeApp(const char *appId);
/// Resolves and clears a retained WakeApp target after the app catalog is ready.
bool periodicRetainedTakeWakeApp(char *appId, size_t appIdSize);

/// One timer-wake dispatch over all statically registered built-in hooks.
class PeriodicWakeSession {
public:
  PeriodicWakeSession(int64_t localUnix, int64_t previousDispatchMinute,
                      bool clockSet,
                      const PeriodicWakePolicy &policy, bool wifiPreferred,
                      bool lowBattery);

  /// Highest-priority initialization tier still required by due hooks.
  PeriodicWakeLevel nextWakeLevel() const;
  /// Maximum display mode requested by hooks runnable at level.
  PeriodicDisplayMode displayModeFor(PeriodicWakeLevel level) const;
  /// Runs hooks assigned to level and records escalation/wake requests.
  void dispatch(PeriodicWakeLevel level, Adafruit_GFX *display,
                bool wifiConnected);
  /// Returns and clears the display update produced by the last dispatch call.
  PeriodicDisplayMode takeDisplayUpdate();

  bool hasPendingTasks() const;
  bool shouldWakeDevice() const;
  const char *wakeAppId() const;
  int64_t localMinute() const;

private:
  static const size_t kMaxBuiltInTasks = 64;

  PeriodicWakePolicy policy;
  int64_t localUnix = 0;
  int64_t minute = 0;
  uint64_t basicMask = 0;
  uint64_t systemMask = 0;
  uint64_t networkMask = 0;
  bool clockSet = false;
  bool wifiPreferred = false;
  bool lowBattery = false;
  bool wakeDevice = false;
  PeriodicDisplayMode lastDisplayUpdate = PeriodicDisplayMode::None;
  char pendingWakeAppId[24] = {};

  uint64_t &maskFor(PeriodicWakeLevel level);
  uint64_t maskFor(PeriodicWakeLevel level) const;
};

#endif
