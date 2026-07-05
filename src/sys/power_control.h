#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <stdint.h>

/// Releases transient holds before entering a low-power or shutdown path.
void releasePowerHolds();
/// Keeps the hardware power latch asserted while firmware is running.
void keepPowerLatchOn();
/// Restores GPIO defaults expected immediately after wake.
void prepareWakePowerDefaults();
/// Disables wireless peripherals before sleep or shutdown.
void disableWirelessForSleep();
/// Reboots the device through the platform reset path.
void rebootDevice();
/// Powers the device down after preparing peripherals for shutdown.
void powerOffDevice();
/// Enters deep sleep, optionally keeping e-paper powered for display retention.
void enterDeepSleep(uint64_t timerWakeupUs = 0, bool keepEpdPowerOn = false);
/// Reports whether the current boot was caused by a deep-sleep timer wake.
bool deepSleepWokeFromTimer();

#endif
