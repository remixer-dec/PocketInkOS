#ifndef APP_API_SERVICE_RESULT_H
#define APP_API_SERVICE_RESULT_H

#include <stdint.h>

namespace app_api {

enum class ServiceState : uint8_t { Ok, Pending, End, Failed };

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

struct ServiceResult {
  ServiceState state = ServiceState::Ok;
  ServiceError error = ServiceError::None;
  const char *detail = nullptr;

  static ServiceResult ok() { return {}; }
  static ServiceResult pending() { return {ServiceState::Pending}; }
  static ServiceResult end() { return {ServiceState::End}; }
  static ServiceResult fail(ServiceError error, const char *detail) {
    return {ServiceState::Failed, error, detail};
  }
};

} // namespace app_api

#endif
