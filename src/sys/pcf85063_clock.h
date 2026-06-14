#ifndef PCF85063_CLOCK_H
#define PCF85063_CLOCK_H

#include <stdint.h>

#ifndef ENABLE_RTC_CLOCK
#define ENABLE_RTC_CLOCK 1
#endif

#if ENABLE_RTC_CLOCK

class Pcf85063Clock {
public:
  void begin();
  bool readToDeviceClock();
  bool writeFromUnix(int64_t unixTime, int32_t utcOffsetSeconds);

private:
  static uint8_t toBcd(uint8_t value);
  static uint8_t fromBcd(uint8_t value);
  static void unixToDateTime(int64_t localUnix, int &year, int &month,
                             int &day, int &hour, int &minute, int &second,
                             int &weekday);
  static int64_t daysFromCivil(int year, unsigned month, unsigned day);
};

extern Pcf85063Clock rtcClock;

#endif

#endif
