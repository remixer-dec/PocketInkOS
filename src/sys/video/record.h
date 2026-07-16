/// Timed screen recording service backed by the currently selected video codec.
#ifndef SYS_VIDEO_RECORD_H
#define SYS_VIDEO_RECORD_H

#include <stdint.h>

class AppDisplay;

enum class VideoRecordResult : uint8_t {
  None,
  Started,
  Saved,
  NoSdCard,
  NoFramebuffer,
  NoMemory,
  OpenFailed,
  WriteFailed,
};

/// Starts recording when idle, or finalizes the active recording.
/// The caller must hold the display lock so the initial frame is stable.
VideoRecordResult videoRecordToggle(const AppDisplay &display);

/// Captures a due frame and automatically finalizes at the five-minute limit.
/// The caller must hold the display lock while invoking this function.
VideoRecordResult videoRecordUpdate(const AppDisplay &display);

/// Finalizes an active recording. Returns None when already idle.
VideoRecordResult videoRecordStop();

bool videoRecording();
const char *videoRecordResultMessage(VideoRecordResult result);

#endif
