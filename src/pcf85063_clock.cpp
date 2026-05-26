#ifndef ENABLE_RTC_CLOCK
#define ENABLE_RTC_CLOCK 0
#endif

#if ENABLE_RTC_CLOCK

#include "pcf85063_clock.h"
#include "device_clock.h"
#include "global.h"

#include <Arduino.h>
#include <Wire.h>

Pcf85063Clock rtcClock;

void Pcf85063Clock::begin() {
  Wire.begin(ESP32_I2C_SDA, ESP32_I2C_SCL);
  Wire.setTimeOut(100);
}

bool Pcf85063Clock::readToDeviceClock() {
  Wire.beginTransmission(ADDRESS);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(ADDRESS, 7) != 7) {
    return false;
  }

  uint8_t secondReg = Wire.read();
  int second = fromBcd(secondReg & 0x7F);
  int minute = fromBcd(Wire.read() & 0x7F);
  int hour = fromBcd(Wire.read() & 0x3F);
  int day = fromBcd(Wire.read() & 0x3F);
  Wire.read();
  int month = fromBcd(Wire.read() & 0x1F);
  int year = 2000 + fromBcd(Wire.read());

  if ((secondReg & 0x80) || year < 2024 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  int64_t localUnix = daysFromCivil(year, month, day) * 86400 + hour * 3600 +
                      minute * 60 + second;
  deviceClock.set(localUnix, 0);
  return true;
}

bool Pcf85063Clock::writeFromUnix(int64_t unixTime, int32_t utcOffsetSeconds) {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int weekday;
  unixToDateTime(unixTime + utcOffsetSeconds, year, month, day, hour, minute,
                 second, weekday);

  Wire.beginTransmission(ADDRESS);
  Wire.write(0x04);
  Wire.write(toBcd(second));
  Wire.write(toBcd(minute));
  Wire.write(toBcd(hour));
  Wire.write(toBcd(day));
  Wire.write(toBcd(weekday));
  Wire.write(toBcd(month));
  Wire.write(toBcd(year % 100));
  return Wire.endTransmission() == 0;
}

uint8_t Pcf85063Clock::toBcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

uint8_t Pcf85063Clock::fromBcd(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

void Pcf85063Clock::unixToDateTime(int64_t localUnix, int &year, int &month,
                                   int &day, int &hour, int &minute,
                                   int &second, int &weekday) {
  int64_t days = localUnix / 86400;
  int daySecond = static_cast<int>((localUnix % 86400 + 86400) % 86400);
  hour = daySecond / 3600;
  minute = (daySecond / 60) % 60;
  second = daySecond % 60;
  weekday = static_cast<int>((days + 4) % 7);
  if (weekday < 0) {
    weekday += 7;
  }

  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  month = static_cast<int>(mp + (mp < 10 ? 3 : -9));
  year += month <= 2;
}

int64_t Pcf85063Clock::daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

#endif
