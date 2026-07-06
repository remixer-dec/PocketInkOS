#ifndef LOCKSCREEN_GUARD_H
#define LOCKSCREEN_GUARD_H

#include <stdint.h>

/// Adds one active hold that prevents inactivity-triggered sleep.
void lockscreenGuardAcquire();
/// Releases one inactivity-sleep hold.
void lockscreenGuardRelease();
/// Resets the inactivity timer after user activity or foreground work.
void inactivitySleepKeepAwake();
/// Returns true when sleep is blocked by holds or recent keep-awake activity.
bool inactivitySleepBlocked(uint32_t graceMs = 0);

/// RAII helper that keeps the lockscreen awake for the lifetime of a scope.
class LockscreenGuard {
public:
  /// Acquires one lockscreen hold.
  LockscreenGuard() { lockscreenGuardAcquire(); }
  /// Releases the lockscreen hold acquired by the constructor.
  ~LockscreenGuard() { lockscreenGuardRelease(); }

  LockscreenGuard(const LockscreenGuard &) = delete;
  LockscreenGuard &operator=(const LockscreenGuard &) = delete;
};

#endif
