#ifndef SLEEP_CLOCK_CONTEXT_H
#define SLEEP_CLOCK_CONTEXT_H

#include <stdint.h>

struct SleepClockSnapshot {
  bool active;
  bool clockSet;
  int64_t localUnix;
  uint16_t wakeIntervalSeconds;
};

void sleepClockContextStart(bool clockSet, int64_t localUnix,
                            uint16_t wakeIntervalSeconds);
bool sleepClockContextLoad(SleepClockSnapshot &snapshot);
void sleepClockContextUpdate(bool clockSet, int64_t localUnix,
                             uint16_t wakeIntervalSeconds);
void sleepClockContextClear();

#endif
