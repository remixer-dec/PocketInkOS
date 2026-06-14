#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H

#include <stdint.h>

void releasePowerHolds();
void keepPowerLatchOn();
void rebootDevice();
void powerOffDevice();
void enterDeepSleep(uint64_t timerWakeupUs = 0, bool keepEpdPowerOn = false);
bool deepSleepWokeFromTimer();

#endif
