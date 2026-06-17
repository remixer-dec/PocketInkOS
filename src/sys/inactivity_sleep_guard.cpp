#include "sys/inactivity_sleep_guard.h"

#include <Arduino.h>

namespace {

uint8_t sleepGuardDepth = 0;
unsigned long lastKeepAwakeMs = 0;

} // namespace

void inactivitySleepGuardAcquire() {
  if (sleepGuardDepth < 255U) {
    sleepGuardDepth++;
  }
  inactivitySleepKeepAwake();
}

void inactivitySleepGuardRelease() {
  if (sleepGuardDepth > 0U) {
    sleepGuardDepth--;
  }
  inactivitySleepKeepAwake();
}

void inactivitySleepKeepAwake() { lastKeepAwakeMs = millis(); }

bool inactivitySleepBlocked(uint32_t graceMs) {
  if (sleepGuardDepth > 0U) {
    return true;
  }
  return graceMs > 0U && lastKeepAwakeMs != 0UL &&
         static_cast<uint32_t>(millis() - lastKeepAwakeMs) < graceMs;
}
