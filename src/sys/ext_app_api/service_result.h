/// Internal result vocabulary shared by external-app services. It separates
/// asynchronous progress from completion and carries structured failure causes
/// before the public adapter reduces them to the stable app ABI status values.
#ifndef EXT_APP_API_SERVICE_RESULT_H
#define EXT_APP_API_SERVICE_RESULT_H

#include <stdint.h>

namespace ext_app_api {

/// Lifecycle outcome of an internal app service operation.
enum class ServiceState : uint8_t { Ok, Pending, End, Failed };

/// Structured failure reason retained before conversion to the public ABI.
enum class ServiceError : uint8_t {
  None,
  InvalidArgument,
  Denied,
  Unsupported,
  Busy,
  OutOfMemory,
  NotFound,
  Io,
  Hardware,
};

/// State, error category, and optional static detail for one service call.
struct ServiceResult {
  /// Current lifecycle outcome.
  ServiceState state = ServiceState::Ok;
  /// Failure category; meaningful only when state is Failed.
  ServiceError error = ServiceError::None;
  /// Optional non-owning diagnostic string for logs or adapters.
  const char *detail = nullptr;

  /// Constructs a successful result whose operation may be used immediately.
  static ServiceResult ok() { return {}; }
  /// Constructs an in-progress result that the caller should poll again.
  static ServiceResult pending() { return {ServiceState::Pending}; }
  /// Constructs normal end-of-stream or completed-playback state.
  static ServiceResult end() { return {ServiceState::End}; }
  /// Constructs a failed result with a stable category and diagnostic detail.
  static ServiceResult fail(ServiceError error, const char *detail) {
    return {ServiceState::Failed, error, detail};
  }
};

} // namespace ext_app_api

#endif
