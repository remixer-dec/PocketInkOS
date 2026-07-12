/// Cross-core recording service for external apps. App code submits a bounded
/// PCM or WAV request, the main core drives the microphone capture, and an atomic
/// mailbox publishes the session-owned result back without touching hardware.
#ifndef EXT_APP_API_AUDIO_CAPTURE_SERVICE_H
#define EXT_APP_API_AUDIO_CAPTURE_SERVICE_H

#include "sys/audio/audio_capture.h"
#include "sys/ext_app_api/service_result.h"

#include <atomic>

namespace ext_app_api {

/// Asynchronous mailbox between an app task and main-core microphone hardware.
class AudioCaptureService {
public:
  /// Output representation returned after microphone capture completes.
  enum class Format : uint8_t { Pcm16, Wav };

  /// Starts or polls one capture, returning owned bytes when it reaches End.
  ServiceResult request(Format format, uint32_t maxDurationMs,
                        const uint8_t **data, size_t *size);
  /// Advances requested hardware capture from the shell's main core.
  void updateOnMainCore();
  /// Cancels capture and frees any session-owned recording buffer.
  void release();

private:
  static constexpr uint32_t kMaxDurationMs = 60000;

  enum class State : uint8_t {
    Idle,
    Requested,
    Capturing,
    Ready,
    Delivered,
    Failed
  };

  struct CommandMailbox {
    uint32_t maxDurationMs = 0;
    Format format = Format::Pcm16;
  };

  struct CompletionMailbox {
    AudioCaptureResult result;
    Format format = Format::Pcm16;
    char error[64] = {};
  };

  AudioCapture capture;
  // Release/acquire state transitions publish the command to the main core
  // and the owned completion back to the app/runtime task.
  CommandMailbox command;
  CompletionMailbox completion;
  size_t targetBytes = 0;
  std::atomic<State> state{State::Idle};

  void fail(const char *detail);
};

} // namespace ext_app_api

#endif
