/// Public firmware gateway for sandboxed external apps. It turns app requests
/// for files, modal UI, GPIO, sensors, Wi-Fi, recording, and playback into
/// session-scoped operations that the shell can permission and safely service.
#ifndef EXT_APP_API_H
#define EXT_APP_API_H

#include "sys/services/environment_monitor.h"
#include "sys/services/touch_input.h"
#include "ui/components/date_time_picker_component.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Shared result contract used by PinkApp's C ABI and PocketInkJS's adapters.
enum class AppApiStatus : int8_t {
  Error = -1,
  Denied = -2,
  Unsupported = -3,
  Pending = 0,
  Ok = 1,
};

static constexpr AppApiStatus kAppApiEnd = static_cast<AppApiStatus>(2);

/// Portable GPIO modes exposed to external apps rather than Arduino constants.
enum class AppApiGpioMode : uint8_t {
  Input = 0,
  InputPullup = 1,
  Output = 2,
};

/// Capability classes an approved AgentFiesta action can satisfy exactly once
/// without opening a second platform prompt.
enum class AppApiAgentCapability : uint8_t {
  None,
  FileWrite,
  FileDelete,
  Gpio,
  WifiScan,
  SystemConfig,
};

/// Bounded Wi-Fi scan entry copied out of the platform network stack.
struct AppApiWifiNetwork {
  /// Null-terminated network name, up to the IEEE 802.11 limit.
  char ssid[33] = {};
  /// Received signal strength in dBm.
  int32_t rssi = -127;
};

/// Callback used to consume transient directory entries during enumeration.
using AppApiDirectoryEntryHandler = bool (*)(const char *name,
                                              bool directory, uint32_t size,
                                              void *context);

/// Starts or resumes capability state for the active external app. Permissions
/// live only for this session and are discarded by appApiEndSession().
bool appApiBeginSession(const char *appId);
/// Releases permissions, modal state, media, capture, and configured GPIO.
void appApiEndSession();
/// Checks whether the active capability session belongs to an app id.
bool appApiSessionMatches(const char *appId);
/// Performs deferred hardware work from the shell/main core.
bool appApiUpdateOnMainCore();

AppApiStatus appApiRequestAgentAction(const char *action,
                                      AppApiAgentCapability capability,
                                      const char *resource);
AppApiStatus appApiRequestCapability(AppApiAgentCapability capability,
                                     const char *resource);
bool appApiAuthorizeCapability(AppApiAgentCapability capability,
                               const char *resource);

/// Shell-owned overlay hooks for permission prompts and the file picker.
bool appApiOverlayActive();
void appApiDrawOverlay(Adafruit_GFX &gfx);
bool appApiHandleOverlayTouch(const TouchPoint &point);
bool appApiHandleOverlayMenuButton();
bool appApiHandleOverlayPowerButton();

/// Reads an arbitrary chunk. outRead may be zero with End when offset is at or
/// beyond EOF. Passing a null output is allowed only when capacity is zero and
/// can be used to query the file size.
AppApiStatus appApiReadFile(const char *path, size_t offset, uint8_t *out,
                            size_t capacity, size_t *outRead,
                            size_t *outFileSize);
/// Enumerates one directory synchronously. The callback may return false to
/// stop early; names are valid only for the duration of the callback.
AppApiStatus appApiListDirectory(const char *path,
                                 AppApiDirectoryEntryHandler handler,
                                 void *context);

/// Writes or appends one chunk after a once-per-session, once-per-path prompt.
AppApiStatus appApiWriteFile(const char *path, const uint8_t *data,
                             size_t size, bool append);
/// Deletes one file after a once-per-session, once-per-path prompt.
AppApiStatus appApiDeleteFile(const char *path);

/// Opens the full-screen file picker. Repeating the request polls its state;
/// appApiTakePickedFile consumes a completed selection.
AppApiStatus appApiRequestFilePicker(const char *initialPath,
                                     const char *extensionFilter);
AppApiStatus appApiTakePickedFile(char *out, size_t outSize);

/// One-call modal input APIs. The first call opens system UI; subsequent calls
/// return Pending until the user accepts or cancels. Sandbox code never draws
/// or hit-tests the system widgets.
AppApiStatus appApiGetKeyboardInput(const char *initialText, uint8_t layout,
                                    size_t maxLength, char *out,
                                    size_t outSize);
AppApiStatus appApiGetDateInput(const DatePickerDate &initial,
                                DatePickerDate &out);

/// Queues a stable catalog id for the shell to launch after the current app
/// callback returns.
bool appApiRequestOpenApp(const char *appId);
bool appApiTakeOpenAppRequest(char *out, size_t outSize);

/// Configures an allow-listed expansion GPIO after session permission.
AppApiStatus appApiGpioConfigure(uint8_t pin, AppApiGpioMode mode);
/// Reads an allow-listed GPIO previously configured by this session.
AppApiStatus appApiGpioRead(uint8_t pin, int *value);
/// Writes an allow-listed output GPIO previously configured by this session.
AppApiStatus appApiGpioWrite(uint8_t pin, bool high);

/// Returns the cached environment snapshot and optionally requests a refresh.
EnvironmentSnapshot appApiSensorSnapshot(bool refresh);

/// Starts a confirmed scan and exposes a bounded snapshot after completion.
AppApiStatus appApiWifiScan();
/// Returns the number of entries in the completed bounded scan snapshot.
size_t appApiWifiNetworkCount();
/// Copies one completed scan entry; returns false when out of range.
bool appApiWifiNetworkAt(size_t index, AppApiWifiNetwork &out);

/// Simplified capture operation. The system pumps recording while Pending and
/// returns a session-owned WAV buffer at End. Repeating an active request with
/// maxDurationMs zero stops it early; poll with zero until End.
AppApiStatus appApiCaptureWav(uint32_t maxDurationMs, const uint8_t **data,
                              size_t *size);
AppApiStatus appApiCapturePcm16(uint32_t maxDurationMs, const uint8_t **data,
                                size_t *size);

/// Preloads independently replayable audio. MIDI is always available; other
/// codecs follow the firmware edition's headless Audio implementation.
bool appApiAudioFormatSupported(const char *format);
/// Preloads an audio path and returns a session handle, or a negative error.
int32_t appApiAudioLoadPath(const char *path);
/// Preloads copied audio bytes and returns a session handle, or a negative error.
int32_t appApiAudioLoadBuffer(const uint8_t *data, size_t size,
                              const char *format);
/// Starts the selected preloaded source from its beginning.
AppApiStatus appApiAudioPlay(uint32_t handle);
/// Stops the selected source while retaining its preload state.
AppApiStatus appApiAudioStop(uint32_t handle);
/// Polls playback and reports End after a source finishes naturally.
AppApiStatus appApiAudioUpdate(uint32_t handle);
/// Releases one session audio handle and its owned resources.
void appApiAudioRelease(uint32_t handle);

#endif
