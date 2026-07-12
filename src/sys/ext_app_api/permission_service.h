/// Holds per-session capability decisions for potentially destructive or
/// privacy-sensitive external-app operations. Unknown requests become a shell
/// prompt; grants and denials are remembered only until the app session ends.
#ifndef EXT_APP_API_PERMISSION_SERVICE_H
#define EXT_APP_API_PERMISSION_SERVICE_H

#include "sys/services/touch_input.h"
#include "sys/ext_app_api/service_result.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

namespace ext_app_api {

/// Capability classes that require explicit user consent.
enum class PermissionKind : uint8_t { FileWrite, FileDelete, Gpio, WifiScan };
/// Stored or pending answer to one capability request.
enum class PermissionDecision : int8_t { Denied = -1, Pending = 0, Granted = 1 };

/// Session-local permission records plus the currently unresolved user prompt.
class PermissionService {
public:
  /// Returns a remembered decision or queues a prompt for a new resource.
  ServiceResult request(PermissionKind kind, const char *resource);
  /// Reports whether an unresolved request needs shell-owned prompt UI.
  bool promptActive() const;
  /// Draws the current capability and resource prompt for the named app.
  void drawPrompt(Adafruit_GFX &gfx, const char *appId) const;
  /// Resolves the prompt when its allow or deny control is touched.
  bool handlePromptTouch(const TouchPoint &point);
  /// Forgets all decisions and releases their PSRAM storage.
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

} // namespace ext_app_api

#endif
