#include "sys/ext_app_api/audio_capture_service.h"

#include "sys/ext_app_api/service_utils.h"
#include "sys/audio/wav_encoder.h"

namespace ext_app_api {

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

  State current = state.load(std::memory_order_acquire);
  if (maxDurationMs == 0) {
    if (current == State::Requested && command.format == format) {
      state.store(State::StopBeforeStart, std::memory_order_release);
      return ServiceResult::pending();
    }
    if (current == State::Capturing && command.format == format) {
      state.store(State::StopRequested, std::memory_order_release);
      return ServiceResult::pending();
    }
    if ((current == State::StopBeforeStart ||
         current == State::StopRequested) &&
        command.format == format) {
      return ServiceResult::pending();
    }
    if (current != State::Ready) {
      return ServiceResult::fail(ServiceError::InvalidArgument,
                                 "no active capture to stop");
    }
  } else if (maxDurationMs > kMaxDurationMs) {
    return ServiceResult::fail(ServiceError::InvalidArgument,
                               "invalid capture duration");
  }
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
  if (current == State::Requested || current == State::StopBeforeStart ||
      current == State::Capturing || current == State::StopRequested) {
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
  if (current == State::Requested || current == State::StopBeforeStart) {
    const bool stopAfterStart = current == State::StopBeforeStart;
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
    if (stopAfterStart) {
      capture.finishPcm16(completion.result);
      state.store(State::Ready, std::memory_order_release);
      return;
    }
    state.store(State::Capturing, std::memory_order_release);
    current = State::Capturing;
  }
  if (current == State::StopRequested) {
    char error[64] = {};
    if (!capture.pumpPcm16(completion.result, error, sizeof(error))) {
      fail(error[0] != '\0' ? error : "capture read failed");
      return;
    }
    capture.finishPcm16(completion.result);
    state.store(State::Ready, std::memory_order_release);
    return;
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

} // namespace ext_app_api
