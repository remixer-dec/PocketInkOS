#ifndef RTC_CONTEXT_H
#define RTC_CONTEXT_H

#include "sys/app_runtime.h"
#include <stddef.h>
#include <stdint.h>

static const size_t RTC_CONTEXT_APP_CAPACITY = 4096;
static const size_t RTC_CONTEXT_APP_ID_SIZE = 24;

struct RtcNavigationContext {
  Screen screen;
  MenuCategory menuCategory;
  uint8_t menuPage;
  bool hasActiveApp;
  char appId[RTC_CONTEXT_APP_ID_SIZE];
};

struct RtcSystemContext {
  bool clockSet;
  int64_t clockLocalUnix;
  bool wifiOn;
};

struct RtcAppContext {
  uint16_t appDataLength;
  uint8_t appData[RTC_CONTEXT_APP_CAPACITY];
};

struct RtcContextSnapshot {
  RtcNavigationContext navigation;
  RtcSystemContext system;
  RtcAppContext app;
};

class RtcBitWriter {
public:
  RtcBitWriter(uint8_t *buffer, size_t capacity);

  bool writeBits(uint32_t value, uint8_t bitCount);
  bool ok() const;
  size_t bytesWritten() const;

private:
  uint8_t *buffer;
  size_t capacity;
  size_t bitPosition = 0;
  bool valid = true;
};

class RtcBitReader {
public:
  RtcBitReader(const uint8_t *buffer, size_t length);

  bool readBits(uint8_t bitCount, uint32_t &value);
  bool ok() const;

private:
  const uint8_t *buffer;
  size_t length;
  size_t bitPosition = 0;
  bool valid = true;
};

bool rtcContextSave(const RtcContextSnapshot &snapshot);
bool rtcContextLoad(RtcContextSnapshot &snapshot);
void rtcContextClear();

#endif
