#include "sys/pink_periodic_tasks.h"

#include "sys/periodic_tasks.h"
#include "sys/pink_executable.h"

#include <cstring>

namespace {

PeriodicDisplayMode maximumDisplayMode(PeriodicDisplayMode a,
                                       PeriodicDisplayMode b) {
  return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

void copyText(char *dest, size_t destSize, const char *source) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  const char *text = source != nullptr ? source : "";
  strncpy(dest, text, destSize - 1);
  dest[destSize - 1] = '\0';
}

} // namespace

PinkPeriodicPlan pinkPeriodicPlan(int64_t localMinute,
                                  int64_t previousMinute,
                                  uint16_t minIntervalMinutes) {
  PinkPeriodicPlan plan;
  pinkExecutableRefreshApps();
  const size_t count = pinkExecutablePeriodicTaskCount();
  for (size_t i = 0; i < count; i++) {
    PinkExecutablePeriodicTask task;
    if (!pinkExecutablePeriodicTaskAt(i, task)) {
      continue;
    }
    plan.anyRegistered = true;
    const uint16_t interval = periodicEffectiveIntervalMinutes(
        task.schedule.intervalMinutes, minIntervalMinutes);
    const int64_t next = periodicNextDueMinute(localMinute, interval);
    if (plan.nextDueMinute < 0 || next < plan.nextDueMinute) {
      plan.nextDueMinute = next;
    }
    if (!periodicTaskDueBetweenMinutes(previousMinute, localMinute,
                                       interval)) {
      continue;
    }
    if (task.schedule.wakeLevel == PeriodicWakeLevel::Network) {
      plan.dueAtNetwork = true;
      plan.networkDisplayMode = maximumDisplayMode(
          plan.networkDisplayMode, task.schedule.displayMode);
    } else {
      plan.dueAtSystem = true;
      plan.systemDisplayMode = maximumDisplayMode(
          plan.systemDisplayMode, task.schedule.displayMode);
    }
  }
  return plan;
}

PinkPeriodicRunResult pinkRunDuePeriodic(
    int64_t localUnix, int64_t previousMinute,
    uint16_t minIntervalMinutes, PeriodicWakeLevel wakeLevel,
    Adafruit_GFX *display) {
  PinkPeriodicRunResult result;
  const int64_t localMinute = localUnix / 60;
  const size_t count = pinkExecutablePeriodicTaskCount();
  for (size_t i = 0; i < count; i++) {
    PinkExecutablePeriodicTask task;
    if (!pinkExecutablePeriodicTaskAt(i, task) ||
        task.schedule.wakeLevel != wakeLevel) {
      continue;
    }
    const uint16_t interval = periodicEffectiveIntervalMinutes(
        task.schedule.intervalMinutes, minIntervalMinutes);
    if (!periodicTaskDueBetweenMinutes(previousMinute, localMinute,
                                       interval)) {
      continue;
    }

    const PeriodicTaskMeasurement measurement =
        periodicTaskMeasurementStart(task.id);
    if (!measurement.shouldRun) {
      continue;
    }
    bool dirty = false;
    const PeriodicTaskAction action = pinkExecutableRunPeriodic(
        task, localUnix, interval,
        task.schedule.displayMode == PeriodicDisplayMode::None ? nullptr
                                                               : display,
        dirty);
    periodicTaskMeasurementFinish(task.id, measurement);
    result.displayUpdated = result.displayUpdated || dirty;
    if (action == PeriodicTaskAction::WakeDevice ||
        action == PeriodicTaskAction::WakeApp) {
      result.wakeDevice = true;
    }
    if (action == PeriodicTaskAction::WakeApp &&
        result.wakeAppId[0] == '\0') {
      copyText(result.wakeAppId, sizeof(result.wakeAppId), task.id);
    }
  }
  return result;
}
