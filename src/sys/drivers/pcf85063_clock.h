/// Driver for the PCF85063 battery-backed real-time clock on the shared I2C bus.
/// The chip stores local date and time fields, not a Unix timestamp.
#ifndef PCF85063_CLOCK_H
#define PCF85063_CLOCK_H

#include <stdint.h>

#ifndef ENABLE_RTC_CLOCK
#define ENABLE_RTC_CLOCK 1
#endif

#if ENABLE_RTC_CLOCK

/// Minimal access for reading and writing the battery-backed PCF85063.
class Pcf85063Clock {
public:
  /// Attaches the RTC to the shared I2C bus when it is available.
  void begin();
  /// Reads the date and time fields and converts them to a local seconds count.
  bool readLocalUnix(int64_t &localUnix);
  /// Converts a Unix timestamp to local date and time fields and writes them.
  bool writeFromUnix(int64_t unixTime, int32_t utcOffsetSeconds);

private:
  static uint8_t toBcd(uint8_t value);
  static uint8_t fromBcd(uint8_t value);
  static void unixToDateTime(int64_t localUnix, int &year, int &month,
                             int &day, int &hour, int &minute, int &second,
                             int &weekday);
  static int64_t daysFromCivil(int year, unsigned month, unsigned day);
};

/// Process-wide PCF85063 device read at boot and updated after network sync.
extern Pcf85063Clock rtcClock;

#endif

#endif
