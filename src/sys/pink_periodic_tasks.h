#ifndef PINK_PERIODIC_TASKS_H
#define PINK_PERIODIC_TASKS_H

#include "sys/app_runtime.h"

#include <stdint.h>

/// Summary of installed .pink hooks due in one wake window.
struct PinkPeriodicPlan {
  bool anyRegistered = false;
  bool dueAtSystem = false;
  bool dueAtNetwork = false;
  int64_t nextDueMinute = -1;
  PeriodicDisplayMode systemDisplayMode = PeriodicDisplayMode::None;
  PeriodicDisplayMode networkDisplayMode = PeriodicDisplayMode::None;
};

/// Combined outcome of the due .pink hooks invoked at one wake tier.
struct PinkPeriodicRunResult {
  bool wakeDevice = false;
  bool displayUpdated = false;
  char wakeAppId[24] = {};
};

/// Re-scans installed artifacts and plans their valid static schedules.
PinkPeriodicPlan pinkPeriodicPlan(int64_t localMinute,
                                  int64_t previousMinute,
                                  uint16_t minIntervalMinutes);
/// Invokes the due hooks at one initialized tier using the last plan's scan.
PinkPeriodicRunResult pinkRunDuePeriodic(
    int64_t localUnix, int64_t previousMinute,
    uint16_t minIntervalMinutes, PeriodicWakeLevel wakeLevel,
    Adafruit_GFX *display);

#endif
