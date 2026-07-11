#ifndef APP_API_AUDIO_CAPTURE_SERVICE_H
#define APP_API_AUDIO_CAPTURE_SERVICE_H

#include "sys/drivers/audio_capture.h"
#include "sys/services/app_api/service_result.h"

#include <atomic>

namespace app_api {

class AudioCaptureService {
public:
  enum class Format : uint8_t { Pcm16, Wav };

  ServiceResult request(Format format, uint32_t maxDurationMs,
                        const uint8_t **data, size_t *size);
  void updateOnMainCore();
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

} // namespace app_api

#endif
