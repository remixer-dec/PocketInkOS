#ifndef SLEEP_CLOCK_CONTEXT_H
#define SLEEP_CLOCK_CONTEXT_H

#include <stdint.h>

/// Minimal clock context used by periodic sleep/wake clock updates.
struct SleepClockSnapshot {
  /// True when periodic sleep clock mode is active.
  bool active;
  /// Whether localUnix is based on a trusted clock value.
  bool clockSet;
  /// Local Unix timestamp captured for the next wake cycle.
  int64_t localUnix;
  /// Requested wake interval in seconds.
  uint16_t wakeIntervalSeconds;
};

/// Starts periodic sleep clock mode with the current clock state.
void sleepClockContextStart(bool clockSet, int64_t localUnix,
                            uint16_t wakeIntervalSeconds);
/// Loads periodic sleep clock state after wake.
bool sleepClockContextLoad(SleepClockSnapshot &snapshot);
/// Updates periodic sleep clock state before returning to sleep.
void sleepClockContextUpdate(bool clockSet, int64_t localUnix,
                             uint16_t wakeIntervalSeconds);
/// Clears periodic sleep clock state.
void sleepClockContextClear();

#endif
