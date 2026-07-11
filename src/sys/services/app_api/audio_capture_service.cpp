#include "sys/services/app_api/audio_capture_service.h"

#include "sys/services/app_api/service_utils.h"
#include "sys/wav_encoder.h"

namespace app_api {

void AudioCaptureService::fail(const char *detail) {
  capture.release(completion.result);
  copyText(completion.error, sizeof(completion.error), detail);
  state.store(State::Failed, std::memory_order_release);
}

ServiceResult AudioCaptureService::request(Format format,
                                           uint32_t maxDurationMs,
                                           const uint8_t **data,
                                           size_t *size) {
  if (data == nullptr || size == nullptr) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "missing capture output");
  }
  *data = nullptr;
  *size = 0;
  if (maxDurationMs == 0 || maxDurationMs > kMaxDurationMs) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid capture duration");
  }

  State current = state.load(std::memory_order_acquire);
  if (current == State::Ready) {
    if (completion.format != format) {
      return ServiceResult::fail(ServiceError::Busy,
                                 "different capture format is ready");
    }
    *data = completion.result.data;
    *size = completion.result.length;
    state.store(State::Delivered, std::memory_order_release);
    return ServiceResult::end();
  }
  if (current == State::Requested || current == State::Capturing) {
    return command.format == format
               ? ServiceResult::pending()
               : ServiceResult::fail(ServiceError::Busy,
                                     "different capture format is active");
  }
  if (current == State::Failed) {
    ServiceResult result = ServiceResult::fail(ServiceError::Hardware,
                                               completion.error);
    state.store(State::Idle, std::memory_order_release);
    return result;
  }
  if (current == State::Delivered) {
    capture.release(completion.result);
    completion = {};
  }

  command.maxDurationMs = maxDurationMs;
  command.format = format;
  state.store(State::Requested, std::memory_order_release);
  return ServiceResult::pending();
}

void AudioCaptureService::updateOnMainCore() {
  State current = state.load(std::memory_order_acquire);
  if (current == State::Requested) {
    completion = {};
    completion.format = command.format;
    char error[64] = {};
    const bool started =
        command.format == Format::Wav
            ? capture.beginWav(completion.result, command.maxDurationMs, error,
                               sizeof(error))
            : capture.beginPcm16(completion.result, command.maxDurationMs,
                                 error, sizeof(error));
    if (!started) {
      fail(error[0] != '\0' ? error : "capture start failed");
      return;
    }
    targetBytes =
        static_cast<size_t>(16000U) * command.maxDurationMs / 1000U *
        sizeof(int16_t);
    if (command.format == Format::Wav) {
      targetBytes += wav::kPcmHeaderSize;
    }
    state.store(State::Capturing, std::memory_order_release);
    current = State::Capturing;
  }
  if (current != State::Capturing) {
    return;
  }

  char error[64] = {};
  if (!capture.pumpPcm16(completion.result, error, sizeof(error))) {
    fail(error[0] != '\0' ? error : "capture read failed");
    return;
  }
  if (completion.result.length >= targetBytes) {
    capture.finishPcm16(completion.result);
    state.store(State::Ready, std::memory_order_release);
  }
}

void AudioCaptureService::release() {
  capture.release(completion.result);
  command = {};
  completion = {};
  targetBytes = 0;
  state.store(State::Idle, std::memory_order_release);
}

} // namespace app_api
