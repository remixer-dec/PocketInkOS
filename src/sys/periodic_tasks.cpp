#include "sys/periodic_tasks.h"

#include "sys/builtin_apps.h"
#include "sys/services/battery_monitor.h"
#include "sys/drivers/usb_status.h"

#include <Arduino.h>

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif

#include <cstddef>
#include <cstring>

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR RTC_DATA_ATTR
#endif

#ifndef RTC_FAST_ATTR
#define RTC_FAST_ATTR RTC_NOINIT_ATTR
#endif

namespace {

constexpr uint32_t kPeriodicContextMagic = 0x50544358UL; // PTCX
constexpr uint32_t kPeriodicStatsMagic = 0x50545354UL;   // PTST
constexpr size_t kMaxSystemTasks = 8;
constexpr size_t kMaxTrackedTaskStats = 24;

static_assert(sizeof(PeriodicTaskStats) == 8,
              "RTC periodic stats must stay compact");

struct StoredPeriodicContext {
  uint32_t magic;
  uint16_t checksum;
  uint16_t minIntervalMinutes;
  uint8_t timeSyncIntervalHours;
  int64_t lastDispatchMinute;
  int64_t pinkNextDueMinute;
  uint32_t pendingWakeAppHash;
  uint8_t disableOnLowBattery;
  uint8_t wifiPreferred;
  uint8_t pinkRegistered;
};

RTC_FAST_ATTR StoredPeriodicContext retainedPeriodicContext;

struct StoredPeriodicStatsContext {
  uint32_t magic;
  uint16_t checksum;
  uint8_t count;
  uint8_t nextReplacement;
  uint16_t inFlightIdHash;
  uint16_t interruptedIdHash;
  PeriodicTaskStats tasks[kMaxTrackedTaskStats];
};

RTC_FAST_ATTR StoredPeriodicStatsContext retainedPeriodicStats;

const PeriodicSystemTaskDefinition *systemTasks[kMaxSystemTasks] = {};
size_t systemTaskCount = 0;

uint16_t fnv1a16(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return static_cast<uint16_t>((hash >> 16) ^ (hash & 0xffffU));
}

uint16_t retainedChecksum(const StoredPeriodicContext &context) {
  return fnv1a16(
      reinterpret_cast<const uint8_t *>(&context.minIntervalMinutes),
      sizeof(context) - offsetof(StoredPeriodicContext, minIntervalMinutes));
}

uint16_t statsChecksum(const StoredPeriodicStatsContext &context) {
  return fnv1a16(reinterpret_cast<const uint8_t *>(&context.count),
                 sizeof(context) -
                     offsetof(StoredPeriodicStatsContext, count));
}

bool retainedValid() {
  return retainedPeriodicContext.magic == kPeriodicContextMagic &&
         retainedPeriodicContext.checksum ==
             retainedChecksum(retainedPeriodicContext);
}

bool statsValid() {
  return retainedPeriodicStats.magic == kPeriodicStatsMagic &&
         retainedPeriodicStats.count <= kMaxTrackedTaskStats &&
         retainedPeriodicStats.nextReplacement < kMaxTrackedTaskStats &&
         retainedPeriodicStats.checksum == statsChecksum(retainedPeriodicStats);
}

void ensureStatsValid() {
  if (statsValid()) {
    return;
  }
  StoredPeriodicStatsContext empty = {};
  empty.magic = kPeriodicStatsMagic;
  empty.checksum = statsChecksum(empty);
  retainedPeriodicStats = empty;
}

void writeRetained(const PeriodicRetainedSnapshot &snapshot) {
  StoredPeriodicContext next = {};
  next.magic = kPeriodicContextMagic;
  next.minIntervalMinutes = snapshot.policy.minIntervalMinutes;
  next.timeSyncIntervalHours = snapshot.policy.timeSyncIntervalHours;
  next.lastDispatchMinute = snapshot.lastDispatchMinute;
  next.pinkNextDueMinute = snapshot.pinkNextDueMinute;
  next.pendingWakeAppHash = snapshot.pendingWakeAppHash;
  next.disableOnLowBattery = snapshot.policy.disableOnLowBattery ? 1U : 0U;
  next.wifiPreferred = snapshot.wifiPreferred ? 1U : 0U;
  next.pinkRegistered = snapshot.pinkRegistered ? 1U : 0U;
  next.checksum = retainedChecksum(next);
  retainedPeriodicContext = next;
}

PeriodicDisplayMode maximumDisplayMode(PeriodicDisplayMode a,
                                       PeriodicDisplayMode b) {
  return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

PeriodicWakeLevel normalizedLevel(PeriodicWakeLevel level) {
  const uint8_t raw = static_cast<uint8_t>(level);
  if (raw >= static_cast<uint8_t>(PeriodicWakeLevel::Network)) {
    return PeriodicWakeLevel::Network;
  }
  if (raw >= static_cast<uint8_t>(PeriodicWakeLevel::System)) {
    return PeriodicWakeLevel::System;
  }
  return PeriodicWakeLevel::Basic;
}

PeriodicDisplayMode normalizedDisplayMode(PeriodicDisplayMode mode) {
  const uint8_t raw = static_cast<uint8_t>(mode);
  if (raw >= static_cast<uint8_t>(PeriodicDisplayMode::Full)) {
    return PeriodicDisplayMode::Full;
  }
  return raw >= static_cast<uint8_t>(PeriodicDisplayMode::Partial)
             ? PeriodicDisplayMode::Partial
             : PeriodicDisplayMode::None;
}

struct RegisteredTaskView {
  const char *id = nullptr;
  const PeriodicTaskSchedule *schedule = nullptr;
  AppPeriodicHandler handler = nullptr;
  bool appOwned = false;
};

size_t registeredTaskCount() {
  return systemTaskCount + builtInAppDefinitionCount();
}

bool registeredTaskAt(size_t index, RegisteredTaskView &out) {
  out = RegisteredTaskView();
  if (index < systemTaskCount) {
    const PeriodicSystemTaskDefinition &task = *systemTasks[index];
    out.id = task.id;
    out.schedule = &task.schedule;
    out.handler = task.onPeriodic;
    return true;
  }

  const AppDefinition *app =
      builtInAppDefinitionAt(index - systemTaskCount);
  if (app == nullptr) {
    return false;
  }
  out.id = app->id;
  out.schedule = &app->behavior.periodic;
  out.handler = app->behavior.onPeriodic;
  out.appOwned = true;
  return true;
}

uint32_t taskIdHash32(const char *id) {
  uint32_t hash = 2166136261UL;
  for (const char *cursor = id != nullptr ? id : ""; *cursor != '\0';
       cursor++) {
    hash ^= static_cast<uint8_t>(*cursor);
    hash *= 16777619UL;
  }
  return hash == 0 ? 1 : hash;
}

uint16_t taskIdHash(const char *id) {
  const uint32_t hash = taskIdHash32(id);
  const uint16_t folded =
      static_cast<uint16_t>((hash >> 16) ^ (hash & 0xffffU));
  return folded == 0 ? 1 : folded;
}

uint16_t batteryMillivolts() {
  const BatterySnapshot &battery = batteryMonitor.snapshot();
  if (!battery.valid || battery.voltage <= 0.0f) {
    return 0;
  }
  const float millivolts = battery.voltage * 1000.0f;
  return millivolts >= 65535.0f
             ? UINT16_MAX
             : static_cast<uint16_t>(millivolts + 0.5f);
}

} // namespace

uint16_t periodicEffectiveIntervalMinutes(uint16_t requestedMinutes,
                                          uint16_t policyMinimumMinutes) {
  if (requestedMinutes == 0 || policyMinimumMinutes == 0) {
    return 0;
  }
  uint16_t requested = requestedMinutes;
  if (requested < CONFIG_PERIODIC_INTERVAL_MIN_MINUTES) {
    requested = CONFIG_PERIODIC_INTERVAL_MIN_MINUTES;
  } else if (requested > CONFIG_PERIODIC_INTERVAL_MAX_MINUTES) {
    requested = CONFIG_PERIODIC_INTERVAL_MAX_MINUTES;
  }

  uint16_t minimum = policyMinimumMinutes;
  if (minimum < CONFIG_PERIODIC_INTERVAL_MIN_MINUTES) {
    minimum = CONFIG_PERIODIC_INTERVAL_MIN_MINUTES;
  } else if (minimum > CONFIG_PERIODIC_INTERVAL_MAX_MINUTES) {
    minimum = CONFIG_PERIODIC_INTERVAL_MAX_MINUTES;
  }
  return requested < minimum ? minimum : requested;
}

bool periodicTaskDueAtMinute(int64_t localMinute, uint16_t intervalMinutes) {
  if (intervalMinutes == 0) {
    return false;
  }
  int64_t remainder = localMinute % intervalMinutes;
  if (remainder < 0) {
    remainder += intervalMinutes;
  }
  return remainder == 0;
}

bool periodicTaskDueBetweenMinutes(int64_t previousMinute,
                                   int64_t currentMinute,
                                   uint16_t intervalMinutes) {
  if (intervalMinutes == 0 || currentMinute <= previousMinute) {
    return false;
  }
  return periodicNextDueMinute(previousMinute, intervalMinutes) <=
         currentMinute;
}

int64_t periodicNextDueMinute(int64_t currentMinute, uint16_t intervalMinutes) {
  if (intervalMinutes == 0) {
    return -1;
  }
  int64_t remainder = currentMinute % intervalMinutes;
  if (remainder < 0) {
    remainder += intervalMinutes;
  }
  const int64_t delta = remainder == 0 ? intervalMinutes
                                       : intervalMinutes - remainder;
  return currentMinute + delta;
}

PeriodicWakePolicy periodicPolicyFromConfig(const SystemConfig &config) {
  PeriodicWakePolicy policy;
  policy.minIntervalMinutes = config.periodic.minIntervalMinutes == 0
                                  ? 0
                                  : periodicEffectiveIntervalMinutes(
                                        config.periodic.minIntervalMinutes, 1);
  policy.timeSyncIntervalHours =
      config.time.syncIntervalHours <= 24
          ? config.time.syncIntervalHours
          : 24;
  policy.disableOnLowBattery = config.periodic.disableOnLowBattery;
  return policy;
}

void periodicSystemTasksReset() {
  for (size_t i = 0; i < kMaxSystemTasks; i++) {
    systemTasks[i] = nullptr;
  }
  systemTaskCount = 0;
}

bool periodicSystemTaskRegister(const PeriodicSystemTaskDefinition &task) {
  if (task.id == nullptr || task.id[0] == '\0' || task.onPeriodic == nullptr ||
      task.schedule.intervalMinutes == 0 ||
      task.schedule.intervalMinutes > CONFIG_PERIODIC_INTERVAL_MAX_MINUTES) {
    return false;
  }
  for (size_t i = 0; i < systemTaskCount; i++) {
    if (strcmp(systemTasks[i]->id, task.id) == 0) {
      systemTasks[i] = &task;
      return true;
    }
  }
  if (systemTaskCount >= kMaxSystemTasks) {
    return false;
  }
  systemTasks[systemTaskCount++] = &task;
  return true;
}

PeriodicTaskMeasurement periodicTaskMeasurementStart(const char *taskId) {
  PeriodicTaskMeasurement measurement;
  ensureStatsValid();
  const uint16_t idHash = taskIdHash(taskId);
  if (retainedPeriodicStats.inFlightIdHash != 0) {
    retainedPeriodicStats.interruptedIdHash =
        retainedPeriodicStats.inFlightIdHash;
    retainedPeriodicStats.inFlightIdHash = 0;
  }
  if (retainedPeriodicStats.interruptedIdHash == idHash) {
    retainedPeriodicStats.interruptedIdHash = 0;
    retainedPeriodicStats.checksum = statsChecksum(retainedPeriodicStats);
    measurement.shouldRun = false;
    return measurement;
  }
  retainedPeriodicStats.inFlightIdHash = idHash;
  retainedPeriodicStats.checksum = statsChecksum(retainedPeriodicStats);
  measurement.startedAtMs = millis();
  measurement.batteryStartMv = batteryMillivolts();
  measurement.usbConnectedAtStart = usbConnected();
  return measurement;
}

void periodicTaskMeasurementFinish(
    const char *taskId, const PeriodicTaskMeasurement &measurement) {
  if (!measurement.shouldRun) {
    return;
  }
  const uint32_t elapsedMs = millis() - measurement.startedAtMs;
  batteryMonitor.refresh();
  const uint16_t endMv = batteryMillivolts();

  ensureStatsValid();
  const uint16_t idHash = taskIdHash(taskId);
  if (retainedPeriodicStats.inFlightIdHash == idHash) {
    retainedPeriodicStats.inFlightIdHash = 0;
  }
  size_t slot = kMaxTrackedTaskStats;
  for (size_t i = 0; i < retainedPeriodicStats.count; i++) {
    if (retainedPeriodicStats.tasks[i].idHash == idHash) {
      slot = i;
      break;
    }
  }
  if (slot == kMaxTrackedTaskStats) {
    if (retainedPeriodicStats.count < kMaxTrackedTaskStats) {
      slot = retainedPeriodicStats.count++;
    } else {
      slot = retainedPeriodicStats.nextReplacement;
      retainedPeriodicStats.nextReplacement = static_cast<uint8_t>(
          (retainedPeriodicStats.nextReplacement + 1U) %
          kMaxTrackedTaskStats);
    }
    retainedPeriodicStats.tasks[slot] = PeriodicTaskStats();
    retainedPeriodicStats.tasks[slot].idHash = idHash;
  }

  PeriodicTaskStats &stats = retainedPeriodicStats.tasks[slot];
  const uint32_t elapsed10Ms = elapsedMs == 0 ? 0 : (elapsedMs + 9U) / 10U;
  if (elapsed10Ms > UINT16_MAX ||
      static_cast<uint32_t>(UINT16_MAX - stats.totalDuration10Ms) <
          elapsed10Ms) {
    stats.totalDuration10Ms = UINT16_MAX;
  } else {
    stats.totalDuration10Ms =
        static_cast<uint16_t>(stats.totalDuration10Ms + elapsed10Ms);
  }
  if (stats.runCount < UINT16_MAX) {
    stats.runCount++;
  }
  if (!measurement.usbConnectedAtStart && !usbConnected() && endMv != 0 &&
      measurement.batteryStartMv > endMv) {
    const uint16_t drop =
        static_cast<uint16_t>(measurement.batteryStartMv - endMv);
    if (UINT16_MAX - stats.totalBatteryDropMv < drop) {
      stats.totalBatteryDropMv = UINT16_MAX;
    } else {
      stats.totalBatteryDropMv =
          static_cast<uint16_t>(stats.totalBatteryDropMv + drop);
    }
  }
  retainedPeriodicStats.checksum = statsChecksum(retainedPeriodicStats);
}

size_t periodicTaskStatsCount() {
  ensureStatsValid();
  return retainedPeriodicStats.count;
}

bool periodicTaskStatsAt(size_t index, PeriodicTaskStats &out) {
  ensureStatsValid();
  if (index >= retainedPeriodicStats.count) {
    return false;
  }
  out = retainedPeriodicStats.tasks[index];
  return true;
}

void periodicRetainedPrepareForSleep(const PeriodicWakePolicy &policy,
                                     bool wifiPreferred,
                                     bool pinkRegistered,
                                     int64_t pinkNextDueMinute) {
  PeriodicRetainedSnapshot snapshot;
  if (retainedValid()) {
    periodicRetainedLoad(snapshot);
  }
  snapshot.policy = policy;
  snapshot.wifiPreferred = wifiPreferred;
  snapshot.pinkRegistered = pinkRegistered;
  snapshot.pinkNextDueMinute = pinkNextDueMinute;
  snapshot.pendingWakeAppHash = 0;
  writeRetained(snapshot);
}

bool periodicRetainedLoad(PeriodicRetainedSnapshot &snapshot) {
  if (!retainedValid()) {
    return false;
  }
  snapshot.policy.minIntervalMinutes =
      retainedPeriodicContext.minIntervalMinutes;
  snapshot.policy.timeSyncIntervalHours =
      retainedPeriodicContext.timeSyncIntervalHours <= 24
          ? retainedPeriodicContext.timeSyncIntervalHours
          : 24;
  snapshot.policy.disableOnLowBattery =
      retainedPeriodicContext.disableOnLowBattery != 0;
  snapshot.lastDispatchMinute = retainedPeriodicContext.lastDispatchMinute;
  snapshot.pinkNextDueMinute = retainedPeriodicContext.pinkNextDueMinute;
  snapshot.pendingWakeAppHash = retainedPeriodicContext.pendingWakeAppHash;
  snapshot.wifiPreferred = retainedPeriodicContext.wifiPreferred != 0;
  snapshot.pinkRegistered = retainedPeriodicContext.pinkRegistered != 0;
  return true;
}

void periodicRetainedUpdate(const PeriodicWakePolicy &policy,
                            bool pinkRegistered, int64_t pinkNextDueMinute) {
  PeriodicRetainedSnapshot snapshot;
  if (!periodicRetainedLoad(snapshot)) {
    snapshot = PeriodicRetainedSnapshot();
  }
  snapshot.policy = policy;
  snapshot.pinkRegistered = pinkRegistered;
  snapshot.pinkNextDueMinute = pinkNextDueMinute;
  writeRetained(snapshot);
}

bool periodicRetainedClaimDispatchMinute(int64_t localMinute) {
  PeriodicRetainedSnapshot snapshot;
  if (!periodicRetainedLoad(snapshot)) {
    return false;
  }
  if (snapshot.lastDispatchMinute == localMinute) {
    return false;
  }
  snapshot.lastDispatchMinute = localMinute;
  writeRetained(snapshot);
  return true;
}

void periodicRetainedRememberWakeApp(const char *appId) {
  if (appId == nullptr || appId[0] == '\0') {
    return;
  }
  PeriodicRetainedSnapshot snapshot;
  if (!periodicRetainedLoad(snapshot)) {
    return;
  }
  snapshot.pendingWakeAppHash = taskIdHash32(appId);
  writeRetained(snapshot);
}

bool periodicRetainedTakeWakeApp(char *appId, size_t appIdSize) {
  if (appId == nullptr || appIdSize == 0) {
    return false;
  }
  appId[0] = '\0';
  PeriodicRetainedSnapshot snapshot;
  if (!periodicRetainedLoad(snapshot) || snapshot.pendingWakeAppHash == 0) {
    return false;
  }
  const uint32_t pendingHash = snapshot.pendingWakeAppHash;
  snapshot.pendingWakeAppHash = 0;
  writeRetained(snapshot);

  const size_t count = appCatalogAllCount();
  for (size_t i = 0; i < count; i++) {
    AppCatalogEntry app;
    if (!tryGetAppAtIndex(i, app) || app.definition.id == nullptr ||
        taskIdHash32(app.definition.id) != pendingHash) {
      continue;
    }
    strncpy(appId, app.definition.id, appIdSize - 1);
    appId[appIdSize - 1] = '\0';
    return true;
  }
  return false;
}

PeriodicWakeSession::PeriodicWakeSession(
    int64_t nextLocalUnix, int64_t previousDispatchMinute, bool nextClockSet,
    const PeriodicWakePolicy &nextPolicy, bool nextWifiPreferred,
    bool nextLowBattery)
    : policy(nextPolicy), localUnix(nextLocalUnix),
      minute(nextLocalUnix / 60), clockSet(nextClockSet),
      wifiPreferred(nextWifiPreferred), lowBattery(nextLowBattery) {
  if (!clockSet || policy.minIntervalMinutes == 0 ||
      (policy.disableOnLowBattery && lowBattery)) {
    return;
  }

  const size_t count = registeredTaskCount();
  const size_t limit = count < kMaxBuiltInTasks ? count : kMaxBuiltInTasks;
  for (size_t i = 0; i < limit; i++) {
    RegisteredTaskView task;
    if (!registeredTaskAt(i, task) || task.handler == nullptr ||
        task.schedule == nullptr || task.schedule->intervalMinutes == 0) {
      continue;
    }
    const uint16_t interval = periodicEffectiveIntervalMinutes(
        task.schedule->intervalMinutes, policy.minIntervalMinutes);
    if (!periodicTaskDueBetweenMinutes(previousDispatchMinute, minute,
                                       interval)) {
      continue;
    }
    maskFor(normalizedLevel(task.schedule->wakeLevel)) |= (1ULL << i);
  }
}

uint64_t &PeriodicWakeSession::maskFor(PeriodicWakeLevel level) {
  if (level == PeriodicWakeLevel::Network) {
    return networkMask;
  }
  if (level == PeriodicWakeLevel::System) {
    return systemMask;
  }
  return basicMask;
}

uint64_t PeriodicWakeSession::maskFor(PeriodicWakeLevel level) const {
  if (level == PeriodicWakeLevel::Network) {
    return networkMask;
  }
  if (level == PeriodicWakeLevel::System) {
    return systemMask;
  }
  return basicMask;
}

PeriodicWakeLevel PeriodicWakeSession::nextWakeLevel() const {
  if (basicMask != 0) {
    return PeriodicWakeLevel::Basic;
  }
  if (systemMask != 0) {
    return PeriodicWakeLevel::System;
  }
  return networkMask != 0 ? PeriodicWakeLevel::Network
                          : PeriodicWakeLevel::Basic;
}

PeriodicDisplayMode
PeriodicWakeSession::displayModeFor(PeriodicWakeLevel level) const {
  PeriodicDisplayMode mode = PeriodicDisplayMode::None;
  uint64_t runnable = maskFor(level);
  const size_t count = registeredTaskCount();
  const size_t limit = count < kMaxBuiltInTasks ? count : kMaxBuiltInTasks;
  for (size_t i = 0; i < limit; i++) {
    if ((runnable & (1ULL << i)) == 0) {
      continue;
    }
    RegisteredTaskView task;
    if (registeredTaskAt(i, task) && task.schedule != nullptr) {
      mode = maximumDisplayMode(
          mode, normalizedDisplayMode(task.schedule->displayMode));
    }
  }
  return mode;
}

void PeriodicWakeSession::dispatch(PeriodicWakeLevel level,
                                   Adafruit_GFX *display,
                                   bool wifiConnected) {
  lastDisplayUpdate = PeriodicDisplayMode::None;
  uint64_t runnable = maskFor(level);
  maskFor(level) = 0;

  const size_t count = registeredTaskCount();
  const size_t limit = count < kMaxBuiltInTasks ? count : kMaxBuiltInTasks;
  for (size_t i = 0; i < limit; i++) {
    const uint64_t bit = 1ULL << i;
    if ((runnable & bit) == 0) {
      continue;
    }
    RegisteredTaskView task;
    if (!registeredTaskAt(i, task) || task.handler == nullptr ||
        task.schedule == nullptr) {
      continue;
    }

    PeriodicTaskContext context;
    context.localUnix = localUnix;
    context.intervalMinutes = periodicEffectiveIntervalMinutes(
        task.schedule->intervalMinutes, policy.minIntervalMinutes);
    context.wakeLevel = level;
    context.displayMode = normalizedDisplayMode(task.schedule->displayMode);
    context.clockSet = clockSet;
    context.wifiPreferred = wifiPreferred;
    context.wifiConnected = wifiConnected;
    context.lowBattery = lowBattery;
    context.display = context.displayMode == PeriodicDisplayMode::None
                          ? nullptr
                          : display;

    const PeriodicTaskMeasurement measurement =
        periodicTaskMeasurementStart(task.id);
    if (!measurement.shouldRun) {
      continue;
    }
    const PeriodicTaskAction action = task.handler(context);
    periodicTaskMeasurementFinish(task.id, measurement);
    if (action == PeriodicTaskAction::RequestSystem &&
        level == PeriodicWakeLevel::Basic) {
      systemMask |= bit;
      continue;
    }
    if (action == PeriodicTaskAction::RequestNetwork &&
        level != PeriodicWakeLevel::Network) {
      networkMask |= bit;
      continue;
    }
    if (action == PeriodicTaskAction::WakeDevice ||
        action == PeriodicTaskAction::WakeApp) {
      wakeDevice = true;
    }
    if (action == PeriodicTaskAction::WakeApp &&
        task.appOwned && pendingWakeAppId[0] == '\0' && task.id != nullptr) {
      strncpy(pendingWakeAppId, task.id, sizeof(pendingWakeAppId) - 1);
      pendingWakeAppId[sizeof(pendingWakeAppId) - 1] = '\0';
      periodicRetainedRememberWakeApp(task.id);
    }
    if (context.displayMode != PeriodicDisplayMode::None) {
      lastDisplayUpdate =
          maximumDisplayMode(lastDisplayUpdate, context.displayMode);
    }
  }
}

PeriodicDisplayMode PeriodicWakeSession::takeDisplayUpdate() {
  const PeriodicDisplayMode update = lastDisplayUpdate;
  lastDisplayUpdate = PeriodicDisplayMode::None;
  return update;
}

bool PeriodicWakeSession::hasPendingTasks() const {
  return basicMask != 0 || systemMask != 0 || networkMask != 0;
}

bool PeriodicWakeSession::shouldWakeDevice() const { return wakeDevice; }

const char *PeriodicWakeSession::wakeAppId() const {
  return pendingWakeAppId[0] != '\0' ? pendingWakeAppId : nullptr;
}

int64_t PeriodicWakeSession::localMinute() const { return minute; }
