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

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned monthOfYear = month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear = (153 * monthOfYear + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

void civilFromDays(int64_t days, int &year, unsigned &month, unsigned &day) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
       dayOfEra / 146096) /
      365;
  year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const unsigned dayOfYear =
      dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPart = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPart + 2) / 5 + 1;
  month = monthPart + (monthPart < 10 ? 3 : -9);
  year += month <= 2;
}

bool parseIsoDate(const char *isoDate, int &year, unsigned &month,
                  unsigned &day) {
  char trailing = '\0';
  return isoDate != nullptr &&
         sscanf(isoDate, "%4d-%2u-%2u%c", &year, &month, &day, &trailing) ==
             3 &&
         year >= 1 && month >= 1 && month <= 12 && day >= 1 &&
         day <= static_cast<unsigned>(
                    (month == 2
                         ? ((year % 4 == 0 &&
                             (year % 100 != 0 || year % 400 == 0))
                                ? 29
                                : 28)
                         : (month == 4 || month == 6 || month == 9 ||
                                    month == 11
                                ? 30
                                : 31)));
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
  formatDate(out, outSize, currentSystemConfig().time.dateFormat);
}

void DeviceClock::formatDate(char *out, size_t outSize,
                             ConfigDateFormat format) const {
  if (outSize == 0) {
    return;
  }
  if (!setFlag) {
    snprintf(out, outSize, "DATE ----");
    return;
  }

  const int64_t days = nowLocalUnix() / 86400;
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(days, year, month, day);
  char isoDate[11] = "";
  snprintf(isoDate, sizeof(isoDate), "%04d-%02u-%02u", year, month, day);
  if (!formatDateString(isoDate, out, outSize, format, true)) {
    snprintf(out, outSize, "DATE ----");
  }
}

bool DeviceClock::formatDateString(const char *isoDate, char *out,
                                    size_t outSize, bool includeYear) {
  return formatDateString(isoDate, out, outSize,
                          currentSystemConfig().time.dateFormat, includeYear);
}

bool DeviceClock::formatDateString(const char *isoDate, char *out,
                                   size_t outSize, ConfigDateFormat format,
                                   bool includeYear) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!parseIsoDate(isoDate, year, month, day)) {
    return false;
  }

  switch (format) {
  case CONFIG_DATE_WEEKDAY: {
    static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int weekday = static_cast<int>(
        (daysFromCivil(year, month, day) + 4) % 7 + 7) %
                       7;
    snprintf(out, outSize, "%s, %s %u", WEEKDAYS[weekday],
             MONTHS[month - 1], day);
    break;
  }
  case CONFIG_DATE_MDY:
    snprintf(out, outSize, includeYear ? "%02u/%02u/%04d" : "%02u/%02u",
             month, day, year);
    break;
  case CONFIG_DATE_DMY:
    snprintf(out, outSize, includeYear ? "%02u/%02u/%04d" : "%02u/%02u",
             day, month, year);
    break;
  case CONFIG_DATE_DMY_DOT:
    snprintf(out, outSize, includeYear ? "%02u.%02u.%04d" : "%02u.%02u",
             day, month, year);
    break;
  case CONFIG_DATE_ISO:
  default:
    snprintf(out, outSize, includeYear ? "%04d-%02u-%02u" : "%02u-%02u",
             year, month, day);
    break;
  }
  return out[0] != '\0';
}

int64_t DeviceClock::nowLocalUnix() const {
  return baseUnix + utcOffset + static_cast<int64_t>((millis() - baseMillis) /
                                                     1000);
}
