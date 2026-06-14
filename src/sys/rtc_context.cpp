#include "sys/rtc_context.h"

#include <cstring>

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

#ifndef RTC_SLOW_ATTR
#define RTC_SLOW_ATTR RTC_DATA_ATTR
#endif

namespace {

static const uint16_t CONTEXT_MAGIC = 0x5049U; // PI

struct StoredContext {
  uint16_t magic = 0;
  uint16_t checksum = 0;
  RtcContextSnapshot snapshot = {};
};

RTC_SLOW_ATTR StoredContext retainedContext;

uint16_t fnv1a16(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return static_cast<uint16_t>((hash >> 16) ^ (hash & 0xffff));
}

uint16_t snapshotChecksum(const RtcContextSnapshot &snapshot) {
  return fnv1a16(reinterpret_cast<const uint8_t *>(&snapshot),
                 sizeof(snapshot));
}

} // namespace

RtcBitWriter::RtcBitWriter(uint8_t *buffer, size_t capacity)
    : buffer(buffer), capacity(capacity) {}

bool RtcBitWriter::writeBits(uint32_t value, uint8_t bitCount) {
  if (bitCount > 32 || bitPosition + bitCount > capacity * 8) {
    valid = false;
    return false;
  }
  for (uint8_t i = 0; i < bitCount; i++) {
    size_t byteIndex = bitPosition >> 3;
    uint8_t bitMask = static_cast<uint8_t>(1U << (bitPosition & 7));
    if ((value >> i) & 1U) {
      buffer[byteIndex] |= bitMask;
    } else {
      buffer[byteIndex] &= static_cast<uint8_t>(~bitMask);
    }
    bitPosition++;
  }
  return true;
}

bool RtcBitWriter::ok() const { return valid; }

size_t RtcBitWriter::bytesWritten() const { return (bitPosition + 7) >> 3; }

RtcBitReader::RtcBitReader(const uint8_t *buffer, size_t length)
    : buffer(buffer), length(length) {}

bool RtcBitReader::readBits(uint8_t bitCount, uint32_t &value) {
  value = 0;
  if (bitCount > 32 || bitPosition + bitCount > length * 8) {
    valid = false;
    return false;
  }
  for (uint8_t i = 0; i < bitCount; i++) {
    size_t byteIndex = bitPosition >> 3;
    uint8_t bitMask = static_cast<uint8_t>(1U << (bitPosition & 7));
    if (buffer[byteIndex] & bitMask) {
      value |= (1UL << i);
    }
    bitPosition++;
  }
  return true;
}

bool RtcBitReader::ok() const { return valid; }

bool rtcContextSave(const RtcContextSnapshot &snapshot) {
  if (snapshot.app.appDataLength > RTC_CONTEXT_APP_CAPACITY) {
    return false;
  }

  retainedContext.magic = CONTEXT_MAGIC;
  retainedContext.snapshot = snapshot;
  retainedContext.snapshot.navigation.appId[RTC_CONTEXT_APP_ID_SIZE - 1] =
      '\0';
  retainedContext.checksum = snapshotChecksum(retainedContext.snapshot);
  return true;
}

bool rtcContextLoad(RtcContextSnapshot &snapshot) {
  if (retainedContext.magic != CONTEXT_MAGIC ||
      retainedContext.snapshot.app.appDataLength > RTC_CONTEXT_APP_CAPACITY ||
      retainedContext.checksum != snapshotChecksum(retainedContext.snapshot)) {
    return false;
  }

  snapshot = retainedContext.snapshot;
  snapshot.navigation.appId[RTC_CONTEXT_APP_ID_SIZE - 1] = '\0';
  return true;
}

void rtcContextClear() { retainedContext = StoredContext{}; }
