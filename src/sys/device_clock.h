#ifndef DEVICE_CLOCK_H
#define DEVICE_CLOCK_H

#include <stdint.h>
#include <stddef.h>

class DeviceClock {
public:
  void set(int64_t unixTime, int32_t utcOffsetSeconds);
  bool isSet() const;
  bool snapshotLocalUnix(int64_t &localUnix) const;
  void restoreLocalUnix(int64_t localUnix);
  int64_t localMinuteIndex() const;
  void formatTime(char *out, size_t outSize) const;
  void formatDate(char *out, size_t outSize) const;

private:
  int64_t baseUnix = 0;
  int32_t utcOffset = 0;
  unsigned long baseMillis = 0;
  bool setFlag = false;

  int64_t nowLocalUnix() const;
};

extern DeviceClock deviceClock;

#endif
