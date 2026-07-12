#ifndef DEVICE_CLOCK_H
#define DEVICE_CLOCK_H

#include <stdint.h>
#include <stddef.h>

#include "sys/system_config.h"

/// Monotonic-millis backed local clock used after network or RTC time sync.
class DeviceClock {
public:
  /// Sets the clock from Unix time and UTC offset.
  void set(int64_t unixTime, int32_t utcOffsetSeconds);
  /// Returns true after the clock has been set from a trusted source.
  bool isSet() const;
  /// Captures current local Unix time in seconds.
  bool snapshotLocalUnix(int64_t &localUnix) const;
  /// Captures current local Unix time and milliseconds into the current second.
  bool snapshotLocalUnixMillis(int64_t &localUnix,
                               uint16_t &millisIntoSecond) const;
  /// Captures current UTC Unix time and milliseconds for standards-based APIs
  /// such as JavaScript Date.now().
  bool snapshotUnixMillis(int64_t &unixTime,
                          uint16_t &millisIntoSecond) const;
  /// Restores local Unix time and its UTC offset after deep sleep.
  void restoreLocalUnix(int64_t localUnix, int32_t utcOffsetSeconds);
  /// Returns the UTC offset currently applied to local snapshots.
  int32_t utcOffsetSeconds() const;
  /// Returns the current local minute index for minute-granularity UI refresh.
  int64_t localMinuteIndex() const;
  /// Formats local time according to system config.
  void formatTime(char *out, size_t outSize) const;
  /// Formats local time using an explicit display format. Sleep-clock wakes use
  /// this with the RTC-retained format before SD-backed configuration is ready.
  void formatTime(char *out, size_t outSize, ConfigTimeFormat format) const;
  /// Formats local date according to system config.
  void formatDate(char *out, size_t outSize) const;

private:
  int64_t baseUnix = 0;
  int32_t utcOffset = 0;
  unsigned long baseMillis = 0;
  bool setFlag = false;

  int64_t nowLocalUnix() const;
};

/// Process-wide device clock instance.
extern DeviceClock deviceClock;

#endif
