#include "sys/pcf85063_clock.h"

#if ENABLE_RTC_CLOCK

#include "sys/device_clock.h"
#include "sys/global.h"
#include "sys/touch_input.h"

#include <Arduino.h>
#include <driver/i2c_master.h>

Pcf85063Clock rtcClock;

namespace {

static const uint8_t RTC_ADDRESS = 0x51;
static i2c_master_dev_handle_t rtcDevice = NULL;

bool ensureRtcDevice() {
  if (rtcDevice != NULL) {
    return true;
  }

  touchI2cBegin();
  i2c_master_bus_handle_t bus = touchI2cBusHandle();
  if (bus == NULL) {
    return false;
  }

  i2c_device_config_t devConfig = {};
  devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devConfig.device_address = RTC_ADDRESS;
  devConfig.scl_speed_hz = 400000;

  return i2c_master_bus_add_device(bus, &devConfig, &rtcDevice) == ESP_OK;
}

} // namespace

void Pcf85063Clock::begin() {
  ensureRtcDevice();
}

bool Pcf85063Clock::readToDeviceClock() {
  if (!ensureRtcDevice()) {
    return false;
  }

  uint8_t reg = 0x04;
  uint8_t raw[7] = {};
  if (i2c_master_transmit_receive(rtcDevice, &reg, sizeof(reg), raw,
                                  sizeof(raw), 100) != ESP_OK) {
    return false;
  }

  uint8_t secondReg = raw[0];
  int second = fromBcd(secondReg & 0x7F);
  int minute = fromBcd(raw[1] & 0x7F);
  int hour = fromBcd(raw[2] & 0x3F);
  int day = fromBcd(raw[3] & 0x3F);
  int month = fromBcd(raw[5] & 0x1F);
  int year = 2000 + fromBcd(raw[6]);

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
  if (!ensureRtcDevice()) {
    return false;
  }

  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int weekday;
  unixToDateTime(unixTime + utcOffsetSeconds, year, month, day, hour, minute,
                 second, weekday);

  uint8_t raw[8] = {0x04,
                    toBcd(static_cast<uint8_t>(second)),
                    toBcd(static_cast<uint8_t>(minute)),
                    toBcd(static_cast<uint8_t>(hour)),
                    toBcd(static_cast<uint8_t>(day)),
                    toBcd(static_cast<uint8_t>(weekday)),
                    toBcd(static_cast<uint8_t>(month)),
                    toBcd(static_cast<uint8_t>(year % 100))};
  return i2c_master_transmit(rtcDevice, raw, sizeof(raw), 100) == ESP_OK;
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
