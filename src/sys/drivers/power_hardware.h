#ifndef POWER_HARDWARE_H
#define POWER_HARDWARE_H

#include <stdint.h>

/// Releases GPIO and RTC holds left by a sleep transition.
void powerHardwareReleaseHolds();
/// Applies safe board GPIO levels immediately after wake.
void powerHardwarePrepareWakeDefaults();
/// Keeps the board power latch asserted.
void powerHardwareKeepLatchOn();
/// Restarts through the platform reset mechanism.
void powerHardwareReboot();
/// Drops power rails and waits for a power-button wake.
void powerHardwareOff();
/// Enters deep sleep with the configured wake timer and panel power state.
void powerHardwareEnterDeepSleep(uint64_t timerWakeupUs, bool keepEpdPowerOn);
/// Reports whether deep sleep ended because of the timer.
bool powerHardwareWokeFromTimer();

#endif
