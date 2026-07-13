#include "sys/services/power_control.h"

#include "sys/drivers/power_hardware.h"

#if __has_include(<WiFi.h>)
#include <WiFi.h>
#define POCKET_INK_HAS_WIFI 1
#else
#define POCKET_INK_HAS_WIFI 0
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#endif

void releasePowerHolds() { powerHardwareReleaseHolds(); }

void prepareWakePowerDefaults() { powerHardwarePrepareWakeDefaults(); }

void keepPowerLatchOn() { powerHardwareKeepLatchOn(); }

void disableWirelessForSleep() {
#if POCKET_INK_HAS_WIFI
  WiFi.scanDelete();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
#endif
#if defined(ARDUINO_ARCH_ESP32)
  btStop();
#endif
}

void rebootDevice() { powerHardwareReboot(); }

void powerOffDevice() {
  disableWirelessForSleep();
  powerHardwareOff();
}

void emergencyPowerOffDevice() { powerHardwareOff(); }

void enterDeepSleep(uint64_t timerWakeupUs, bool keepEpdPowerOn) {
  disableWirelessForSleep();
  powerHardwareEnterDeepSleep(timerWakeupUs, keepEpdPowerOn);
}

bool deepSleepWokeFromTimer() { return powerHardwareWokeFromTimer(); }

int16_t deviceResetReason() { return powerHardwareResetReason(); }

int16_t deviceWakeupCause() { return powerHardwareWakeupCause(); }
