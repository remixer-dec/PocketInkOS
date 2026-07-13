#include "sys/services/device_clock.h"
#include "sys/system_config.h"

#include <Arduino.h>
#include <stdio.h>
#include <sys/time.h>

namespace {

void setFilesystemClock(int64_t localUnix) {
  timeval value = {};
  value.tv_sec = static_cast<time_t>(localUnix);
  // FAT writes use the platform clock for file timestamps. DeviceClock keeps
  // local time, so mirror that value instead of leaving FAT at its 1980 epoch.
  settimeofday(&value, nullptr);
}

} // namespace

DeviceClock deviceClock;

void DeviceClock::set(int64_t unixTime, int32_t utcOffsetSeconds) {
  baseUnix = unixTime;
  utcOffset = utcOffsetSeconds;
  baseMillis = millis();
  setFlag = true;
  estimatedFlag = false;
  setFilesystemClock(baseUnix + utcOffset);
}

bool DeviceClock::isSet() const { return setFlag; }

bool DeviceClock::isEstimated() const { return setFlag && estimatedFlag; }

bool DeviceClock::snapshotLocalUnix(int64_t &localUnix) const {
  if (!setFlag) {
    return false;
  }
  localUnix = nowLocalUnix();
  return true;
}

bool DeviceClock::snapshotLocalUnixMillis(int64_t &localUnix,
                                          uint16_t &millisIntoSecond) const {
  if (!setFlag) {
    return false;
  }
  const unsigned long elapsed = millis() - baseMillis;
  localUnix = baseUnix + utcOffset + static_cast<int64_t>(elapsed / 1000);
  millisIntoSecond = static_cast<uint16_t>(elapsed % 1000);
  return true;
}

bool DeviceClock::snapshotUnixMillis(int64_t &unixTime,
                                     uint16_t &millisIntoSecond) const {
  if (!setFlag) {
    return false;
  }
  const unsigned long elapsed = millis() - baseMillis;
  unixTime = baseUnix + static_cast<int64_t>(elapsed / 1000);
  millisIntoSecond = static_cast<uint16_t>(elapsed % 1000);
  return true;
}

void DeviceClock::restoreLocalUnix(int64_t localUnix,
                                   int32_t utcOffsetSeconds,
                                   bool estimated) {
  set(localUnix - utcOffsetSeconds, utcOffsetSeconds);
  estimatedFlag = estimated;
}

int32_t DeviceClock::utcOffsetSeconds() const { return utcOffset; }

int64_t DeviceClock::localMinuteIndex() const {
  if (!setFlag) {
    return -1;
  }
  return nowLocalUnix() / 60;
}

void DeviceClock::formatTime(char *out, size_t outSize) const {
  formatTime(out, outSize, currentSystemConfig().time.timeFormat);
}

void DeviceClock::formatTime(char *out, size_t outSize,
                             ConfigTimeFormat format) const {
  if (outSize == 0) {
    return;
  }
  if (!setFlag) {
    snprintf(out, outSize, "--:--");
    return;
  }
  int64_t seconds = nowLocalUnix();
  int daySecond = static_cast<int>((seconds % 86400 + 86400) % 86400);
  int hour = daySecond / 3600;
  const int minute = (daySecond / 60) % 60;
  if (format == CONFIG_TIME_12H) {
    const bool pm = hour >= 12;
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
    snprintf(out, outSize, "%d:%02d %s", hour, minute, pm ? "PM" : "AM");
    return;
  }
  snprintf(out, outSize, "%02d:%02d", hour, minute);
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
  switch (currentSystemConfig().time.dateFormat) {
  case CONFIG_DATE_WEEKDAY: {
    static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int weekday = static_cast<int>((days + 4) % 7 + 7) % 7;
    snprintf(out, outSize, "%s, %s %u", WEEKDAYS[weekday], MONTHS[month - 1],
             day);
    break;
  }
  case CONFIG_DATE_MDY:
    snprintf(out, outSize, "%02u/%02u/%04d", month, day, year);
    break;
  case CONFIG_DATE_DMY:
    snprintf(out, outSize, "%02u/%02u/%04d", day, month, year);
    break;
  case CONFIG_DATE_DMY_DOT:
    snprintf(out, outSize, "%02u.%02u.%04d", day, month, year);
    break;
  case CONFIG_DATE_ISO:
  default:
    snprintf(out, outSize, "%04d-%02u-%02u", year, month, day);
    break;
  }
}

int64_t DeviceClock::nowLocalUnix() const {
  return baseUnix + utcOffset + static_cast<int64_t>((millis() - baseMillis) /
                                                     1000);
}
