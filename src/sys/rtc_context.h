#ifndef RTC_CONTEXT_H
#define RTC_CONTEXT_H

#include "sys/app_runtime.h"
#include <stddef.h>
#include <stdint.h>

/// Maximum bytes reserved in RTC memory for app-owned suspend state.
static const size_t RTC_CONTEXT_APP_CAPACITY = 4096;
/// Maximum stored app id length in RTC navigation context, including terminator.
static const size_t RTC_CONTEXT_APP_ID_SIZE = 24;

/// Navigation state preserved across deep sleep.
struct RtcNavigationContext {
  /// Shell screen active before sleep.
  Screen screen;
  /// Menu category active before sleep.
  MenuCategory menuCategory;
  /// Launcher page within the active menu category.
  uint8_t menuPage;
  /// True when an app runtime should be restored instead of shell UI only.
  bool hasActiveApp;
  /// Stable id of the app that owned the active session.
  char appId[RTC_CONTEXT_APP_ID_SIZE];
};

/// System-level state preserved across deep sleep.
struct RtcSystemContext {
  /// Whether the device clock had a trusted value before sleep.
  bool clockSet;
  /// Local Unix timestamp captured before sleep.
  int64_t clockLocalUnix;
  /// UTC offset paired with clockLocalUnix.
  int32_t clockUtcOffsetSeconds;
  /// Whether Wi-Fi was enabled before sleep.
  bool wifiOn;
  /// Runtime output volume before sleep.
  uint8_t volume;
  /// Runtime mute state before sleep.
  bool muted;
};

/// App-owned state blob preserved across deep sleep.
struct RtcAppContext {
  /// Number of valid bytes in appData.
  uint16_t appDataLength;
  /// Serialized app state written by the active app.
  uint8_t appData[RTC_CONTEXT_APP_CAPACITY];
};

/// Full RTC context payload saved before deep sleep.
struct RtcContextSnapshot {
  /// Restorable shell and app navigation state.
  RtcNavigationContext navigation;
  /// Restorable system service state.
  RtcSystemContext system;
  /// Restorable app-owned state.
  RtcAppContext app;
};

/// Bit-packed writer used to keep RTC snapshots small and versionable.
class RtcBitWriter {
public:
  /// Creates a writer over a caller-owned byte buffer.
  RtcBitWriter(uint8_t *buffer, size_t capacity);

  /// Appends the low bitCount bits of value to the stream.
  bool writeBits(uint32_t value, uint8_t bitCount);
  /// Returns false after any capacity or argument error.
  bool ok() const;
  /// Returns the number of bytes touched by written bits.
  size_t bytesWritten() const;

private:
  uint8_t *buffer;
  size_t capacity;
  size_t bitPosition = 0;
  bool valid = true;
};

/// Bit-packed reader matching RtcBitWriter encoding.
class RtcBitReader {
public:
  /// Creates a reader over a serialized RTC byte buffer.
  RtcBitReader(const uint8_t *buffer, size_t length);

  /// Reads bitCount bits into value.
  bool readBits(uint8_t bitCount, uint32_t &value);
  /// Returns false after any bounds or argument error.
  bool ok() const;

private:
  const uint8_t *buffer;
  size_t length;
  size_t bitPosition = 0;
  bool valid = true;
};

/// Saves a complete RTC snapshot for restoration after deep sleep.
bool rtcContextSave(const RtcContextSnapshot &snapshot);
/// Loads a previously saved RTC snapshot.
bool rtcContextLoad(RtcContextSnapshot &snapshot);
/// Clears any saved RTC snapshot.
void rtcContextClear();

#endif
