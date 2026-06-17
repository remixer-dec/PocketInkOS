#ifndef INACTIVITY_SLEEP_GUARD_H
#define INACTIVITY_SLEEP_GUARD_H

#include <stdint.h>

void inactivitySleepGuardAcquire();
void inactivitySleepGuardRelease();
void inactivitySleepKeepAwake();
bool inactivitySleepBlocked(uint32_t graceMs = 0);

class ScopedInactivitySleepGuard {
public:
  ScopedInactivitySleepGuard() { inactivitySleepGuardAcquire(); }
  ~ScopedInactivitySleepGuard() { inactivitySleepGuardRelease(); }

  ScopedInactivitySleepGuard(const ScopedInactivitySleepGuard &) = delete;
  ScopedInactivitySleepGuard &
  operator=(const ScopedInactivitySleepGuard &) = delete;
};

#endif
