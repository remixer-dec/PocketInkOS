#ifndef INACTIVITY_SLEEP_GUARD_H
#define INACTIVITY_SLEEP_GUARD_H

#include <stdint.h>

/// Adds one active hold that prevents inactivity-triggered sleep.
void inactivitySleepGuardAcquire();
/// Releases one inactivity-sleep hold.
void inactivitySleepGuardRelease();
/// Resets the inactivity timer after user activity or foreground work.
void inactivitySleepKeepAwake();
/// Returns true when sleep is blocked by holds or recent keep-awake activity.
bool inactivitySleepBlocked(uint32_t graceMs = 0);

/// RAII helper that blocks inactivity sleep for the lifetime of a scope.
class ScopedInactivitySleepGuard {
public:
  /// Acquires one inactivity-sleep hold.
  ScopedInactivitySleepGuard() { inactivitySleepGuardAcquire(); }
  /// Releases the hold acquired by the constructor.
  ~ScopedInactivitySleepGuard() { inactivitySleepGuardRelease(); }

  ScopedInactivitySleepGuard(const ScopedInactivitySleepGuard &) = delete;
  ScopedInactivitySleepGuard &
  operator=(const ScopedInactivitySleepGuard &) = delete;
};

#endif
