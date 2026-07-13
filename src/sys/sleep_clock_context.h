#ifndef SLEEP_CLOCK_CONTEXT_H
#define SLEEP_CLOCK_CONTEXT_H

#include <stdint.h>

#include "sys/system_config.h"

/// Minimal clock context used by periodic sleep/wake clock updates.
struct SleepClockSnapshot {
  /// True when periodic sleep clock mode is active.
  bool active;
  /// Whether localUnix contains a usable clock value.
  bool clockSet;
  /// Whether localUnix was advanced from retained state after an RTC failure.
  bool timeEstimated;
  /// Local Unix timestamp captured for the next wake cycle.
  int64_t localUnix;
  /// UTC offset paired with localUnix.
  int32_t utcOffsetSeconds;
  /// Requested wake interval in seconds.
  uint16_t wakeIntervalSeconds;
  /// Whether the retained timestamp reconstructs the physical panel frame.
  bool previousFrameValid;
  /// Clock presentation used for the previous e-paper frame.
  ConfigTimeFormat timeFormat;
};

/// Starts periodic sleep clock mode with the current clock state.
void sleepClockContextStart(bool clockSet, bool timeEstimated,
                            int64_t localUnix,
                            int32_t utcOffsetSeconds,
                            uint16_t wakeIntervalSeconds,
                            ConfigTimeFormat timeFormat);
/// Loads periodic sleep clock state after wake.
bool sleepClockContextLoad(SleepClockSnapshot &snapshot);
/// Updates periodic sleep clock state before returning to sleep.
void sleepClockContextUpdate(bool clockSet, bool timeEstimated,
                             int64_t localUnix,
                             int32_t utcOffsetSeconds,
                             uint16_t wakeIntervalSeconds,
                             ConfigTimeFormat timeFormat,
                             bool previousFrameValid = true);
/// Clears periodic sleep clock state.
void sleepClockContextClear();

#endif
