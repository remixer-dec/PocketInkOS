#ifndef APP_API_PERMISSION_SERVICE_H
#define APP_API_PERMISSION_SERVICE_H

#include "sys/drivers/touch_input.h"
#include "sys/services/app_api/service_result.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

namespace app_api {

enum class PermissionKind : uint8_t { FileWrite, FileDelete, Gpio, WifiScan };
enum class PermissionDecision : int8_t { Denied = -1, Pending = 0, Granted = 1 };

class PermissionService {
public:
  ServiceResult request(PermissionKind kind, const char *resource);
  bool promptActive() const;
  void drawPrompt(Adafruit_GFX &gfx, const char *appId) const;
  bool handlePromptTouch(const TouchPoint &point);
  void release();

private:
  static constexpr size_t kResourceCapacity = 288;

  struct Record {
    PermissionKind kind = PermissionKind::FileWrite;
    PermissionDecision decision = PermissionDecision::Pending;
    char resource[kResourceCapacity] = {};
  };

  struct Prompt {
    PermissionKind kind = PermissionKind::FileWrite;
    char resource[kResourceCapacity] = {};
  };

  Record *records = nullptr;
  size_t recordCount = 0;
  size_t recordCapacity = 0;
  Record emergencyRecord;
  Prompt prompt;
  bool hasEmergencyRecord = false;
  bool storageExhausted = false;
  bool hasPrompt = false;

  bool ensureCapacity();
  void resolve(PermissionDecision decision);
  const Record *find(PermissionKind kind, const char *resource) const;
};

} // namespace app_api

#endif
