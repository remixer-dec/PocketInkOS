#ifndef WAKE_TRACE_H
#define WAKE_TRACE_H

#include <stdint.h>

enum class WakeTracePhase : uint8_t {
  None = 0,
  SleepSessionStarted,
  TimerBoot,
  ContextAccepted,
  ContextRejected,
  RtcRead,
  ClockAdvanced,
  RetainedClaim,
  BatteryReading,
  BatteryReady,
  BasicProfiledDispatch,
  SystemDispatch,
  NetworkDispatch,
  DisplayInitializing,
  DisplayReady,
  DisplayFlushing,
  DisplayFlushed,
  ContextStored,
  PreparingSleep,
  SleepArmed,
};

enum class WakeTraceResult : uint8_t {
  Unknown = 0,
  Failed,
  Passed,
};

struct WakeTraceSnapshot {
  uint32_t bootCount = 0;
  uint32_t timerWakeCount = 0;
  int16_t resetReason = 0;
  int16_t wakeupCause = 0;
  int16_t previousResetReason = 0;
  int16_t previousWakeupCause = 0;
  WakeTracePhase phase = WakeTracePhase::None;
  WakeTraceResult sleepContext = WakeTraceResult::Unknown;
  WakeTraceResult rtcRead = WakeTraceResult::Unknown;
  uint8_t dispatchStage = 0;
  uint8_t displayStage = 0;
  uint8_t sleepStage = 0;
  int64_t retainedLocalUnix = 0;
  int64_t rtcLocalUnix = 0;
  int64_t renderedLocalUnix = 0;
};

/// Records reset/wakeup causes before any storage-backed service starts.
void wakeTraceBeginBoot(int16_t resetReason, int16_t wakeupCause,
                        bool timerWake);
/// Starts a fresh lockscreen sleep session while retaining the boot counter.
void wakeTraceBeginSleepSession(int64_t retainedLocalUnix);
void wakeTraceSetContext(bool valid, int64_t retainedLocalUnix);
void wakeTraceSetRtcReadStarted();
void wakeTraceSetRtcRead(bool succeeded, int64_t rtcLocalUnix);
void wakeTraceSetClockAdvanced(int64_t localUnix);
void wakeTraceSetDispatch(WakeTracePhase phase, uint8_t stage);
void wakeTraceSetDisplay(WakeTracePhase phase, uint8_t stage,
                         int64_t renderedLocalUnix = 0);
void wakeTraceSetSleep(WakeTracePhase phase, uint8_t stage);
bool wakeTraceLoad(WakeTraceSnapshot &snapshot);
const char *wakeTracePhaseName(WakeTracePhase phase);
const char *wakeTraceResultName(WakeTraceResult result);

#endif
