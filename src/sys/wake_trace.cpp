#include "sys/wake_trace.h"

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

constexpr uint32_t kWakeTraceMagic = 0x57545232UL; // WTR2

struct StoredWakeTrace {
  uint32_t magic;
  uint16_t checksum;
  uint16_t reserved;
  uint32_t bootCount;
  uint32_t timerWakeCount;
  int16_t resetReason;
  int16_t wakeupCause;
  int16_t previousResetReason;
  int16_t previousWakeupCause;
  uint8_t phase;
  uint8_t sleepContext;
  uint8_t rtcRead;
  uint8_t dispatchStage;
  uint8_t displayStage;
  uint8_t sleepStage;
  uint8_t reservedBytes[2];
  int64_t retainedLocalUnix;
  int64_t rtcLocalUnix;
  int64_t renderedLocalUnix;
};

static_assert(sizeof(StoredWakeTrace) <= 64,
              "RTC wake trace must remain compact");

RTC_FAST_ATTR StoredWakeTrace retainedWakeTrace;

uint16_t checksum(const StoredWakeTrace &trace) {
  const uint8_t *data =
      reinterpret_cast<const uint8_t *>(&trace.bootCount);
  const size_t length =
      sizeof(trace) - offsetof(StoredWakeTrace, bootCount);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return static_cast<uint16_t>((hash >> 16) ^ (hash & 0xffffU));
}

bool valid() {
  return retainedWakeTrace.magic == kWakeTraceMagic &&
         retainedWakeTrace.checksum == checksum(retainedWakeTrace);
}

void ensureValid() {
  if (valid()) {
    return;
  }
  memset(&retainedWakeTrace, 0, sizeof(retainedWakeTrace));
  retainedWakeTrace.magic = kWakeTraceMagic;
  retainedWakeTrace.checksum = checksum(retainedWakeTrace);
}

void commit() { retainedWakeTrace.checksum = checksum(retainedWakeTrace); }

void setPhase(WakeTracePhase phase) {
  ensureValid();
  retainedWakeTrace.phase = static_cast<uint8_t>(phase);
}

} // namespace

void wakeTraceBeginBoot(int16_t resetReason, int16_t wakeupCause,
                        bool timerWake) {
  ensureValid();
  retainedWakeTrace.previousResetReason = retainedWakeTrace.resetReason;
  retainedWakeTrace.previousWakeupCause = retainedWakeTrace.wakeupCause;
  retainedWakeTrace.resetReason = resetReason;
  retainedWakeTrace.wakeupCause = wakeupCause;
  if (retainedWakeTrace.bootCount < UINT32_MAX) {
    retainedWakeTrace.bootCount++;
  }
  if (timerWake) {
    if (retainedWakeTrace.timerWakeCount < UINT32_MAX) {
      retainedWakeTrace.timerWakeCount++;
    }
    retainedWakeTrace.phase = static_cast<uint8_t>(WakeTracePhase::TimerBoot);
    retainedWakeTrace.sleepContext =
        static_cast<uint8_t>(WakeTraceResult::Unknown);
    retainedWakeTrace.rtcRead =
        static_cast<uint8_t>(WakeTraceResult::Unknown);
    retainedWakeTrace.dispatchStage = 0;
    retainedWakeTrace.displayStage = 0;
    retainedWakeTrace.sleepStage = 0;
    retainedWakeTrace.rtcLocalUnix = 0;
    retainedWakeTrace.renderedLocalUnix = 0;
  }
  commit();
}

void wakeTraceBeginSleepSession(int64_t retainedLocalUnix) {
  ensureValid();
  retainedWakeTrace.timerWakeCount = 0;
  retainedWakeTrace.phase =
      static_cast<uint8_t>(WakeTracePhase::SleepSessionStarted);
  retainedWakeTrace.sleepContext =
      static_cast<uint8_t>(WakeTraceResult::Unknown);
  retainedWakeTrace.rtcRead =
      static_cast<uint8_t>(WakeTraceResult::Unknown);
  retainedWakeTrace.dispatchStage = 0;
  retainedWakeTrace.displayStage = 0;
  retainedWakeTrace.sleepStage = 0;
  retainedWakeTrace.retainedLocalUnix = retainedLocalUnix;
  retainedWakeTrace.rtcLocalUnix = 0;
  retainedWakeTrace.renderedLocalUnix = retainedLocalUnix;
  commit();
}

void wakeTraceSetContext(bool contextValid, int64_t retainedLocalUnix) {
  setPhase(contextValid ? WakeTracePhase::ContextAccepted
                        : WakeTracePhase::ContextRejected);
  retainedWakeTrace.sleepContext = static_cast<uint8_t>(
      contextValid ? WakeTraceResult::Passed : WakeTraceResult::Failed);
  retainedWakeTrace.retainedLocalUnix = retainedLocalUnix;
  commit();
}

void wakeTraceSetRtcReadStarted() {
  setPhase(WakeTracePhase::RtcRead);
  if (retainedWakeTrace.rtcRead !=
      static_cast<uint8_t>(WakeTraceResult::Passed)) {
    retainedWakeTrace.rtcRead =
        static_cast<uint8_t>(WakeTraceResult::Unknown);
    retainedWakeTrace.rtcLocalUnix = 0;
  }
  commit();
}

void wakeTraceSetRtcRead(bool succeeded, int64_t rtcLocalUnix) {
  setPhase(WakeTracePhase::RtcRead);
  if (succeeded) {
    retainedWakeTrace.rtcRead =
        static_cast<uint8_t>(WakeTraceResult::Passed);
    retainedWakeTrace.rtcLocalUnix = rtcLocalUnix;
  } else if (retainedWakeTrace.rtcRead !=
             static_cast<uint8_t>(WakeTraceResult::Passed)) {
    retainedWakeTrace.rtcRead =
        static_cast<uint8_t>(WakeTraceResult::Failed);
    retainedWakeTrace.rtcLocalUnix = 0;
  }
  commit();
}

void wakeTraceSetClockAdvanced(int64_t localUnix) {
  setPhase(WakeTracePhase::ClockAdvanced);
  retainedWakeTrace.renderedLocalUnix = localUnix;
  commit();
}

void wakeTraceSetDispatch(WakeTracePhase phase, uint8_t stage) {
  setPhase(phase);
  retainedWakeTrace.dispatchStage = stage;
  commit();
}

void wakeTraceSetDisplay(WakeTracePhase phase, uint8_t stage,
                         int64_t renderedLocalUnix) {
  setPhase(phase);
  retainedWakeTrace.displayStage = stage;
  if (renderedLocalUnix != 0) {
    retainedWakeTrace.renderedLocalUnix = renderedLocalUnix;
  }
  commit();
}

void wakeTraceSetSleep(WakeTracePhase phase, uint8_t stage) {
  setPhase(phase);
  retainedWakeTrace.sleepStage = stage;
  commit();
}

bool wakeTraceLoad(WakeTraceSnapshot &snapshot) {
  if (!valid()) {
    return false;
  }
  snapshot.bootCount = retainedWakeTrace.bootCount;
  snapshot.timerWakeCount = retainedWakeTrace.timerWakeCount;
  snapshot.resetReason = retainedWakeTrace.resetReason;
  snapshot.wakeupCause = retainedWakeTrace.wakeupCause;
  snapshot.previousResetReason = retainedWakeTrace.previousResetReason;
  snapshot.previousWakeupCause = retainedWakeTrace.previousWakeupCause;
  snapshot.phase = static_cast<WakeTracePhase>(retainedWakeTrace.phase);
  snapshot.sleepContext =
      static_cast<WakeTraceResult>(retainedWakeTrace.sleepContext);
  snapshot.rtcRead = static_cast<WakeTraceResult>(retainedWakeTrace.rtcRead);
  snapshot.dispatchStage = retainedWakeTrace.dispatchStage;
  snapshot.displayStage = retainedWakeTrace.displayStage;
  snapshot.sleepStage = retainedWakeTrace.sleepStage;
  snapshot.retainedLocalUnix = retainedWakeTrace.retainedLocalUnix;
  snapshot.rtcLocalUnix = retainedWakeTrace.rtcLocalUnix;
  snapshot.renderedLocalUnix = retainedWakeTrace.renderedLocalUnix;
  return true;
}

const char *wakeTracePhaseName(WakeTracePhase phase) {
  switch (phase) {
  case WakeTracePhase::None:
    return "none";
  case WakeTracePhase::SleepSessionStarted:
    return "session-started";
  case WakeTracePhase::TimerBoot:
    return "timer-boot";
  case WakeTracePhase::ContextAccepted:
    return "context-accepted";
  case WakeTracePhase::ContextRejected:
    return "context-rejected";
  case WakeTracePhase::RtcRead:
    return "rtc-read";
  case WakeTracePhase::ClockAdvanced:
    return "clock-advanced";
  case WakeTracePhase::RetainedClaim:
    return "retained-claim";
  case WakeTracePhase::BatteryReading:
    return "battery-reading";
  case WakeTracePhase::BatteryReady:
    return "battery-ready";
  case WakeTracePhase::BasicProfiledDispatch:
    return "basic-profiled-dispatch";
  case WakeTracePhase::SystemDispatch:
    return "system-dispatch";
  case WakeTracePhase::NetworkDispatch:
    return "network-dispatch";
  case WakeTracePhase::DisplayInitializing:
    return "display-initializing";
  case WakeTracePhase::DisplayReady:
    return "display-ready";
  case WakeTracePhase::DisplayFlushing:
    return "display-flushing";
  case WakeTracePhase::DisplayFlushed:
    return "display-flushed";
  case WakeTracePhase::ContextStored:
    return "context-stored";
  case WakeTracePhase::PreparingSleep:
    return "preparing-sleep";
  case WakeTracePhase::SleepArmed:
    return "sleep-armed";
  }
  return "unknown";
}

const char *wakeTraceResultName(WakeTraceResult result) {
  switch (result) {
  case WakeTraceResult::Unknown:
    return "unknown";
  case WakeTraceResult::Failed:
    return "failed";
  case WakeTraceResult::Passed:
    return "passed";
  }
  return "invalid";
}
