#include "sys/device_clock.h"

#include <Arduino.h>
#include <stdio.h>

DeviceClock deviceClock;

void DeviceClock::set(int64_t unixTime, int32_t utcOffsetSeconds) {
  baseUnix = unixTime;
  utcOffset = utcOffsetSeconds;
  baseMillis = millis();
  setFlag = true;
}

bool DeviceClock::isSet() const { return setFlag; }

int64_t DeviceClock::localMinuteIndex() const {
  if (!setFlag) {
    return -1;
  }
  return nowLocalUnix() / 60;
}

void DeviceClock::formatTime(char *out, size_t outSize) const {
  if (outSize == 0) {
    return;
  }
  if (!setFlag) {
    snprintf(out, outSize, "--:--");
    return;
  }
  int64_t seconds = nowLocalUnix();
  int daySecond = static_cast<int>((seconds % 86400 + 86400) % 86400);
  snprintf(out, outSize, "%02d:%02d", daySecond / 3600,
           (daySecond / 60) % 60);
}

void DeviceClock::formatDate(char *out, size_t outSize) const {
  if (outSize == 0) {
    return;
  }
  if (!setFlag) {
    snprintf(out, outSize, "DATE ----");
    return;
  }

  int64_t days = nowLocalUnix() / 86400;
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned day = doy - (153 * mp + 2) / 5 + 1;
  unsigned month = mp + (mp < 10 ? 3 : -9);
  year += month <= 2;
  snprintf(out, outSize, "%04d-%02u-%02u", year, month, day);
}

int64_t DeviceClock::nowLocalUnix() const {
  return baseUnix + utcOffset + static_cast<int64_t>((millis() - baseMillis) /
                                                     1000);
}
