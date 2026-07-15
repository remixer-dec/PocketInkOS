#include "sys/ext_app_api/app_api.h"

#include "sys/global.h"
#include "sys/psram_allocator.h"
#include "sys/ext_app_api/service_result.h"
#include "sys/ext_app_api/service_utils.h"
#include "sys/ext_app_api/audio_capture_service.h"
#include "sys/ext_app_api/audio_service.h"
#include "sys/ext_app_api/file_service.h"
#include "sys/ext_app_api/modal_service.h"
#include "sys/ext_app_api/permission_service.h"

#if ENABLE_NETWORK_APPS
#include "sys/services/wifi_manager.h"
#include <WiFi.h>
#endif

#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <new>

namespace {

using namespace ext_app_api;

constexpr uint8_t kMaxWifiNetworks = 24;

enum class SensorRefreshState : uint8_t { Idle, Requested, Completed };

struct SensorRefreshMailbox {
  // The app/runtime publishes Requested; only the shell/main core performs
  // the hardware refresh and publishes Completed.
  uint32_t refreshedAt = 0;
  std::atomic<SensorRefreshState> state{SensorRefreshState::Idle};
};

struct OpenAppMailbox {
  char requestedAppId[kAppIdCapacity] = {};
};

struct AppApiSession {
  explicit AppApiSession(const char *id)
      : files(permissions), modals(permissions, id), audio(files) {
    copyText(appId, sizeof(appId), id);
  }

  PermissionService permissions;
  FileService files;
  ModalService modals;
  AudioCaptureService audioCapture;
  AudioService audio;
  SensorRefreshMailbox sensorRefresh;
  OpenAppMailbox openApp;
  AppApiWifiNetwork wifiNetworks[kMaxWifiNetworks] = {};
  uint64_t configuredGpio = 0;
  char appId[kAppIdCapacity] = {};
  uint8_t wifiNetworkCount = 0;
  bool wifiScanned = false;

  void release() {
    audioCapture.release();
    audio.releaseAll();
    modals.release();
    permissions.release();
    for (uint8_t pin = 0; pin < 64; pin++) {
      if ((configuredGpio & (UINT64_C(1) << pin)) != 0) {
        pinMode(pin, INPUT);
      }
    }
  }
};

AppApiSession *session = nullptr;

void releaseSession() {
  if (session == nullptr) {
    return;
  }
  session->release();
  session->~AppApiSession();
  psram::release(session);
  session = nullptr;
}

bool supportedGpio(uint8_t pin) {
  switch (pin) {
  case 1:
  case 2:
  case 3:
  case 5:
  case 43:
  case 44:
    return true;
  default:
    return false;
  }
}

AppApiStatus abiStatus(const ServiceResult &result) {
  switch (result.state) {
  case ServiceState::Ok:
    return AppApiStatus::Ok;
  case ServiceState::Pending:
    return AppApiStatus::Pending;
  case ServiceState::End:
    return kAppApiEnd;
  case ServiceState::Failed:
    if (result.error == ServiceError::Denied) {
      return AppApiStatus::Denied;
    }
    if (result.error == ServiceError::Unsupported) {
      return AppApiStatus::Unsupported;
    }
    return AppApiStatus::Error;
  }
  return AppApiStatus::Error;
}

PermissionKind permissionKind(AppApiAgentCapability capability) {
  switch (capability) {
  case AppApiAgentCapability::FileWrite:
    return PermissionKind::FileWrite;
  case AppApiAgentCapability::FileDelete:
    return PermissionKind::FileDelete;
  case AppApiAgentCapability::Gpio:
    return PermissionKind::Gpio;
  case AppApiAgentCapability::WifiScan:
    return PermissionKind::WifiScan;
  case AppApiAgentCapability::SystemConfig:
    return PermissionKind::SystemConfig;
  case AppApiAgentCapability::None:
    return PermissionKind::AgentAction;
  }
  return PermissionKind::AgentAction;
}

} // namespace

bool appApiBeginSession(const char *appId) {
  if (session != nullptr && appApiSessionMatches(appId)) {
    return true;
  }
  releaseSession();
  if (appId == nullptr || appId[0] == '\0' ||
      strlen(appId) >= ext_app_api::kAppIdCapacity) {
    return false;
  }
  void *memory = psram::allocate(sizeof(AppApiSession));
  if (memory == nullptr) {
    return false;
  }
  session = new (memory) AppApiSession(appId);
  return true;
}

void appApiEndSession() { releaseSession(); }

bool appApiSessionMatches(const char *appId) {
  return session != nullptr && appId != nullptr &&
         strcmp(session->appId, appId) == 0;
}

bool appApiUpdateOnMainCore() {
  if (session == nullptr) {
    return false;
  }
  session->audioCapture.updateOnMainCore();

  SensorRefreshState refresh =
      session->sensorRefresh.state.load(std::memory_order_acquire);
  if (refresh == SensorRefreshState::Requested) {
    const uint32_t now = millis();
    if (session->sensorRefresh.refreshedAt == 0 ||
        now - session->sensorRefresh.refreshedAt >= 1000) {
      environmentMonitor.refresh();
      session->sensorRefresh.refreshedAt = now;
    }
    session->sensorRefresh.state.store(SensorRefreshState::Completed,
                                       std::memory_order_release);
  }
  return session->modals.update();
}

AppApiStatus appApiRequestAgentAction(const char *action,
                                      AppApiAgentCapability capability,
                                      const char *resource) {
  if (session == nullptr || action == nullptr || action[0] == '\0') {
    return AppApiStatus::Error;
  }
  const ServiceResult preflight =
      session->permissions.request(PermissionKind::AgentAction, action);
  if (preflight.state != ServiceState::Ok) {
    return abiStatus(preflight);
  }
  if (capability == AppApiAgentCapability::None) {
    return AppApiStatus::Ok;
  }
  return session->permissions.grant(permissionKind(capability), resource)
             ? AppApiStatus::Ok
             : AppApiStatus::Error;
}

AppApiStatus appApiRequestCapability(AppApiAgentCapability capability,
                                     const char *resource) {
  if (session == nullptr) {
    return AppApiStatus::Error;
  }
  if (capability == AppApiAgentCapability::None) {
    return AppApiStatus::Ok;
  }
  return abiStatus(
      session->permissions.request(permissionKind(capability), resource));
}

bool appApiAuthorizeCapability(AppApiAgentCapability capability,
                               const char *resource) {
  return session != nullptr && capability != AppApiAgentCapability::None &&
         session->permissions.grant(permissionKind(capability), resource);
}

bool appApiOverlayActive() {
  return session != nullptr && session->modals.active();
}

void appApiDrawOverlay(Adafruit_GFX &gfx) {
  if (session != nullptr) {
    session->modals.draw(gfx);
  }
}

bool appApiHandleOverlayTouch(const TouchPoint &point) {
  return session != nullptr && session->modals.handleTouch(point);
}

bool appApiHandleOverlayMenuButton() {
  return session != nullptr && session->modals.handleMenuButton();
}

bool appApiHandleOverlayPowerButton() {
  return session != nullptr && session->modals.handlePowerButton();
}

AppApiStatus appApiReadFile(const char *path, size_t offset, uint8_t *out,
                            size_t capacity, size_t *outRead,
                            size_t *outFileSize) {
  if (session == nullptr) {
    if (outRead != nullptr) {
      *outRead = 0;
    }
    if (outFileSize != nullptr) {
      *outFileSize = 0;
    }
    return AppApiStatus::Error;
  }
  return abiStatus(
      session->files.read(path, offset, out, capacity, outRead, outFileSize));
}

AppApiStatus appApiListDirectory(const char *path,
                                 AppApiDirectoryEntryHandler handler,
                                 void *context) {
  return session != nullptr
             ? abiStatus(session->files.list(path, handler, context))
             : AppApiStatus::Error;
}

AppApiStatus appApiWriteFile(const char *path, const uint8_t *data,
                             size_t size, bool append) {
  return session != nullptr
             ? abiStatus(
                   session->files.write(path, data, size, append))
             : AppApiStatus::Error;
}

AppApiStatus appApiDeleteFile(const char *path) {
  return session != nullptr
             ? abiStatus(session->files.remove(path))
             : AppApiStatus::Error;
}

AppApiStatus appApiRequestFilePicker(const char *initialPath,
                                     const char *extensionFilter) {
  return session != nullptr
             ? abiStatus(session->modals.requestFilePicker(
                   initialPath, extensionFilter))
             : AppApiStatus::Error;
}

AppApiStatus appApiTakePickedFile(char *out, size_t outSize) {
  return session != nullptr
             ? abiStatus(
                   session->modals.takePickedFile(out, outSize))
             : AppApiStatus::Error;
}

AppApiStatus appApiGetKeyboardInput(const char *initialText, uint8_t layout,
                                    size_t maxLength, char *out,
                                    size_t outSize) {
  return session != nullptr
             ? abiStatus(session->modals.keyboardInput(
                   initialText, layout, maxLength, out, outSize))
             : AppApiStatus::Error;
}

AppApiStatus appApiGetDateInput(const DatePickerDate &initial,
                                DatePickerDate &out) {
  return session != nullptr
             ? abiStatus(session->modals.dateInput(initial, out))
             : AppApiStatus::Error;
}

bool appApiRequestOpenApp(const char *appId) {
  return session != nullptr && appId != nullptr && appId[0] != '\0' &&
         ext_app_api::copyText(session->openApp.requestedAppId,
                           sizeof(session->openApp.requestedAppId), appId);
}

bool appApiTakeOpenAppRequest(char *out, size_t outSize) {
  if (session == nullptr || session->openApp.requestedAppId[0] == '\0' ||
      !ext_app_api::copyText(out, outSize, session->openApp.requestedAppId)) {
    return false;
  }
  session->openApp.requestedAppId[0] = '\0';
  return true;
}

AppApiStatus appApiGpioConfigure(uint8_t pin, AppApiGpioMode mode) {
  if (!supportedGpio(pin) ||
      static_cast<uint8_t>(mode) >
          static_cast<uint8_t>(AppApiGpioMode::Output)) {
    return AppApiStatus::Unsupported;
  }
  if (session == nullptr) {
    return AppApiStatus::Error;
  }
  ext_app_api::ServiceResult permission =
      session->permissions.request(ext_app_api::PermissionKind::Gpio, "");
  if (permission.state != ext_app_api::ServiceState::Ok) {
    return abiStatus(permission);
  }
  switch (mode) {
  case AppApiGpioMode::Input:
    pinMode(pin, INPUT);
    break;
  case AppApiGpioMode::InputPullup:
    pinMode(pin, INPUT_PULLUP);
    break;
  case AppApiGpioMode::Output:
    pinMode(pin, OUTPUT);
    break;
  }
  session->configuredGpio |= UINT64_C(1) << pin;
  return AppApiStatus::Ok;
}

AppApiStatus appApiGpioRead(uint8_t pin, int *value) {
  if (!supportedGpio(pin)) {
    return AppApiStatus::Unsupported;
  }
  if (session == nullptr || value == nullptr) {
    return AppApiStatus::Error;
  }
  ext_app_api::ServiceResult permission =
      session->permissions.request(ext_app_api::PermissionKind::Gpio, "");
  if (permission.state != ext_app_api::ServiceState::Ok) {
    return abiStatus(permission);
  }
  *value = digitalRead(pin);
  return AppApiStatus::Ok;
}

AppApiStatus appApiGpioWrite(uint8_t pin, bool high) {
  if (!supportedGpio(pin)) {
    return AppApiStatus::Unsupported;
  }
  if (session == nullptr) {
    return AppApiStatus::Error;
  }
  ext_app_api::ServiceResult permission =
      session->permissions.request(ext_app_api::PermissionKind::Gpio, "");
  if (permission.state != ext_app_api::ServiceState::Ok) {
    return abiStatus(permission);
  }
  digitalWrite(pin, high ? HIGH : LOW);
  session->configuredGpio |= UINT64_C(1) << pin;
  return AppApiStatus::Ok;
}

EnvironmentSnapshot appApiSensorSnapshot(bool refresh) {
  if (refresh && session != nullptr) {
    session->sensorRefresh.state.store(SensorRefreshState::Requested,
                                       std::memory_order_release);
  }
  return environmentMonitor.snapshot();
}

AppApiStatus appApiWifiScan() {
#if ENABLE_NETWORK_APPS
  if (session == nullptr) {
    return AppApiStatus::Error;
  }
  ext_app_api::ServiceResult permission =
      session->permissions.request(ext_app_api::PermissionKind::WifiScan, "");
  if (permission.state != ext_app_api::ServiceState::Ok) {
    return abiStatus(permission);
  }
  if (session->wifiScanned) {
    return AppApiStatus::Ok;
  }
  if (!wifiManager.startSta()) {
    return AppApiStatus::Error;
  }
  const int found = WiFi.scanNetworks();
  if (found < 0) {
    return AppApiStatus::Error;
  }
  session->wifiNetworkCount = static_cast<uint8_t>(
      found < kMaxWifiNetworks ? found : kMaxWifiNetworks);
  for (uint8_t i = 0; i < session->wifiNetworkCount; i++) {
    ext_app_api::copyText(session->wifiNetworks[i].ssid,
                      sizeof(session->wifiNetworks[i].ssid),
                      WiFi.SSID(i).c_str());
    session->wifiNetworks[i].rssi = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  session->wifiScanned = true;
  return AppApiStatus::Ok;
#else
  return AppApiStatus::Unsupported;
#endif
}

size_t appApiWifiNetworkCount() {
  return session != nullptr ? session->wifiNetworkCount : 0;
}

bool appApiWifiNetworkAt(size_t index, AppApiWifiNetwork &out) {
  if (session == nullptr || index >= session->wifiNetworkCount) {
    return false;
  }
  out = session->wifiNetworks[index];
  return true;
}

AppApiStatus appApiCaptureWav(uint32_t maxDurationMs, const uint8_t **data,
                              size_t *size) {
  if (data != nullptr) {
    *data = nullptr;
  }
  if (size != nullptr) {
    *size = 0;
  }
  return session != nullptr
             ? abiStatus(session->audioCapture.request(
                   ext_app_api::AudioCaptureService::Format::Wav, maxDurationMs,
                   data, size))
             : AppApiStatus::Error;
}

AppApiStatus appApiCapturePcm16(uint32_t maxDurationMs, const uint8_t **data,
                                size_t *size) {
  if (data != nullptr) {
    *data = nullptr;
  }
  if (size != nullptr) {
    *size = 0;
  }
  return session != nullptr
             ? abiStatus(session->audioCapture.request(
                   ext_app_api::AudioCaptureService::Format::Pcm16, maxDurationMs,
                   data, size))
             : AppApiStatus::Error;
}

bool appApiAudioFormatSupported(const char *format) {
  return session != nullptr && session->audio.formatSupported(format);
}

int32_t appApiAudioLoadPath(const char *path) {
  return session != nullptr ? session->audio.loadPath(path).handle : -1;
}

int32_t appApiAudioLoadBuffer(const uint8_t *data, size_t size,
                              const char *format) {
  return session != nullptr
             ? session->audio.loadBuffer(data, size, format).handle
             : -1;
}

AppApiStatus appApiAudioPlay(uint32_t handle) {
  return session != nullptr
             ? abiStatus(session->audio.play(handle))
             : AppApiStatus::Error;
}

AppApiStatus appApiAudioStop(uint32_t handle) {
  return session != nullptr
             ? abiStatus(session->audio.stop(handle))
             : AppApiStatus::Error;
}

AppApiStatus appApiAudioUpdate(uint32_t handle) {
  return session != nullptr
             ? abiStatus(session->audio.update(handle))
             : AppApiStatus::Error;
}

void appApiAudioRelease(uint32_t handle) {
  if (session != nullptr) {
    session->audio.release(handle);
  }
}
