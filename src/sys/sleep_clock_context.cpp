#include "sys/sleep_clock_context.h"

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif

#include <cstddef>
#include <cstring>

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR RTC_DATA_ATTR
#endif

#ifndef RTC_FAST_ATTR
#define RTC_FAST_ATTR RTC_NOINIT_ATTR
#endif

namespace {

static const uint16_t SLEEP_CLOCK_MAGIC = 0x5343U; // SC

struct StoredSleepClockContext {
  uint16_t magic;
  uint16_t checksum;
  uint8_t active;
  uint8_t clockSet;
  uint16_t wakeIntervalSeconds;
  int64_t localUnix;
};

RTC_FAST_ATTR StoredSleepClockContext sleepClockContext;

uint16_t fnv1a16(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return static_cast<uint16_t>((hash >> 16) ^ (hash & 0xffff));
}

uint16_t contextChecksum(const StoredSleepClockContext &context) {
  return fnv1a16(reinterpret_cast<const uint8_t *>(&context.active),
                 sizeof(context) - offsetof(StoredSleepClockContext, active));
}

void store(bool active, bool clockSet, int64_t localUnix,
           uint16_t wakeIntervalSeconds) {
  sleepClockContext.magic = SLEEP_CLOCK_MAGIC;
  sleepClockContext.active = active ? 1 : 0;
  sleepClockContext.clockSet = clockSet ? 1 : 0;
  sleepClockContext.wakeIntervalSeconds = wakeIntervalSeconds;
  sleepClockContext.localUnix = localUnix;
  sleepClockContext.checksum = contextChecksum(sleepClockContext);
}

} // namespace

void sleepClockContextStart(bool clockSet, int64_t localUnix,
                            uint16_t wakeIntervalSeconds) {
  store(true, clockSet, localUnix, wakeIntervalSeconds);
}

bool sleepClockContextLoad(SleepClockSnapshot &snapshot) {
  if (sleepClockContext.magic != SLEEP_CLOCK_MAGIC ||
      sleepClockContext.checksum != contextChecksum(sleepClockContext)) {
    return false;
  }

  snapshot.active = sleepClockContext.active != 0;
  snapshot.clockSet = sleepClockContext.clockSet != 0;
  snapshot.localUnix = sleepClockContext.localUnix;
  snapshot.wakeIntervalSeconds = sleepClockContext.wakeIntervalSeconds;
  return snapshot.active;
}

void sleepClockContextUpdate(bool clockSet, int64_t localUnix,
                             uint16_t wakeIntervalSeconds) {
  store(true, clockSet, localUnix, wakeIntervalSeconds);
}

void sleepClockContextClear() {
  memset(&sleepClockContext, 0, sizeof(sleepClockContext));
}
