#ifndef APP_API_SERVICES_H
#define APP_API_SERVICES_H

#include "sys/services/environment_monitor.h"
#include "sys/drivers/touch_input.h"
#include "ui/components/date_time_picker_component.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Shared result contract used by PinkApp's C ABI and PinkJS's adapters.
enum class AppApiStatus : int8_t {
  Error = -1,
  Denied = -2,
  Unsupported = -3,
  Pending = 0,
  Ok = 1,
  End = 2,
};

enum class AppApiGpioMode : uint8_t {
  Input = 0,
  InputPullup = 1,
  Output = 2,
};

struct AppApiWifiNetwork {
  char ssid[33] = {};
  int32_t rssi = -127;
};

using AppApiDirectoryEntryHandler = bool (*)(const char *name,
                                              bool directory, uint32_t size,
                                              void *context);

/// Starts or resumes capability state for the active external app. Permissions
/// live only for this session and are discarded by appApiEndSession().
bool appApiBeginSession(const char *appId);
void appApiEndSession();
bool appApiSessionMatches(const char *appId);
/// Performs deferred hardware work from the shell/main core.
bool appApiUpdateOnMainCore();

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

AppApiStatus appApiGpioConfigure(uint8_t pin, AppApiGpioMode mode);
AppApiStatus appApiGpioRead(uint8_t pin, int *value);
AppApiStatus appApiGpioWrite(uint8_t pin, bool high);

EnvironmentSnapshot appApiSensorSnapshot(bool refresh);

/// Starts a confirmed scan and exposes a bounded snapshot after completion.
AppApiStatus appApiWifiScan();
size_t appApiWifiNetworkCount();
bool appApiWifiNetworkAt(size_t index, AppApiWifiNetwork &out);

/// Simplified capture operation. The system pumps recording while Pending and
/// returns a session-owned WAV buffer at End.
AppApiStatus appApiCaptureWav(uint32_t maxDurationMs, const uint8_t **data,
                              size_t *size);
AppApiStatus appApiCapturePcm16(uint32_t maxDurationMs, const uint8_t **data,
                                size_t *size);

/// Preloads independently replayable audio. MIDI is always available; other
/// codecs follow the firmware edition's headless Audio implementation.
bool appApiAudioFormatSupported(const char *format);
int32_t appApiAudioLoadPath(const char *path);
int32_t appApiAudioLoadBuffer(const uint8_t *data, size_t size,
                              const char *format);
AppApiStatus appApiAudioPlay(uint32_t handle);
AppApiStatus appApiAudioStop(uint32_t handle);
AppApiStatus appApiAudioUpdate(uint32_t handle);
void appApiAudioRelease(uint32_t handle);

#endif
