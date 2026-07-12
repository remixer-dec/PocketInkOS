#include "sys/pocketinkjs_app.h"

#include "fs/file_provider.h"
#include "fs/providers/image_file_viewer.h"
#include "fs/providers/svg_file_viewer.h"
#include "sys/services/connectivity_service.h"
#include "sys/ext_app_api/app_api.h"
#include "sys/services/device_clock.h"
#include "sys/global.h"
#include "sys/services/lockscreen_guard.h"
#include "sys/psram_allocator.h"
#ifndef POCKETINKJS_USE_LIGHTWEIGHT_JSON_PARSER
#define POCKETINKJS_USE_LIGHTWEIGHT_JSON_PARSER 0
#endif
#if POCKETINKJS_USE_LIGHTWEIGHT_JSON_PARSER
#define POCKETINKJS_DISABLE_BUILTIN_JSON_PARSER 1
#include "netapps/lightweight_json_parser.h"
#endif
#include "sys/runtime/pocketinkjs/pocketinkjs.h"
#include "sys/services/sd_storage.h"
#include "sys/system_config.h"
#include "sys/system_fonts.h"
#include "ui/components/list_component.h"
#include "ui/components/touch_button.h"
#include "ui/ui_helpers.h"
#include "ui/system_qr.h"
#include <Arduino.h>
#if ENABLE_NETWORK_APPS
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif
#include <cstdlib>
#include <atomic>
#include <cstdarg>
#include <climits>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <new>
#include <stdio.h>

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define POCKETINKJS_HAS_ROM_PRINTF 1
#else
#define POCKETINKJS_HAS_ROM_PRINTF 0
#endif

namespace {

constexpr size_t kPocketInkJSMinWorkspaceBytes = 160U * 1024U;
constexpr size_t kPocketInkJSPsramReserveBytes = 512U * 1024U;
constexpr size_t kPocketInkJSAllocationHeadroomBytes = 32U * 1024U;
constexpr size_t kPocketInkJSMaxSourceBytes = 96U * 1024U;
constexpr size_t kPocketInkJSMaxHttpBytes = 64U * 1024U;
constexpr uint32_t kPocketInkJSHttpTimeoutMs = 8000;
constexpr uint8_t kPocketInkJSMaxTimers = 16;
constexpr size_t kPocketInkJSJobsPerUpdate = 32;
constexpr uint8_t kPocketInkJSFetchQueueDepth = 3;
constexpr uint32_t kPocketInkJSWorkerStackBytes = 7168;
constexpr uint32_t kPocketInkJSExecutorStackBytes = 40U * 1024U;
constexpr UBaseType_t kPocketInkJSWorkerPriority = 1;
constexpr BaseType_t kPocketInkJSWorkerCore = 0;
constexpr UBaseType_t kPocketInkJSExecutorPriority = 1;
constexpr BaseType_t kPocketInkJSExecutorCore = 1;
constexpr size_t kIdCapacity = 20;
constexpr size_t kStatusCapacity = 96;

class PocketInkJSApp;

using PocketInkJSExecutorOperation = void (*)(PocketInkJSApp &, void *);

struct PocketInkJSExecutorCommand {
  PocketInkJSExecutorOperation operation = nullptr;
  void *context = nullptr;
};

enum PocketInkJSTouchCallback : uint8_t {
  PocketInkJSTouchGeneric = 1U << 0U,
  PocketInkJSTouchDown = 1U << 1U,
  PocketInkJSTouchMove = 1U << 2U,
  PocketInkJSTouchUp = 1U << 3U,
};

struct FetchHeader {
  char name[48] = {};
  char value[160] = {};
};

struct FetchRequest {
  PocketInkJS::RootHandle promise = PocketInkJS::kInvalidRoot;
  char url[512] = {};
  char method[12] = "GET";
  uint8_t *body = nullptr;
  size_t bodyBytes = 0;
  size_t maxBytes = kPocketInkJSMaxHttpBytes;
  uint32_t timeoutMs = kPocketInkJSHttpTimeoutMs;
  FetchHeader headers[6] = {};
  uint8_t headerCount = 0;
};

struct FetchCompletion {
  PocketInkJS::RootHandle promise = PocketInkJS::kInvalidRoot;
  char url[512] = {};
  uint8_t *body = nullptr;
  size_t bodyBytes = 0;
  int statusCode = 0;
  PocketInkJS::ErrorCode error = {};
};

struct AsyncState {
  QueueHandle_t requests = nullptr;
  QueueHandle_t completions = nullptr;
  TaskHandle_t task = nullptr;
  std::atomic<bool> stopping{false};
};

struct TimerSlot {
  uint32_t id = 0;
  uint32_t dueAt = 0;
  uint32_t intervalMs = 0;
  PocketInkJS::RootHandle callback = PocketInkJS::kInvalidRoot;
  PocketInkJS::RootHandle arguments = PocketInkJS::kInvalidRoot;
  bool active = false;
};

struct TimerState {
  TimerSlot slots[kPocketInkJSMaxTimers] = {};
  uint32_t nextId = 1;
};

enum class PocketInkJSSystemModal : uint8_t {
  None,
  FilePicker,
  Keyboard,
  DatePicker,
  AudioCapture,
  AudioCapturePcm16,
};

bool activeSystemModalIdle();
bool beginActiveSystemModal(PocketInkJSSystemModal kind, PocketInkJS::Value promise,
                            uint32_t captureDurationMs = 0,
                            DatePickerDate initialDate = {});

void setFetchError(FetchCompletion *completion, PocketInkJS::ErrorCode error) {
  completion->error = error;
}

PocketInkJSApp *activePocketInkJSApp = nullptr;
Adafruit_GFX *PocketInkJSDrawTarget = nullptr;
uint8_t PocketInkJSSleepGuardDepth = 0;
UBaseType_t PocketInkJSLowestStackHwm = UINT_MAX;
char *PocketInkJSLogBuffer = nullptr;

void PocketInkJSLogWrite(const char *text) {
#if POCKETINKJS_HAS_ROM_PRINTF
  esp_rom_printf("%s", text != nullptr ? text : "");
#else
  std::fputs(text != nullptr ? text : "", stdout);
#endif
}

void PocketInkJSLogPrintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
#if POCKETINKJS_HAS_ROM_PRINTF
  esp_rom_vprintf(format, args);
#else
  std::vprintf(format, args);
#endif
  va_end(args);
}

void logPocketInkJSStackLow(const char *callback) {
#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && INCLUDE_uxTaskGetStackHighWaterMark
  const UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(nullptr);
  if (highWaterMark >= PocketInkJSLowestStackHwm)
    return;
  PocketInkJSLowestStackHwm = highWaterMark;
  PocketInkJSLogPrintf("[PocketInkJS] stack low callback=%s hwm=%u\n",
                  callback != nullptr ? callback : "?",
                  static_cast<unsigned>(highWaterMark));
#else
  (void)callback;
#endif
}

bool copyText(char *destination, size_t destinationSize, const char *source) {
  if (destination == nullptr || destinationSize == 0) {
    return false;
  }
  const char *text = source != nullptr ? source : "";
  const size_t length = strlen(text);
  const size_t amount = length < destinationSize ? length : destinationSize - 1U;
  if (amount > 0) {
    memcpy(destination, text, amount);
  }
  destination[amount] = '\0';
  return length < destinationSize;
}

void drawPocketInkJSCentered(Adafruit_GFX &gfx, const char *text, int16_t y,
                        uint8_t textSize) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  gfx.setTextSize(textSize);
  gfx.getTextBounds(text != nullptr ? text : "", 0, 0, &x1, &y1, &width,
                    &height);
  gfx.setCursor((EPD_WIDTH - static_cast<int16_t>(width)) / 2 - x1, y);
  gfx.print(text != nullptr ? text : "");
}

void drawPocketInkJSWrappedCentered(Adafruit_GFX &gfx, const char *text,
                               int16_t firstY) {
  constexpr size_t kLineCharacters = 30;
  const size_t length = text != nullptr ? strlen(text) : 0;
  size_t at = 0;
  int16_t y = firstY;
  char line[kLineCharacters + 1U];
  while (at < length && y < EPD_HEIGHT) {
    while (at < length && text[at] == ' ')
      at++;
    size_t end = at + kLineCharacters < length ? at + kLineCharacters : length;
    if (end < length) {
      size_t breakAt = end;
      while (breakAt > at && text[breakAt] != ' ')
        breakAt--;
      if (breakAt > at)
        end = breakAt;
    }
    const size_t amount = end - at;
    memcpy(line, text + at, amount);
    line[amount] = '\0';
    drawPocketInkJSCentered(gfx, line, y, 1);
    y += 14;
    at = end;
  }
}

bool PocketInkJSErrorNumber(const char *message, uint8_t *number) {
  if (message == nullptr || number == nullptr || message[0] != 'E' ||
      message[1] < '0' || message[1] > '9' || message[2] < '0' ||
      message[2] > '9' || message[3] < '0' || message[3] > '9') {
    return false;
  }
  const unsigned value = static_cast<unsigned>(message[1] - '0') * 100U +
                         static_cast<unsigned>(message[2] - '0') * 10U +
                         static_cast<unsigned>(message[3] - '0');
  if (value == 0 || value > UINT8_MAX)
    return false;
  *number = static_cast<uint8_t>(value);
  return true;
}

uint32_t hashPath(const char *text) {
  uint32_t hash = 2166136261UL;
  while (text != nullptr && *text != '\0') {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619UL;
  }
  return hash;
}

const char *filenameForPath(const char *path) {
  const char *slash = path != nullptr ? strrchr(path, '/') : nullptr;
  return slash != nullptr ? slash + 1 : (path != nullptr ? path : "");
}

void copyLabel(char *destination, size_t destinationSize, const char *path) {
  const char *name = filenameForPath(path);
  size_t length = 0;
  while (name[length] != '\0' && name[length] != '.' &&
         length + 1U < destinationSize) {
    const char c = name[length];
    destination[length++] = c >= 0x20 && c <= 0x7e ? c : '_';
  }
  if (destinationSize > 0) {
    destination[length] = '\0';
  }
  if (length == 0) {
    copyText(destination, destinationSize, "script");
  }
}

void *allocatePsram(size_t bytes) {
  return bytes > 0 ? psram::allocate(bytes) : nullptr;
}

void *allocatePocketInkJSWorkspace(size_t *allocatedBytes) {
  if (allocatedBytes == nullptr)
    return nullptr;
  *allocatedBytes = 0;
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t freeBytes = heap_caps_get_free_size(caps);
  const size_t largestBlock = heap_caps_get_largest_free_block(caps);
  // Leave at least 512 KiB, or 25% on larger devices, for fetch bodies,
  // component state, TLS, and other active services.
  const size_t reserve = freeBytes / 4U > kPocketInkJSPsramReserveBytes
                             ? freeBytes / 4U
                             : kPocketInkJSPsramReserveBytes;
  const size_t freeBudget = freeBytes > reserve ? freeBytes - reserve : 0;
  const size_t blockBudget =
      largestBlock > kPocketInkJSAllocationHeadroomBytes
          ? largestBlock - kPocketInkJSAllocationHeadroomBytes
          : 0;
  size_t candidate = freeBudget < blockBudget ? freeBudget : blockBudget;
  candidate &= ~size_t(4095U);
  if (candidate < kPocketInkJSMinWorkspaceBytes)
    candidate = kPocketInkJSMinWorkspaceBytes;

  while (candidate >= kPocketInkJSMinWorkspaceBytes) {
    void *workspace = allocatePsram(candidate);
    if (workspace != nullptr) {
      *allocatedBytes = candidate;
      return workspace;
    }
    candidate = (candidate / 2U) & ~size_t(4095U);
  }
  return nullptr;
}

void releaseFetchRequest(FetchRequest *request) {
  if (request == nullptr)
    return;
  if (request->body != nullptr)
    heap_caps_free(request->body);
  heap_caps_free(request);
}

void releaseFetchCompletion(FetchCompletion *completion) {
  if (completion == nullptr)
    return;
  if (completion->body != nullptr)
    heap_caps_free(completion->body);
  heap_caps_free(completion);
}

FetchCompletion *performFetch(FetchRequest *request,
                              const std::atomic<bool> &stopping) {
  FetchCompletion *completion = static_cast<FetchCompletion *>(
      allocatePsram(sizeof(FetchCompletion)));
  if (completion == nullptr)
    return nullptr;
  new (completion) FetchCompletion();
  completion->promise = request->promise;
  copyText(completion->url, sizeof(completion->url), request->url);
#if ENABLE_NETWORK_APPS
  if (!wifiIsConnected()) {
    setFetchError(completion, PocketInkJS::ErrorCode{190});
    return completion;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(request->timeoutMs);
  if (!http.begin(client, request->url)) {
    setFetchError(completion, PocketInkJS::ErrorCode{193});
    return completion;
  }
  http.addHeader("User-Agent", "PocketInk-PocketInkJS/0.2");
  for (uint8_t i = 0; i < request->headerCount; i++)
    http.addHeader(request->headers[i].name, request->headers[i].value);
  const int code = strcmp(request->method, "GET") == 0
                       ? http.GET()
                       : http.sendRequest(request->method, request->body,
                                          request->bodyBytes);
  completion->statusCode = code;
  if (code <= 0) {
    http.end();
    setFetchError(completion, PocketInkJS::ErrorCode{194});
    return completion;
  }
  const int expected = http.getSize();
  if (expected > 0 && static_cast<size_t>(expected) > request->maxBytes) {
    http.end();
    setFetchError(completion, PocketInkJS::ErrorCode{195});
    return completion;
  }
  const size_t allocationBytes =
      expected > 0 ? static_cast<size_t>(expected) : request->maxBytes;
  if (allocationBytes > 0) {
    completion->body = static_cast<uint8_t *>(allocatePsram(allocationBytes));
    if (completion->body == nullptr) {
      http.end();
      setFetchError(completion, PocketInkJS::ErrorCode{196});
      return completion;
    }
  }
  auto *stream = http.getStreamPtr();
  const uint32_t startedAt = millis();
  while (!stopping.load() && completion->bodyBytes < allocationBytes) {
    while (stream->available() > 0 &&
           completion->bodyBytes < allocationBytes) {
      const int byte = stream->read();
      if (byte < 0)
        break;
      completion->body[completion->bodyBytes++] = static_cast<uint8_t>(byte);
    }
    if ((expected >= 0 &&
         completion->bodyBytes >= static_cast<size_t>(expected)) ||
        (!stream->connected() && stream->available() <= 0)) {
      break;
    }
    if (millis() - startedAt >= request->timeoutMs)
      break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  http.end();
  if (!stopping.load() && expected > 0 &&
      completion->bodyBytes != static_cast<size_t>(expected)) {
    setFetchError(completion, PocketInkJS::ErrorCode{197});
  }
#else
  (void)stopping;
  setFetchError(completion, PocketInkJS::ErrorCode{198});
#endif
  return completion;
}

void PocketInkJSWorker(void *context) {
  AsyncState *state = static_cast<AsyncState *>(context);
  while (state != nullptr && !state->stopping.load()) {
    FetchRequest *request = nullptr;
    if (xQueueReceive(state->requests, &request, pdMS_TO_TICKS(20)) != pdPASS)
      continue;
    if (request == nullptr)
      continue;
    FetchCompletion *completion = performFetch(request, state->stopping);
    releaseFetchRequest(request);
    if (completion != nullptr &&
        (state->stopping.load() ||
         xQueueSend(state->completions, &completion, 0) != pdPASS)) {
      releaseFetchCompletion(completion);
    }
  }
  if (state != nullptr) {
    FetchRequest *request = nullptr;
    while (xQueueReceive(state->requests, &request, 0) == pdPASS)
      releaseFetchRequest(request);
    FetchCompletion *completion = nullptr;
    while (xQueueReceive(state->completions, &completion, 0) == pdPASS)
      releaseFetchCompletion(completion);
    vQueueDelete(state->requests);
    vQueueDelete(state->completions);
    state->~AsyncState();
    heap_caps_free(state);
  }
  vTaskDelete(nullptr);
}

void releaseSleepGuards() {
  while (PocketInkJSSleepGuardDepth > 0) {
    PocketInkJSSleepGuardDepth--;
    lockscreenGuardRelease();
  }
}

bool numericArguments(PocketInkJS::Runtime &runtime, const PocketInkJS::Value *args,
                      size_t argc, size_t expected) {
  if (argc != expected) {
    runtime.fail(PocketInkJS::ErrorCode{157});
    return false;
  }
  for (size_t i = 0; i < argc; i++) {
    if (runtime.typeOf(args[i]) != PocketInkJS::ValueType::Number ||
        !std::isfinite(runtime.toNumber(args[i]))) {
      runtime.fail(PocketInkJS::ErrorCode{158});
      return false;
    }
  }
  return true;
}

bool copyJsString(PocketInkJS::Runtime &runtime, PocketInkJS::Value value, char *out,
                  size_t outSize) {
  if (!runtime.isString(value) || out == nullptr || outSize == 0) {
    return false;
  }
  const PocketInkJS::StringView text = runtime.stringView(value);
  if (text.size >= outSize) {
    return false;
  }
  memcpy(out, text.data, text.size);
  out[text.size] = '\0';
  return true;
}

int16_t toI16(double value) {
  if (value < -32768.0) {
    return INT16_MIN;
  }
  if (value > 32767.0) {
    return INT16_MAX;
  }
  return static_cast<int16_t>(value);
}

uint16_t toU16(double value) {
  if (value <= 0) {
    return 0;
  }
  if (value >= 65535.0) {
    return UINT16_MAX;
  }
  return static_cast<uint16_t>(value);
}

uint8_t toU8(double value) {
  if (value <= 0) {
    return 0;
  }
  if (value >= 255.0) {
    return UINT8_MAX;
  }
  return static_cast<uint8_t>(value);
}

PocketInkJS::Value nativeConsoleLog(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc, void *) {
  if (PocketInkJSLogBuffer == nullptr) {
    PocketInkJSLogBuffer = static_cast<char *>(allocatePsram(160));
  }
  if (PocketInkJSLogBuffer == nullptr) {
    PocketInkJSLogWrite("[PocketInkJS] [log buffer unavailable]\n");
    return runtime.undefined();
  }
  PocketInkJSLogWrite("[PocketInkJS]");
  for (size_t i = 0; i < argc; i++) {
    runtime.format(args[i], PocketInkJSLogBuffer, 160);
    PocketInkJSLogWrite(" ");
    PocketInkJSLogWrite(PocketInkJSLogBuffer);
  }
  PocketInkJSLogWrite("\n");
  return runtime.undefined();
}

PocketInkJS::Value nativeMillis(PocketInkJS::Runtime &runtime, const PocketInkJS::Value *,
                           size_t argc, void *) {
  if (argc != 0) {
    return runtime.fail(PocketInkJS::ErrorCode{159});
  }
  return runtime.number(static_cast<double>(millis()));
}

PocketInkJS::Value nativePocketInkJSHeapUsed(PocketInkJS::Runtime &runtime,
                                   const PocketInkJS::Value *, size_t argc,
                                   void *) {
  return argc == 0
             ? runtime.number(
                   static_cast<double>(runtime.memoryStats().heapUsedBytes))
             : runtime.fail(PocketInkJS::ErrorCode{157});
}

PocketInkJS::Value nativePocketInkJSHeapFree(PocketInkJS::Runtime &runtime,
                                   const PocketInkJS::Value *, size_t argc,
                                   void *) {
  return argc == 0
             ? runtime.number(
                   static_cast<double>(runtime.memoryStats().heapFreeBytes))
             : runtime.fail(PocketInkJS::ErrorCode{157});
}

PocketInkJS::Value nativePocketInkJSHeapPeak(PocketInkJS::Runtime &runtime,
                                   const PocketInkJS::Value *, size_t argc,
                                   void *) {
  return argc == 0
             ? runtime.number(static_cast<double>(
                   runtime.memoryStats().peakHeapUsedBytes))
             : runtime.fail(PocketInkJS::ErrorCode{157});
}

PocketInkJS::Value nativeNetworkConnected(PocketInkJS::Runtime &runtime,
                                     const PocketInkJS::Value *, size_t argc,
                                     void *) {
  if (argc != 0) {
    return runtime.fail(PocketInkJS::ErrorCode{160});
  }
  return runtime.boolean(wifiIsConnected());
}

PocketInkJS::Value nativeSetWakeLock(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  if (argc != 1) {
    return runtime.fail(PocketInkJS::ErrorCode{162});
  }
  if (runtime.toBoolean(args[0])) {
    if (PocketInkJSSleepGuardDepth < 255U) {
      PocketInkJSSleepGuardDepth++;
      lockscreenGuardAcquire();
    }
  } else if (PocketInkJSSleepGuardDepth > 0U) {
    PocketInkJSSleepGuardDepth--;
    lockscreenGuardRelease();
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiSetFont(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (argc != 1)
    return runtime.fail(PocketInkJS::ErrorCode{157});
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.undefined();
  }
  if (runtime.isString(args[0])) {
    char name[32];
    return copyJsString(runtime, args[0], name, sizeof(name)) &&
                   systemFontApply(*PocketInkJSDrawTarget, name)
               ? runtime.boolean(true)
               : runtime.boolean(false);
  }
  if (!runtime.isNumber(args[0]) ||
      !std::isfinite(runtime.toNumber(args[0])))
    return runtime.fail(PocketInkJS::ErrorCode{158});
  switch (toI16(runtime.toNumber(args[0]))) {
  case 1:
    systemFontApply(*PocketInkJSDrawTarget, size_t{1});
    break;
  case 2:
    systemFontApply(*PocketInkJSDrawTarget, size_t{2});
    break;
  default:
    systemFontApply(*PocketInkJSDrawTarget, size_t{0});
    break;
  }
  return runtime.boolean(true);
}

PocketInkJS::Value nativeUiSetTextSize(PocketInkJS::Runtime &runtime,
                                  const PocketInkJS::Value *args, size_t argc,
                                  void *) {
  if (numericArguments(runtime, args, argc, 1) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->setTextSize(toU8(runtime.toNumber(args[0])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiSetTextColor(PocketInkJS::Runtime &runtime,
                                   const PocketInkJS::Value *args, size_t argc,
                                   void *) {
  if (numericArguments(runtime, args, argc, 1) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->setTextColor(toU16(runtime.toNumber(args[0])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiSetCursor(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (numericArguments(runtime, args, argc, 2) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->setCursor(toI16(runtime.toNumber(args[0])),
                                toI16(runtime.toNumber(args[1])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiPrint(PocketInkJS::Runtime &runtime,
                            const PocketInkJS::Value *args, size_t argc, void *) {
  if (argc != 1) {
    return runtime.fail(PocketInkJS::ErrorCode{163});
  }
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.undefined();
  }
  if (runtime.isString(args[0])) {
    const PocketInkJS::StringView text = runtime.stringView(args[0]);
    for (size_t i = 0; i < text.size; i++) {
      char character[2] = {text.data[i], '\0'};
      PocketInkJSDrawTarget->print(character);
    }
  } else {
    char text[96];
    runtime.format(args[0], text, sizeof(text));
    PocketInkJSDrawTarget->print(text);
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiDrawPixel(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (numericArguments(runtime, args, argc, 3) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->drawPixel(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toU16(runtime.toNumber(args[2])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiDrawLine(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc, void *) {
  if (numericArguments(runtime, args, argc, 5) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->drawLine(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toI16(runtime.toNumber(args[2])), toI16(runtime.toNumber(args[3])),
        toU16(runtime.toNumber(args[4])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiDrawRect(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc, void *) {
  if (numericArguments(runtime, args, argc, 5) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->drawRect(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toI16(runtime.toNumber(args[2])), toI16(runtime.toNumber(args[3])),
        toU16(runtime.toNumber(args[4])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiFillRect(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc, void *) {
  if (numericArguments(runtime, args, argc, 5) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->fillRect(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toI16(runtime.toNumber(args[2])), toI16(runtime.toNumber(args[3])),
        toU16(runtime.toNumber(args[4])));
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiFillScreen(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  if (numericArguments(runtime, args, argc, 1) && PocketInkJSDrawTarget != nullptr) {
    PocketInkJSDrawTarget->fillScreen(toU16(runtime.toNumber(args[0])));
  }
  return runtime.undefined();
}

PocketInkJS::Value apiStatusValue(PocketInkJS::Runtime &runtime, AppApiStatus status) {
  return runtime.number(static_cast<int8_t>(status));
}

double imageOptionNumber(PocketInkJS::Runtime &runtime, PocketInkJS::Value options,
                         const char *key, double fallback) {
  if (runtime.typeOf(options) != PocketInkJS::ValueType::Object) {
    return fallback;
  }
  const PocketInkJS::Value value = runtime.objectGet(options, key);
  return runtime.isNumber(value) && std::isfinite(runtime.toNumber(value))
             ? runtime.toNumber(value)
             : fallback;
}

bool imageOptions(PocketInkJS::Runtime &runtime, PocketInkJS::Value options, int16_t &x,
                  int16_t &y, int16_t &w, int16_t &h, uint8_t &dither,
                  bool &fit, ImageTransform &transform) {
  if (runtime.typeOf(options) != PocketInkJS::ValueType::Undefined &&
      runtime.typeOf(options) != PocketInkJS::ValueType::Object) {
    return false;
  }
  x = toI16(imageOptionNumber(runtime, options, "x", 0));
  y = toI16(imageOptionNumber(runtime, options, "y", 0));
  w = toI16(imageOptionNumber(runtime, options, "width", EPD_WIDTH));
  h = toI16(imageOptionNumber(runtime, options, "height", EPD_HEIGHT));
  dither = toU8(imageOptionNumber(runtime, options, "dither", 1));
  if (runtime.typeOf(options) == PocketInkJS::ValueType::Object) {
    const PocketInkJS::Value fitValue = runtime.objectGet(options, "scaleToFit");
    fit = runtime.typeOf(fitValue) == PocketInkJS::ValueType::Undefined
              ? true
              : runtime.toBoolean(fitValue);
    const PocketInkJS::Value matrix = runtime.objectGet(options, "matrix");
    if (runtime.typeOf(matrix) == PocketInkJS::ValueType::Array) {
      if (runtime.arrayLength(matrix) != 6) {
        return false;
      }
      float *values[] = {&transform.a, &transform.b, &transform.c,
                         &transform.d, &transform.tx, &transform.ty};
      for (size_t i = 0; i < 6; i++) {
        const PocketInkJS::Value value = runtime.arrayAt(matrix, i);
        if (!runtime.isNumber(value) ||
            !std::isfinite(runtime.toNumber(value))) {
          return false;
        }
        *values[i] = static_cast<float>(runtime.toNumber(value));
      }
    } else {
      transform.a = static_cast<float>(
          imageOptionNumber(runtime, options, "a", transform.a));
      transform.b = static_cast<float>(
          imageOptionNumber(runtime, options, "b", transform.b));
      transform.c = static_cast<float>(
          imageOptionNumber(runtime, options, "c", transform.c));
      transform.d = static_cast<float>(
          imageOptionNumber(runtime, options, "d", transform.d));
      transform.tx = static_cast<float>(
          imageOptionNumber(runtime, options, "tx", transform.tx));
      transform.ty = static_cast<float>(
          imageOptionNumber(runtime, options, "ty", transform.ty));
    }
  } else {
    fit = true;
  }
  return w > 0 && h > 0;
}

PocketInkJS::Value nativeUiImagePath(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail("ui.imagePath expects path and optional options");
  }
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.boolean(false);
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("image path is too long");
  }
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, nullptr) ==
      AppApiStatus::Error) {
    return runtime.boolean(false);
  }
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  uint8_t dither = 1;
  bool fit = true;
  ImageTransform transform;
  if (!imageOptions(runtime, argc == 2 ? args[1] : runtime.undefined(), x, y,
                    w, h, dither, fit, transform)) {
    return runtime.fail("invalid image options");
  }
  return runtime.boolean(drawImagePathTransformed(
      *PocketInkJSDrawTarget, "sd", path, x, y, w, h, dither, fit, transform));
}

PocketInkJS::Value nativeUiImageBuffer(PocketInkJS::Runtime &runtime,
                                  const PocketInkJS::Value *args, size_t argc,
                                  void *) {
  if (argc < 2 || argc > 3 || !runtime.isString(args[0]) ||
      !runtime.isString(args[1])) {
    return runtime.fail(
        "ui.imageBuffer expects binary string, format, and options");
  }
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.boolean(false);
  }
  char format[12];
  if (!copyJsString(runtime, args[1], format, sizeof(format))) {
    return runtime.fail("image format is too long");
  }
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  uint8_t dither = 1;
  bool fit = true;
  ImageTransform transform;
  if (!imageOptions(runtime, argc == 3 ? args[2] : runtime.undefined(), x, y,
                    w, h, dither, fit, transform)) {
    return runtime.fail("invalid image options");
  }
  const PocketInkJS::StringView data = runtime.stringView(args[0]);
  return runtime.boolean(drawImageBufferTransformed(
      *PocketInkJSDrawTarget, reinterpret_cast<const uint8_t *>(data.data),
      data.size, format, x, y, w, h, dither, fit, transform));
}

PocketInkJS::Value nativeUiVectorPath(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail("ui.vectorPath expects path and optional options");
  }
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.boolean(false);
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("vector path is too long");
  }
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, nullptr) ==
      AppApiStatus::Error) {
    return runtime.boolean(false);
  }
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  uint8_t dither = 1;
  bool fit = true;
  ImageTransform transform;
  if (!imageOptions(runtime, argc == 2 ? args[1] : runtime.undefined(), x, y,
                    w, h, dither, fit, transform)) {
    return runtime.fail("invalid vector options");
  }
  (void)dither;
  (void)fit;
  return runtime.boolean(drawVectorPathTransformed(
      *PocketInkJSDrawTarget, "sd", path, x, y, w, h, transform));
}

PocketInkJS::Value nativeUiVectorString(PocketInkJS::Runtime &runtime,
                                   const PocketInkJS::Value *args, size_t argc,
                                   void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail("ui.vectorString expects SVG text and optional options");
  }
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.boolean(false);
  }
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  uint8_t dither = 1;
  bool fit = true;
  ImageTransform transform;
  if (!imageOptions(runtime, argc == 2 ? args[1] : runtime.undefined(), x, y,
                    w, h, dither, fit, transform)) {
    return runtime.fail("invalid vector options");
  }
  (void)dither;
  (void)fit;
  const PocketInkJS::StringView svg = runtime.stringView(args[0]);
  return runtime.boolean(drawVectorStringTransformed(
      *PocketInkJSDrawTarget, svg.data, svg.size, x, y, w, h, transform));
}

PocketInkJS::Value readPocketInkJSFile(PocketInkJS::Runtime &runtime,
                             const PocketInkJS::Value *args, size_t argc) {
  if (argc < 1 || argc > 3 || !runtime.isString(args[0])) {
    return runtime.fail("file read expects path, offset?, maxBytes?");
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("file path is too long");
  }
  size_t offset = 0;
  size_t maxBytes = 4096;
  if (argc > 1) {
    const double value = runtime.toNumber(args[1]);
    if (!runtime.isNumber(args[1]) || !std::isfinite(value) || value < 0 ||
        value > UINT32_MAX || std::floor(value) != value) {
      return runtime.fail("invalid file offset");
    }
    offset = static_cast<size_t>(value);
  }
  if (argc > 2) {
    const double value = runtime.toNumber(args[2]);
    if (!runtime.isNumber(args[2]) || !std::isfinite(value) || value < 1 ||
        value > 65536 || std::floor(value) != value) {
      return runtime.fail("invalid file read size");
    }
    maxBytes = static_cast<size_t>(value);
  }
  uint8_t *buffer = static_cast<uint8_t *>(psram::allocate(maxBytes));
  if (buffer == nullptr) {
    return runtime.fail("no PSRAM for file read");
  }
  size_t bytes = 0;
  size_t fileSize = 0;
  const AppApiStatus status =
      appApiReadFile(path, offset, buffer, maxBytes, &bytes, &fileSize);
  if (status == AppApiStatus::Error) {
    psram::release(buffer);
    return runtime.fail("file read failed");
  }
  PocketInkJS::Value result = runtime.makeObject();
  PocketInkJS::Value data = runtime.makeString(
      reinterpret_cast<const char *>(buffer), bytes);
  psram::release(buffer);
  runtime.objectSet(result, "data", data);
  runtime.objectSet(result, "bytes", runtime.number(bytes));
  runtime.objectSet(result, "size", runtime.number(fileSize));
  runtime.objectSet(result, "offset", runtime.number(offset));
  runtime.objectSet(result, "eof",
                    runtime.boolean(status == kAppApiEnd));
  return result;
}

PocketInkJS::Value nativeFilesReadText(PocketInkJS::Runtime &runtime,
                                  const PocketInkJS::Value *args, size_t argc,
                                  void *) {
  return readPocketInkJSFile(runtime, args, argc);
}

PocketInkJS::Value nativeFilesReadBinary(PocketInkJS::Runtime &runtime,
                                    const PocketInkJS::Value *args, size_t argc,
                                    void *) {
  return readPocketInkJSFile(runtime, args, argc);
}

bool jsStringEquals(PocketInkJS::Runtime &runtime, PocketInkJS::Value value,
                    const char *expected) {
  if (!runtime.isString(value) || expected == nullptr) {
    return false;
  }
  const PocketInkJS::StringView text = runtime.stringView(value);
  const size_t length = strlen(expected);
  return text.size == length &&
         (length == 0 || memcmp(text.data, expected, length) == 0);
}

bool fsEncodingSupported(PocketInkJS::Runtime &runtime, PocketInkJS::Value value) {
  return runtime.isUndefined(value) || jsStringEquals(runtime, value, "utf8") ||
         jsStringEquals(runtime, value, "utf-8") ||
         jsStringEquals(runtime, value, "binary") ||
         jsStringEquals(runtime, value, "buffer");
}

PocketInkJS::Value resolvedPromise(PocketInkJS::Runtime &runtime,
                              PocketInkJS::Value result) {
  if (runtime.error().message[0] != '\0') {
    return runtime.rejectCurrentError();
  }
  PocketInkJS::Value promise = runtime.makePromise();
  if (!runtime.isUndefined(promise)) {
    runtime.settlePromise(promise, result, false);
  }
  return promise;
}

struct PocketInkJSDirectoryContext {
  PocketInkJS::Runtime *runtime = nullptr;
  PocketInkJS::Value entries = {};
  size_t count = 0;
  bool truncated = false;
};

bool appendPocketInkJSDirectoryEntry(const char *name, bool, uint32_t, void *raw) {
  PocketInkJSDirectoryContext *context =
      static_cast<PocketInkJSDirectoryContext *>(raw);
  if (context == nullptr || context->runtime == nullptr) {
    return false;
  }
  constexpr size_t kMaxDirectoryEntries = 256;
  if (context->count >= kMaxDirectoryEntries) {
    context->truncated = true;
    return false;
  }
  context->count++;
  return context->runtime->arrayPush(
      context->entries, context->runtime->makeString(name != nullptr ? name
                                                                     : ""));
}

PocketInkJS::Value nativeFsReadFileSync(PocketInkJS::Runtime &runtime,
                                   const PocketInkJS::Value *args, size_t argc,
                                   void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0]) ||
      (argc == 2 && !fsEncodingSupported(runtime, args[1]))) {
    return runtime.fail(
        "fs.readFileSync expects path and utf8, binary, or buffer encoding");
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("file path is too long");
  }
  size_t fileSize = 0;
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, &fileSize) ==
      AppApiStatus::Error) {
    return runtime.fail("file read failed");
  }
  constexpr size_t kMaxReadFileSyncBytes = 64U * 1024U;
  if (fileSize > kMaxReadFileSyncBytes) {
    return runtime.fail("fs.readFileSync is limited to 65536 bytes");
  }
  if (fileSize == 0) {
    return runtime.makeString("");
  }
  uint8_t *buffer = static_cast<uint8_t *>(psram::allocate(fileSize));
  if (buffer == nullptr) {
    return runtime.fail("no PSRAM for file read");
  }
  size_t bytes = 0;
  const AppApiStatus status =
      appApiReadFile(path, 0, buffer, fileSize, &bytes, nullptr);
  if ((status != AppApiStatus::Ok && status != kAppApiEnd) ||
      bytes != fileSize) {
    psram::release(buffer);
    return runtime.fail("file read failed");
  }
  const PocketInkJS::Value result = runtime.makeString(
      reinterpret_cast<const char *>(buffer), fileSize);
  psram::release(buffer);
  return result;
}

PocketInkJS::Value nativeFsExistsSync(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  char path[288];
  if (argc != 1 || !copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("fs.existsSync expects a file path");
  }
  return runtime.boolean(appApiReadFile(path, 0, nullptr, 0, nullptr,
                                        nullptr) != AppApiStatus::Error);
}

PocketInkJS::Value nativeFsStatSync(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc,
                               void *) {
  char path[288];
  if (argc != 1 || !copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("fs.statSync expects a file path");
  }
  size_t fileSize = 0;
  if (appApiReadFile(path, 0, nullptr, 0, nullptr, &fileSize) ==
      AppApiStatus::Error) {
    return runtime.fail("file stat failed");
  }
  PocketInkJS::Value result = runtime.makeObject();
  runtime.objectSet(result, "size", runtime.number(fileSize));
  runtime.objectSet(result, "isFile", runtime.boolean(true));
  runtime.objectSet(result, "isDirectory", runtime.boolean(false));
  return result;
}

PocketInkJS::Value nativeFsReaddirSync(PocketInkJS::Runtime &runtime,
                                  const PocketInkJS::Value *args, size_t argc,
                                  void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0]) ||
      (argc == 2 && !fsEncodingSupported(runtime, args[1]))) {
    return runtime.fail(
        "fs.readdirSync expects path and optional string encoding");
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("directory path is too long");
  }
  PocketInkJSDirectoryContext context;
  context.runtime = &runtime;
  context.entries = runtime.makeArray();
  const AppApiStatus status = appApiListDirectory(
      path, appendPocketInkJSDirectoryEntry, &context);
  if (runtime.error().message[0] != '\0') {
    return runtime.undefined();
  }
  if (status != AppApiStatus::Ok) {
    return runtime.fail("directory read failed");
  }
  if (context.truncated) {
    return runtime.fail("fs.readdirSync is limited to 256 entries");
  }
  return context.entries;
}

PocketInkJS::Value fsWriteFile(PocketInkJS::Runtime &runtime,
                          const PocketInkJS::Value *args, size_t argc,
                          bool append) {
  if (argc < 2 || argc > 3 || !runtime.isString(args[0]) ||
      !runtime.isString(args[1]) ||
      (argc == 3 && !fsEncodingSupported(runtime, args[2]))) {
    return runtime.fail(
        "fs write expects path, string data, and optional encoding");
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("file path is too long");
  }
  const PocketInkJS::StringView data = runtime.stringView(args[1]);
  return apiStatusValue(
      runtime,
      appApiWriteFile(path, reinterpret_cast<const uint8_t *>(data.data),
                      data.size, append));
}

PocketInkJS::Value nativeFsWriteFileSync(PocketInkJS::Runtime &runtime,
                                    const PocketInkJS::Value *args, size_t argc,
                                    void *) {
  return fsWriteFile(runtime, args, argc, false);
}

PocketInkJS::Value nativeFsAppendFileSync(PocketInkJS::Runtime &runtime,
                                     const PocketInkJS::Value *args, size_t argc,
                                     void *) {
  return fsWriteFile(runtime, args, argc, true);
}

PocketInkJS::Value nativeFsUnlinkSync(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  char path[288];
  return argc == 1 && copyJsString(runtime, args[0], path, sizeof(path))
             ? apiStatusValue(runtime, appApiDeleteFile(path))
             : runtime.fail("fs.unlinkSync expects a file path");
}

PocketInkJS::Value nativeFsReadFile(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc,
                               void *userData) {
  return resolvedPromise(
      runtime, nativeFsReadFileSync(runtime, args, argc, userData));
}

PocketInkJS::Value nativeFsWriteFile(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *userData) {
  return resolvedPromise(
      runtime, nativeFsWriteFileSync(runtime, args, argc, userData));
}

PocketInkJS::Value nativeFsAppendFile(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *userData) {
  return resolvedPromise(
      runtime, nativeFsAppendFileSync(runtime, args, argc, userData));
}

PocketInkJS::Value nativeFsReaddir(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc,
                              void *userData) {
  return resolvedPromise(
      runtime, nativeFsReaddirSync(runtime, args, argc, userData));
}

PocketInkJS::Value nativeFsStat(PocketInkJS::Runtime &runtime,
                           const PocketInkJS::Value *args, size_t argc,
                           void *userData) {
  return resolvedPromise(runtime,
                         nativeFsStatSync(runtime, args, argc, userData));
}

PocketInkJS::Value nativeFsUnlink(PocketInkJS::Runtime &runtime,
                             const PocketInkJS::Value *args, size_t argc,
                             void *userData) {
  return resolvedPromise(runtime,
                         nativeFsUnlinkSync(runtime, args, argc, userData));
}

PocketInkJS::Value nativeFilesWrite(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc,
                               void *) {
  if (argc < 2 || argc > 3 || !runtime.isString(args[0]) ||
      !runtime.isString(args[1])) {
    return runtime.fail("files.write expects path, data, append?");
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("file path is too long");
  }
  const PocketInkJS::StringView data = runtime.stringView(args[1]);
  return apiStatusValue(
      runtime,
      appApiWriteFile(path, reinterpret_cast<const uint8_t *>(data.data),
                      data.size, argc == 3 && runtime.toBoolean(args[2])));
}

PocketInkJS::Value nativeFilesPick(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (argc > 2 || (argc > 0 && !runtime.isString(args[0])) ||
      (argc > 1 && !runtime.isString(args[1]))) {
    return runtime.fail("files.pick expects initialPath? and filter?");
  }
  char initial[288] = "/";
  char filter[80] = {};
  if ((argc > 0 && !copyJsString(runtime, args[0], initial, sizeof(initial))) ||
      (argc > 1 && !copyJsString(runtime, args[1], filter, sizeof(filter)))) {
    return runtime.fail("file picker argument is too long");
  }
  PocketInkJS::Value promise = runtime.makePromise();
  if (runtime.isUndefined(promise)) {
    return promise;
  }
  if (!activeSystemModalIdle()) {
    runtime.settlePromise(promise, runtime.makeString("file picker is busy"),
                          true);
    return promise;
  }
  const AppApiStatus status = appApiRequestFilePicker(initial, filter);
  if (status != AppApiStatus::Pending ||
      !beginActiveSystemModal(PocketInkJSSystemModal::FilePicker, promise)) {
    runtime.settlePromise(promise, runtime.makeString("file picker is busy"),
                          true);
  }
  return promise;
}

PocketInkJS::Value nativeFilesDelete(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  char path[288];
  return argc == 1 && copyJsString(runtime, args[0], path, sizeof(path))
             ? apiStatusValue(runtime, appApiDeleteFile(path))
             : runtime.fail("files.delete expects a path");
}

PocketInkJS::Value nativeAppOpen(PocketInkJS::Runtime &runtime,
                            const PocketInkJS::Value *args, size_t argc, void *) {
  char appId[24];
  return argc == 1 && copyJsString(runtime, args[0], appId, sizeof(appId))
             ? runtime.boolean(appApiRequestOpenApp(appId))
             : runtime.fail("app.open expects an app id");
}

PocketInkJS::Value nativeGpioMode(PocketInkJS::Runtime &runtime,
                             const PocketInkJS::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 2)) {
    return runtime.undefined();
  }
  const double pin = runtime.toNumber(args[0]);
  const double mode = runtime.toNumber(args[1]);
  if (pin < 0 || pin > UINT8_MAX || std::floor(pin) != pin || mode < 0 ||
      mode > 2 || std::floor(mode) != mode)
    return runtime.fail("invalid GPIO pin or mode");
  return apiStatusValue(runtime, appApiGpioConfigure(
                                     toU8(pin),
                                     static_cast<AppApiGpioMode>(
                                         toU8(mode))));
}

PocketInkJS::Value nativeGpioRead(PocketInkJS::Runtime &runtime,
                             const PocketInkJS::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 1)) {
    return runtime.undefined();
  }
  const double pin = runtime.toNumber(args[0]);
  if (pin < 0 || pin > UINT8_MAX || std::floor(pin) != pin)
    return runtime.fail("invalid GPIO pin");
  int value = 0;
  const AppApiStatus status =
      appApiGpioRead(toU8(pin), &value);
  PocketInkJS::Value result = runtime.makeObject();
  runtime.objectSet(result, "status", apiStatusValue(runtime, status));
  runtime.objectSet(result, "value", runtime.number(value));
  return result;
}

PocketInkJS::Value nativeGpioWrite(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (argc != 2 || !runtime.isNumber(args[0])) {
    return runtime.fail("gpio.write expects pin and value");
  }
  const double pin = runtime.toNumber(args[0]);
  if (!std::isfinite(pin) || pin < 0 || pin > UINT8_MAX ||
      std::floor(pin) != pin)
    return runtime.fail("invalid GPIO pin");
  return apiStatusValue(
      runtime, appApiGpioWrite(toU8(pin),
                               runtime.toBoolean(args[1])));
}

PocketInkJS::Value nativeSensorsRead(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (argc > 1) {
    return runtime.fail("sensors.read expects refresh?");
  }
  const EnvironmentSnapshot snapshot =
      appApiSensorSnapshot(argc == 1 && runtime.toBoolean(args[0]));
  PocketInkJS::Value result = runtime.makeObject();
  runtime.objectSet(result, "temperatureValid",
                    runtime.boolean(snapshot.ambientTemperatureValid));
  runtime.objectSet(result, "humidityValid",
                    runtime.boolean(snapshot.ambientHumidityValid));
  runtime.objectSet(result, "cpuTemperatureValid",
                    runtime.boolean(snapshot.chipTemperatureValid));
  runtime.objectSet(result, "temperatureC",
                    runtime.number(snapshot.ambientTemperatureC));
  runtime.objectSet(result, "humidityPct",
                    runtime.number(snapshot.ambientHumidityPct));
  runtime.objectSet(result, "cpuTemperatureC",
                    runtime.number(snapshot.chipTemperatureC));
  return result;
}

PocketInkJS::Value nativeNetworkScan(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *, size_t argc, void *) {
  return argc == 0 ? apiStatusValue(runtime, appApiWifiScan())
                   : runtime.fail("network.scan expects no arguments");
}

PocketInkJS::Value nativeNetworkNetworks(PocketInkJS::Runtime &runtime,
                                    const PocketInkJS::Value *, size_t argc,
                                    void *) {
  if (argc != 0) {
    return runtime.fail("network.networks expects no arguments");
  }
  PocketInkJS::Value result = runtime.makeArray(appApiWifiNetworkCount());
  for (size_t i = 0; i < appApiWifiNetworkCount(); i++) {
    AppApiWifiNetwork network;
    if (!appApiWifiNetworkAt(i, network)) {
      continue;
    }
    PocketInkJS::Value item = runtime.makeObject();
    runtime.objectSet(item, "ssid", runtime.makeString(network.ssid));
    runtime.objectSet(item, "rssi", runtime.number(network.rssi));
    runtime.arrayPush(result, item);
  }
  return result;
}

PocketInkJS::Value nativeUiFonts(PocketInkJS::Runtime &runtime, const PocketInkJS::Value *,
                            size_t argc, void *) {
  if (argc != 0) {
    return runtime.fail("ui.fonts expects no arguments");
  }
  PocketInkJS::Value result = runtime.makeArray(systemFontCount());
  for (size_t i = 0; i < systemFontCount(); i++) {
    SystemFontInfo info;
    if (!systemFontInfo(i, info)) {
      continue;
    }
    PocketInkJS::Value item = runtime.makeObject();
    runtime.objectSet(item, "name", runtime.makeString(info.name));
    runtime.objectSet(item, "size", runtime.number(info.nominalSize));
    runtime.arrayPush(result, item);
  }
  return result;
}

PocketInkJS::Value nativeAudioCapture(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 bool wav) {
  if (!numericArguments(runtime, args, argc, 1)) {
    return runtime.undefined();
  }
  const double duration = runtime.toNumber(args[0]);
  if (duration < 1 || duration > 60000 || std::floor(duration) != duration) {
    return runtime.fail("invalid capture duration");
  }
  PocketInkJS::Value promise = runtime.makePromise();
  if (runtime.isUndefined(promise)) {
    return promise;
  }
  if (!activeSystemModalIdle()) {
    runtime.settlePromise(promise, runtime.makeString("audio capture is busy"),
                          true);
    return promise;
  }
  const uint32_t durationMs = static_cast<uint32_t>(duration);
  const uint8_t *data = nullptr;
  size_t size = 0;
  const AppApiStatus status = wav ? appApiCaptureWav(durationMs, &data, &size)
                                  : appApiCapturePcm16(durationMs, &data, &size);
  if (status != AppApiStatus::Pending ||
      !beginActiveSystemModal(wav ? PocketInkJSSystemModal::AudioCapture
                                  : PocketInkJSSystemModal::AudioCapturePcm16,
                              promise, durationMs)) {
    runtime.settlePromise(promise, runtime.makeString("audio capture is busy"),
                          true);
  }
  return promise;
}

PocketInkJS::Value nativeAudioCaptureWav(PocketInkJS::Runtime &runtime,
                                    const PocketInkJS::Value *args, size_t argc,
                                    void *) {
  return nativeAudioCapture(runtime, args, argc, true);
}

PocketInkJS::Value nativeAudioCapturePcm16(PocketInkJS::Runtime &runtime,
                                      const PocketInkJS::Value *args, size_t argc,
                                      void *) {
  return nativeAudioCapture(runtime, args, argc, false);
}

PocketInkJS::Value nativeAudioLoad(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  char path[288];
  return argc == 1 && copyJsString(runtime, args[0], path, sizeof(path))
             ? runtime.number(appApiAudioLoadPath(path))
             : runtime.fail("audio.load expects a path");
}

PocketInkJS::Value nativeAudioSupports(PocketInkJS::Runtime &runtime,
                                  const PocketInkJS::Value *args, size_t argc,
                                  void *) {
  char format[24];
  return argc == 1 && copyJsString(runtime, args[0], format, sizeof(format))
             ? runtime.boolean(appApiAudioFormatSupported(format))
             : runtime.fail("audio.supports expects a format");
}

PocketInkJS::Value nativeAudioLoadBuffer(PocketInkJS::Runtime &runtime,
                                    const PocketInkJS::Value *args, size_t argc,
                                    void *) {
  if (argc != 2 || !runtime.isString(args[0]) || !runtime.isString(args[1])) {
    return runtime.fail("audio.loadBuffer expects data and format");
  }
  char format[24];
  if (!copyJsString(runtime, args[1], format, sizeof(format))) {
    return runtime.fail("audio format is too long");
  }
  const PocketInkJS::StringView data = runtime.stringView(args[0]);
  return runtime.number(appApiAudioLoadBuffer(
      reinterpret_cast<const uint8_t *>(data.data), data.size, format));
}

PocketInkJS::Value nativeAudioPlay(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  const double handle = runtime.toNumber(args[0]);
  return handle >= 1 && handle <= UINT32_MAX && std::floor(handle) == handle
             ? apiStatusValue(runtime,
                              appApiAudioPlay(static_cast<uint32_t>(handle)))
             : runtime.fail("invalid audio handle");
}

PocketInkJS::Value nativeAudioStop(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  const double handle = runtime.toNumber(args[0]);
  return handle >= 1 && handle <= UINT32_MAX && std::floor(handle) == handle
             ? apiStatusValue(runtime,
                              appApiAudioStop(static_cast<uint32_t>(handle)))
             : runtime.fail("invalid audio handle");
}

PocketInkJS::Value nativeAudioUpdate(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (!numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  const double handle = runtime.toNumber(args[0]);
  return handle >= 1 && handle <= UINT32_MAX && std::floor(handle) == handle
             ? apiStatusValue(runtime,
                              appApiAudioUpdate(static_cast<uint32_t>(handle)))
             : runtime.fail("invalid audio handle");
}

PocketInkJS::Value nativeAudioRelease(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  if (!numericArguments(runtime, args, argc, 1)) {
    return runtime.undefined();
  }
  const double handle = runtime.toNumber(args[0]);
  if (handle < 1 || handle > UINT32_MAX || std::floor(handle) != handle)
    return runtime.fail("invalid audio handle");
  appApiAudioRelease(static_cast<uint32_t>(handle));
  return runtime.undefined();
}

PocketInkJS::Value nativeUiQr(PocketInkJS::Runtime &runtime, const PocketInkJS::Value *args,
                         size_t argc, void *) {
  if (argc < 4 || argc > 5 || !runtime.isString(args[0])) {
    return runtime.fail("ui.qr expects text, x, y, size, color?");
  }
  for (size_t i = 1; i < argc; i++) {
    if (!runtime.isNumber(args[i]) ||
        !std::isfinite(runtime.toNumber(args[i]))) {
      return runtime.fail("invalid QR position or size");
    }
  }
  if (PocketInkJSDrawTarget == nullptr) {
    return runtime.boolean(false);
  }
  char text[SystemQrCode::maxPayload + 1];
  if (!copyJsString(runtime, args[0], text, sizeof(text))) {
    return runtime.boolean(false);
  }
  SystemQrCode qr;
  if (!qr.encode(text)) {
    return runtime.boolean(false);
  }
  qr.draw(*PocketInkJSDrawTarget, toI16(runtime.toNumber(args[1])),
          toI16(runtime.toNumber(args[2])),
          toI16(runtime.toNumber(args[3])),
          argc == 5 ? toU16(runtime.toNumber(args[4])) : 1);
  return runtime.boolean(true);
}

int64_t PocketInkJSNowMilliseconds(void *) {
  int64_t seconds = 0;
  uint16_t milliseconds = 0;
  if (deviceClock.snapshotUnixMillis(seconds, milliseconds)) {
    return seconds * INT64_C(1000) + milliseconds;
  }
  return static_cast<int64_t>(millis());
}

#if POCKETINKJS_USE_LIGHTWEIGHT_JSON_PARSER
class PocketInkJSJsonStream : public Stream {
public:
  explicit PocketInkJSJsonStream(PocketInkJS::StringView input) : input(input) {}

  int available() override {
    return position < input.size
               ? static_cast<int>(input.size - position > INT_MAX
                                      ? INT_MAX
                                      : input.size - position)
               : 0;
  }
  int read() override {
    return position < input.size
               ? static_cast<unsigned char>(input.data[position++])
               : -1;
  }
  int peek() override {
    return position < input.size
               ? static_cast<unsigned char>(input.data[position])
               : -1;
  }
  size_t write(uint8_t) override { return 0; }

private:
  PocketInkJS::StringView input;
  size_t position = 0;
};

class PocketInkJSJsonListener : public JsonStreamListener {
public:
  explicit PocketInkJSJsonListener(PocketInkJS::Runtime &runtime) : runtime(runtime) {}

  PocketInkJS::Value result() const { return root; }
  bool valid() const { return hasRoot; }

  void onObjectStart(int depth, const char *key) override {
    PocketInkJS::Value object = runtime.makeObject();
    attachContainer(depth, key, object);
  }
  void onArrayStart(int depth, const char *key) override {
    PocketInkJS::Value array = runtime.makeArray();
    attachContainer(depth, key, array);
  }
  void onStringValue(int depth, const char *key, const char *value) override {
    attachScalar(depth, key, runtime.makeString(value));
  }
  void onNumericValue(int depth, const char *key, double value) override {
    attachScalar(depth, key, runtime.number(value));
  }
  void onBooleanValue(int depth, const char *key, bool value) override {
    attachScalar(depth, key, runtime.boolean(value));
  }
  void onNullValue(int depth, const char *key) override {
    attachScalar(depth, key, runtime.null());
  }

private:
  PocketInkJS::Runtime &runtime;
  PocketInkJS::Value root = {};
  PocketInkJS::Value containers[20] = {};
  bool hasRoot = false;

  void append(PocketInkJS::Value parent, const char *key, PocketInkJS::Value value) {
    if (runtime.typeOf(parent) == PocketInkJS::ValueType::Array)
      runtime.arrayPush(parent, value);
    else
      runtime.objectSet(parent, key != nullptr ? key : "", value);
  }
  void attachContainer(int depth, const char *key, PocketInkJS::Value value) {
    if (depth <= 0 || depth > static_cast<int>(sizeof(containers) /
                                               sizeof(containers[0]))) {
      runtime.fail(PocketInkJS::ErrorCode{164});
      return;
    }
    if (depth == 1) {
      root = value;
      hasRoot = true;
    } else
      append(containers[depth - 2], key, value);
    containers[depth - 1] = value;
  }
  void attachScalar(int depth, const char *key, PocketInkJS::Value value) {
    if (depth == 0) {
      root = value;
      hasRoot = true;
      return;
    }
    if (depth < 0 || depth > static_cast<int>(sizeof(containers) /
                                              sizeof(containers[0]))) {
      runtime.fail(PocketInkJS::ErrorCode{164});
      return;
    }
    append(containers[depth - 1], key, value);
  }
};

PocketInkJS::Value parsePocketInkJSJson(PocketInkJS::Runtime &runtime, PocketInkJS::StringView json,
                              void *) {
  if (json.size > INT_MAX - 1)
    return runtime.fail(PocketInkJS::ErrorCode{165});
  PocketInkJSJsonStream stream(json);
  PocketInkJSJsonListener listener(runtime);
  LightweightJsonParser parser;
  char error[64] = {};
  if (!parser.parse(&stream, static_cast<int>(json.size), 1000,
                    static_cast<int>(json.size + 1U), listener, error,
                    sizeof(error))) {
    if (error[0] != '\0')
      return runtime.fail(error);
    return runtime.fail(PocketInkJS::ErrorCode{221});
  }
  if (!listener.valid())
    return runtime.fail(PocketInkJS::ErrorCode{166});
  return listener.result();
}
#endif

class PocketInkJSApp : public ActiveApp {
public:
  void launch(const char *nextPath, const char *nextId) {
    unload();
    PocketInkJSLowestStackHwm = UINT_MAX;
    copyText(appId, sizeof(appId), nextId);
    activePocketInkJSApp = this;
    if (!appApiBeginSession(appId)) {
      copyText(status, sizeof(status), "NO API PSRAM");
      return;
    }
    const size_t pathLength = nextPath != nullptr ? strlen(nextPath) : 0;
    path = static_cast<char *>(allocatePsram(pathLength + 1U));
    if (path == nullptr) {
      copyText(status, sizeof(status), "NO PATH PSRAM");
      return;
    }
    memcpy(path, nextPath, pathLength + 1U);
    if (!startExecutor()) {
      copyText(status, sizeof(status), "NO EXECUTOR DRAM");
      return;
    }
    if (!execute(loadOperation, nullptr)) {
      copyText(status, sizeof(status), "EXECUTOR FAILED");
      return;
    }
    if (runtime != nullptr) {
      pendingStart = true;
    }
  }

  void stop() { unload(); }
  void pause() { paused = true; }
  void resume() { paused = false; }

  const char *id() const { return appId; }

  PocketInkJS::Value queueFetch(PocketInkJS::Runtime &vm, const PocketInkJS::Value *args,
                           size_t argc);
  PocketInkJS::Value scheduleTimer(PocketInkJS::Runtime &vm,
                              const PocketInkJS::Value *args, size_t argc,
                              bool repeating);
  PocketInkJS::Value cancelTimer(PocketInkJS::Runtime &vm, const PocketInkJS::Value *args,
                            size_t argc);
  bool beginSystemModal(PocketInkJSSystemModal kind, PocketInkJS::Value promise,
                        uint32_t captureDurationMs = 0,
                        DatePickerDate initialDate = {});
  bool systemModalIdle() const {
    return systemModal == PocketInkJSSystemModal::None;
  }
  bool waitForPromise(PocketInkJS::Runtime &vm, PocketInkJS::Value promise);

  void draw(Adafruit_GFX &gfx) override {
    if (pendingStart) {
      gfx.setTextColor(1);
      drawPocketInkJSCentered(gfx, "PocketInkJS", 70, 2);
      drawPocketInkJSCentered(gfx, "STARTING", 102, 1);
      return;
    }
    if (status[0] != '\0') {
      gfx.setTextColor(1);
      drawPocketInkJSCentered(gfx, "PocketInkJS", 54, 2);
      drawPocketInkJSWrappedCentered(gfx, status, 86);
      return;
    }
    DrawContext context{&gfx};
    execute(drawOperation, &context);
  }

  bool handleTouch(const TouchPoint &point) override {
    TouchContext context{point, false};
    return execute(touchOperation, &context) && context.dirty;
  }

  bool update() override {
    if (paused)
      return false;
    UpdateContext context;
    return execute(updateOperation, &context) && context.dirty;
  }

  bool hasActiveSession() const override { return false; }

  AppEventResult button(const char *callback) {
    ButtonContext context{callback, AppEventResult::Unhandled};
    return execute(buttonOperation, &context) ? context.result
                                              : AppEventResult::Unhandled;
  }

  void rawTouch(const TouchEvent &event) {
    const uint8_t callback = touchCallbackFor(event.type);
    if ((touchCallbacks & (callback | PocketInkJSTouchGeneric)) == 0)
      return;
    RawTouchContext context{event};
    execute(rawTouchOperation, &context);
  }

  size_t save(uint8_t *buffer, size_t capacity) {
    SaveContext context{buffer, capacity, 0};
    return execute(saveOperation, &context) ? context.length : 0;
  }

  void restore(const uint8_t *buffer, size_t length) {
    RestoreContext context{buffer, length};
    execute(restoreOperation, &context);
  }

private:
  struct DrawContext {
    Adafruit_GFX *gfx = nullptr;
  };
  struct TouchContext {
    TouchPoint point = {};
    bool dirty = false;
  };
  struct UpdateContext {
    bool dirty = false;
  };
  struct ButtonContext {
    const char *callback = nullptr;
    AppEventResult result = AppEventResult::Unhandled;
  };
  struct RawTouchContext {
    TouchEvent event = {};
  };
  struct SaveContext {
    uint8_t *buffer = nullptr;
    size_t capacity = 0;
    size_t length = 0;
  };
  struct RestoreContext {
    const uint8_t *buffer = nullptr;
    size_t length = 0;
  };

  char *path = nullptr;
  char appId[kIdCapacity] = {};
  char status[kStatusCapacity] = {};
  void *runtimeStorage = nullptr;
  void *workspaceAllocation = nullptr;
  size_t workspaceBytes = 0;
  PocketInkJS::Runtime *runtime = nullptr;
  AsyncState *asyncState = nullptr;
  TimerState *timerState = nullptr;
  PocketInkJS::RootHandle systemModalPromise = PocketInkJS::kInvalidRoot;
  DatePickerDate systemModalDate = {};
  uint32_t systemModalCaptureDurationMs = 0;
  PocketInkJSSystemModal systemModal = PocketInkJSSystemModal::None;
  QueueHandle_t executorCommands = nullptr;
  QueueHandle_t executorCompletions = nullptr;
  TaskHandle_t executorTask = nullptr;
  bool pendingDirty = false;
  bool pendingStart = false;
  bool started = false;
  bool paused = false;
  bool hasErrorCatalog = false;
  uint8_t touchCallbacks = 0;

  static uint8_t touchCallbackFor(TouchEventType type) {
    switch (type) {
    case TOUCH_EVENT_DOWN:
      return PocketInkJSTouchDown;
    case TOUCH_EVENT_MOVE:
      return PocketInkJSTouchMove;
    case TOUCH_EVENT_UP:
      return PocketInkJSTouchUp;
    }
    return 0;
  }

  static void executorTaskMain(void *context) {
    PocketInkJSApp *app = static_cast<PocketInkJSApp *>(context);
    if (app != nullptr)
      app->executorLoop();
    vTaskDelete(nullptr);
  }

  void executorLoop() {
    while (true) {
      PocketInkJSExecutorCommand *command = nullptr;
      if (xQueueReceive(executorCommands, &command, portMAX_DELAY) != pdPASS ||
          command == nullptr) {
        continue;
      }
      if (command->operation != nullptr)
        command->operation(*this, command->context);
      xQueueSend(executorCompletions, &command, portMAX_DELAY);
      if (command->operation == nullptr)
        return;
    }
  }

  bool startExecutor() {
    if (executorTask != nullptr)
      return true;
    executorCommands = xQueueCreate(1, sizeof(PocketInkJSExecutorCommand *));
    executorCompletions = xQueueCreate(1, sizeof(PocketInkJSExecutorCommand *));
    if (executorCommands == nullptr || executorCompletions == nullptr) {
      stopExecutor();
      return false;
    }
    if (xTaskCreatePinnedToCore(executorTaskMain, "PocketInkJS",
                                kPocketInkJSExecutorStackBytes, this,
                                kPocketInkJSExecutorPriority, &executorTask,
                                kPocketInkJSExecutorCore) != pdPASS) {
      executorTask = nullptr;
      stopExecutor();
      return false;
    }
    return true;
  }

  bool execute(PocketInkJSExecutorOperation operation, void *context) {
    if (executorTask == nullptr || executorCommands == nullptr ||
        executorCompletions == nullptr) {
      return false;
    }
    PocketInkJSExecutorCommand command{operation, context};
    PocketInkJSExecutorCommand *sent = &command;
    if (xQueueSend(executorCommands, &sent, portMAX_DELAY) != pdPASS)
      return false;
    PocketInkJSExecutorCommand *completed = nullptr;
    return xQueueReceive(executorCompletions, &completed, portMAX_DELAY) ==
               pdPASS &&
           completed == &command;
  }

  void stopExecutor() {
    if (executorTask != nullptr && executorCommands != nullptr &&
        executorCompletions != nullptr) {
      PocketInkJSExecutorCommand command;
      PocketInkJSExecutorCommand *sent = &command;
      if (xQueueSend(executorCommands, &sent, portMAX_DELAY) == pdPASS) {
        PocketInkJSExecutorCommand *completed = nullptr;
        xQueueReceive(executorCompletions, &completed, portMAX_DELAY);
      }
      executorTask = nullptr;
    }
    if (executorCommands != nullptr) {
      vQueueDelete(executorCommands);
      executorCommands = nullptr;
    }
    if (executorCompletions != nullptr) {
      vQueueDelete(executorCompletions);
      executorCompletions = nullptr;
    }
  }

  static void loadOperation(PocketInkJSApp &app, void *) { app.load(); }
  static void unloadOperation(PocketInkJSApp &app, void *) { app.unloadRuntime(); }
  static void drawOperation(PocketInkJSApp &app, void *raw) {
    DrawContext *context = static_cast<DrawContext *>(raw);
    if (context == nullptr || context->gfx == nullptr)
      return;
    PocketInkJSDrawTarget = context->gfx;
    app.run("onDraw", nullptr, 0, nullptr);
    PocketInkJSDrawTarget = nullptr;
  }
  static void touchOperation(PocketInkJSApp &app, void *raw) {
    TouchContext *context = static_cast<TouchContext *>(raw);
    if (context != nullptr) {
      context->dirty =
          app.dispatchTouch("onTouch", "touch", context->point);
    }
  }
  static void updateOperation(PocketInkJSApp &app, void *raw) {
    UpdateContext *context = static_cast<UpdateContext *>(raw);
    if (context != nullptr)
      context->dirty = app.updateRuntime();
  }
  static void buttonOperation(PocketInkJSApp &app, void *raw) {
    ButtonContext *context = static_cast<ButtonContext *>(raw);
    if (context != nullptr)
      context->result = app.buttonRuntime(context->callback);
  }
  static void rawTouchOperation(PocketInkJSApp &app, void *raw) {
    RawTouchContext *context = static_cast<RawTouchContext *>(raw);
    if (context != nullptr)
      app.rawTouchRuntime(context->event);
  }
  static void saveOperation(PocketInkJSApp &app, void *raw) {
    SaveContext *context = static_cast<SaveContext *>(raw);
    if (context != nullptr)
      context->length = app.saveRuntime(context->buffer, context->capacity);
  }
  static void restoreOperation(PocketInkJSApp &app, void *raw) {
    RestoreContext *context = static_cast<RestoreContext *>(raw);
    if (context != nullptr)
      app.restoreRuntime(context->buffer, context->length);
  }

  bool updateRuntime() {
    bool dirty = pendingDirty;
    pendingDirty = false;
    if (pendingStart) {
      pendingStart = false;
      started = true;
      run("onStart", nullptr, 0, nullptr);
      refreshTouchCallbacks();
      dirty = true;
    }
    dirty = pumpAsyncCompletions() || dirty;
    dirty = pumpTimers() || dirty;
    dirty = pumpSystemModal() || dirty;
    // Promise reactions are microtasks: run them immediately after host
    // completions and before entering the next app callback. In particular,
    // picker results must reach their .then(...) handler before onUpdate can
    // trigger another collection/callback boundary.
    if (runtime != nullptr && status[0] == '\0') {
      const PocketInkJS::RunResult jobs = runtime->drainJobs(kPocketInkJSJobsPerUpdate);
      if (!jobs.ok)
        setRuntimeError();
    }
    PocketInkJS::RunResult result;
    bool called = false;
    if (run("onUpdate", nullptr, 0, &result, &called) && called) {
      dirty = dirty || runtime->isTruthy(result.value);
    }
    return dirty;
  }

  AppEventResult buttonRuntime(const char *callback) {
    PocketInkJS::RunResult result;
    bool called = false;
    if (!run(callback, nullptr, 0, &result, &called)) {
      return AppEventResult::Unhandled;
    }
    if (!called) {
      PocketInkJS::Value event = runtime->makeObject();
      runtime->objectSet(event, "type", runtime->makeString(
                                            callback != nullptr ? callback
                                                                : "button"));
      if (!run("onButton", &event, 1, &result, &called) || !called) {
        return AppEventResult::Unhandled;
      }
    }
    return runtime->isTruthy(result.value) ? AppEventResult::Dirty
                                           : AppEventResult::Handled;
  }

  void rawTouchRuntime(const TouchEvent &event) {
    const char *specific = "onTouch";
    const char *type = "touch";
    uint8_t callback = PocketInkJSTouchGeneric;
    switch (event.type) {
    case TOUCH_EVENT_DOWN:
      specific = "onTouchDown";
      type = "down";
      callback = PocketInkJSTouchDown;
      break;
    case TOUCH_EVENT_MOVE:
      specific = "onTouchMove";
      type = "move";
      callback = PocketInkJSTouchMove;
      break;
    case TOUCH_EVENT_UP:
      specific = "onTouchUp";
      type = "up";
      callback = PocketInkJSTouchUp;
      break;
    }
    const char *handler = (touchCallbacks & callback) != 0 ? specific
                                                           : "onTouch";
    pendingDirty = pendingDirty || dispatchTouch(handler, type, event.point);
  }

  size_t saveRuntime(uint8_t *buffer, size_t capacity) {
    PocketInkJS::RunResult result;
    bool called = false;
    if (buffer == nullptr || capacity == 0 ||
        !run("onSave", nullptr, 0, &result, &called) || !called ||
        !runtime->isString(result.value)) {
      return 0;
    }
    const PocketInkJS::StringView data = runtime->stringView(result.value);
    const size_t amount = data.size < capacity ? data.size : capacity;
    memcpy(buffer, data.data, amount);
    return amount;
  }

  void restoreRuntime(const uint8_t *buffer, size_t length) {
    if (runtime == nullptr || buffer == nullptr) {
      return;
    }
    PocketInkJS::Value state =
        runtime->makeString(reinterpret_cast<const char *>(buffer), length);
    PocketInkJS::RunResult result;
    bool called = false;
    if (run("onRestore", &state, 1, &result, &called) && called) {
      pendingDirty = pendingDirty || runtime->isTruthy(result.value);
    }
  }

  bool readErrorDescription(uint8_t errorNumber, char *out,
                            size_t outSize) const {
    if (!hasErrorCatalog || out == nullptr || outSize == 0)
      return false;
    out[0] = '\0';
    File file = openProviderPath("sd", "/misc/js_errors.txt");
    if (!file || file.isDirectory())
      return false;
    uint16_t line = 1;
    size_t length = 0;
    while (file.available() > 0) {
      const int next = file.read();
      if (next < 0)
        break;
      if (next == '\n') {
        if (line == errorNumber)
          break;
        line++;
        continue;
      }
      if (line != errorNumber || next == '\r')
        continue;
      if (length + 1U < outSize) {
        out[length++] = next >= 0x20 && next <= 0x7e
                            ? static_cast<char>(next)
                            : '?';
      }
    }
    out[length] = '\0';
    return line == errorNumber && length > 0;
  }

  void setRuntimeError() {
    if (runtime == nullptr) {
      return;
    }
    const PocketInkJS::Error &error = runtime->error();
    char described[72];
    const char *message = error.message;
    uint8_t errorNumber = 0;
    char description[60];
    if (PocketInkJSErrorNumber(error.message, &errorNumber) &&
        readErrorDescription(errorNumber, description, sizeof(description))) {
      snprintf(described, sizeof(described), "%.4s %.62s", error.message,
               description);
      message = described;
    }
    if (error.line > 0) {
      snprintf(status, sizeof(status), "L%lu:%lu %.78s",
               static_cast<unsigned long>(error.line),
               static_cast<unsigned long>(error.column), message);
    } else {
      copyText(status, sizeof(status), message);
    }
    char caret[sizeof(error.sourceLine)] = {};
    if (error.sourceLine[0] != '\0') {
      const uint16_t caretAt =
          error.sourceColumn < sizeof(caret) - 1U
              ? error.sourceColumn
              : static_cast<uint16_t>(sizeof(caret) - 2U);
      for (uint16_t i = 0; i < caretAt; i++) {
        caret[i] = error.sourceLine[i] == '\t' ? '\t' : ' ';
      }
      caret[caretAt] = '^';
    }
    const PocketInkJS::MemoryStats stats = runtime->memoryStats();
    if (error.sourceLine[0] != '\0') {
      PocketInkJSLogPrintf(
          "[PocketInkJS] %s\n[PocketInkJS] > %s\n[PocketInkJS]   %s\n"
          "[PocketInkJS] error heap=%lu free=%lu peak=%lu workspace=%lu\n",
          status, error.sourceLine, caret,
          static_cast<unsigned long>(stats.heapUsedBytes),
          static_cast<unsigned long>(stats.heapFreeBytes),
          static_cast<unsigned long>(stats.peakHeapUsedBytes),
          static_cast<unsigned long>(stats.workspaceBytes));
    } else {
      PocketInkJSLogPrintf(
          "[PocketInkJS] %s\n"
          "[PocketInkJS] error heap=%lu free=%lu peak=%lu workspace=%lu\n",
          status, static_cast<unsigned long>(stats.heapUsedBytes),
          static_cast<unsigned long>(stats.heapFreeBytes),
          static_cast<unsigned long>(stats.peakHeapUsedBytes),
          static_cast<unsigned long>(stats.workspaceBytes));
    }
    pendingDirty = true;
  }

  bool run(const char *callback, const PocketInkJS::Value *args, size_t argc,
           PocketInkJS::RunResult *out, bool *calledOut = nullptr) {
    if (calledOut != nullptr) {
      *calledOut = false;
    }
    if (runtime == nullptr || status[0] != '\0') {
      return false;
    }
    logPocketInkJSStackLow(callback);
    bool called = false;
    const PocketInkJS::RunResult result =
        runtime->callIfFunction(callback, args, argc, &called);
    logPocketInkJSStackLow(callback);
    if (calledOut != nullptr) {
      *calledOut = called;
    }
    if (!result.ok) {
      setRuntimeError();
      return false;
    }
    if (out != nullptr) {
      *out = result;
    }
    return true;
  }

  bool dispatchTouch(const char *callback, const char *type,
                     const TouchPoint &point, bool *calledOut = nullptr) {
    if (runtime == nullptr || status[0] != '\0') {
      return false;
    }
    PocketInkJS::Value event = runtime->makeObject();
    runtime->objectSet(event, "type", runtime->makeString(type));
    runtime->objectSet(event, "x", runtime->number(point.x));
    runtime->objectSet(event, "y", runtime->number(point.y));
    PocketInkJS::RunResult result;
    bool called = false;
    if (!run(callback, &event, 1, &result, &called)) {
      return false;
    }
    if (calledOut != nullptr) {
      *calledOut = called;
    }
    return called && runtime->isTruthy(result.value);
  }

  bool installBindings();
  void refreshTouchCallbacks() {
    touchCallbacks = 0;
    if (runtime == nullptr)
      return;
    if (runtime->isCallable(runtime->get("onTouch")))
      touchCallbacks |= PocketInkJSTouchGeneric;
    if (runtime->isCallable(runtime->get("onTouchDown")))
      touchCallbacks |= PocketInkJSTouchDown;
    if (runtime->isCallable(runtime->get("onTouchMove")))
      touchCallbacks |= PocketInkJSTouchMove;
    if (runtime->isCallable(runtime->get("onTouchUp")))
      touchCallbacks |= PocketInkJSTouchUp;
  }
  void load();
  bool ensureAsyncWorker();
  bool pumpAsyncCompletions();
  bool pumpTimers();
  bool pumpSystemModal();
  void stopAsyncWorker();
  void releaseTimers();

  void releaseRuntime() {
    if (runtime != nullptr) {
      runtime->~Runtime();
      runtime = nullptr;
    }
    if (runtimeStorage != nullptr) {
      heap_caps_free(runtimeStorage);
      runtimeStorage = nullptr;
    }
    if (workspaceAllocation != nullptr) {
      heap_caps_free(workspaceAllocation);
      workspaceAllocation = nullptr;
    }
    workspaceBytes = 0;
  }

  void unloadRuntime() {
    if (started && runtime != nullptr && status[0] == '\0') {
      run("onStop", nullptr, 0, nullptr);
    }
    pendingStart = false;
    started = false;
    paused = false;
    hasErrorCatalog = false;
    touchCallbacks = 0;
    releaseTimers();
    if (runtime != nullptr && systemModalPromise != PocketInkJS::kInvalidRoot) {
      runtime->release(systemModalPromise);
    }
    systemModal = PocketInkJSSystemModal::None;
    systemModalPromise = PocketInkJS::kInvalidRoot;
    stopAsyncWorker();
    releaseSleepGuards();
    if (appApiSessionMatches(appId)) {
      appApiEndSession();
    }
    releaseRuntime();
    if (PocketInkJSLogBuffer != nullptr) {
      heap_caps_free(PocketInkJSLogBuffer);
      PocketInkJSLogBuffer = nullptr;
    }
    status[0] = '\0';
    pendingDirty = false;
    if (path != nullptr) {
      heap_caps_free(path);
      path = nullptr;
    }
    appId[0] = '\0';
    if (activePocketInkJSApp == this) {
      activePocketInkJSApp = nullptr;
    }
  }

  void unload() {
    if (executorTask != nullptr) {
      execute(unloadOperation, nullptr);
      stopExecutor();
      return;
    }
    unloadRuntime();
  }
};

bool activeSystemModalIdle() {
  return activePocketInkJSApp != nullptr && activePocketInkJSApp->systemModalIdle();
}

bool beginActiveSystemModal(PocketInkJSSystemModal kind, PocketInkJS::Value promise,
                            uint32_t captureDurationMs,
                            DatePickerDate initialDate) {
  return activePocketInkJSApp != nullptr &&
         activePocketInkJSApp->beginSystemModal(kind, promise, captureDurationMs,
                                           initialDate);
}

bool PocketInkJSApp::ensureAsyncWorker() {
  if (asyncState != nullptr)
    return true;
  void *memory = allocatePsram(sizeof(AsyncState));
  if (memory == nullptr)
    return false;
  AsyncState *created = new (memory) AsyncState();
  created->requests =
      xQueueCreate(kPocketInkJSFetchQueueDepth, sizeof(FetchRequest *));
  created->completions =
      xQueueCreate(kPocketInkJSFetchQueueDepth, sizeof(FetchCompletion *));
  if (created->requests == nullptr || created->completions == nullptr) {
    if (created->requests != nullptr)
      vQueueDelete(created->requests);
    if (created->completions != nullptr)
      vQueueDelete(created->completions);
    created->~AsyncState();
    heap_caps_free(created);
    return false;
  }
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(PocketInkJSWorker, "PocketInkJS-fetch",
                              kPocketInkJSWorkerStackBytes, created,
                              kPocketInkJSWorkerPriority, &task,
                              kPocketInkJSWorkerCore) != pdPASS) {
    vQueueDelete(created->requests);
    vQueueDelete(created->completions);
    created->~AsyncState();
    heap_caps_free(created);
    return false;
  }
  created->task = task;
  asyncState = created;
  return true;
}

PocketInkJS::Value PocketInkJSApp::queueFetch(PocketInkJS::Runtime &vm,
                                    const PocketInkJS::Value *args, size_t argc) {
  if (argc < 1 || argc > 2 || !vm.isString(args[0]))
    return vm.fail(PocketInkJS::ErrorCode{167});
  const PocketInkJS::StringView url = vm.stringView(args[0]);
  if (url.size == 0 ||
      url.size >= sizeof(static_cast<FetchRequest *>(nullptr)->url))
    return vm.fail(PocketInkJS::ErrorCode{168});
  if (argc == 2 && vm.typeOf(args[1]) != PocketInkJS::ValueType::Object)
    return vm.fail(PocketInkJS::ErrorCode{169});

  FetchRequest *request =
      static_cast<FetchRequest *>(allocatePsram(sizeof(FetchRequest)));
  if (request == nullptr) {
    PocketInkJS::Value promise = vm.makePromise();
    if (vm.isUndefined(promise))
      return promise;
    vm.settlePromise(promise, vm.makeString("no PSRAM for fetch request"), true);
    return promise;
  }
  new (request) FetchRequest();
  memcpy(request->url, url.data, url.size);
  request->url[url.size] = '\0';

  const PocketInkJS::Value options = argc == 2 ? args[1] : vm.undefined();
  if (!vm.isUndefined(options)) {
    const PocketInkJS::Value method = vm.objectGet(options, "method");
    if (!vm.isUndefined(method)) {
      if (!vm.isString(method)) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{170});
      }
      const PocketInkJS::StringView text = vm.stringView(method);
      if (text.size == 0 || text.size >= sizeof(request->method)) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{171});
      }
      for (size_t i = 0; i < text.size; i++) {
        const char c = text.data[i];
        request->method[i] = c >= 'a' && c <= 'z'
                                 ? static_cast<char>(c - 'a' + 'A')
                                 : c;
      }
      request->method[text.size] = '\0';
    }
    const PocketInkJS::Value maxBytes = vm.objectGet(options, "maxBytes");
    if (!vm.isUndefined(maxBytes)) {
      const double raw = vm.toNumber(maxBytes);
      if (!std::isfinite(raw) || raw < 1 || raw > kPocketInkJSMaxHttpBytes) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{172});
      }
      request->maxBytes = static_cast<size_t>(raw);
    }
    const PocketInkJS::Value timeout = vm.objectGet(options, "timeout");
    if (!vm.isUndefined(timeout)) {
      const double raw = vm.toNumber(timeout);
      if (!std::isfinite(raw) || raw < 100 || raw > 30000) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{173});
      }
      request->timeoutMs = static_cast<uint32_t>(raw);
    }
    const PocketInkJS::Value body = vm.objectGet(options, "body");
    if (!vm.isUndefined(body)) {
      if (!vm.isString(body)) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{174});
      }
      const PocketInkJS::StringView text = vm.stringView(body);
      if (text.size > kPocketInkJSMaxHttpBytes) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{175});
      }
      if (text.size > 0) {
        request->body = static_cast<uint8_t *>(allocatePsram(text.size));
        if (request->body == nullptr) {
          releaseFetchRequest(request);
          return vm.fail(PocketInkJS::ErrorCode{176});
        }
        memcpy(request->body, text.data, text.size);
        request->bodyBytes = text.size;
      }
    }
    const PocketInkJS::Value headers = vm.objectGet(options, "headers");
    if (!vm.isUndefined(headers)) {
      if (vm.typeOf(headers) != PocketInkJS::ValueType::Object) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{177});
      }
      const size_t count = vm.objectPropertyCount(headers);
      if (count > sizeof(request->headers) / sizeof(request->headers[0])) {
        releaseFetchRequest(request);
        return vm.fail(PocketInkJS::ErrorCode{178});
      }
      for (size_t i = 0; i < count; i++) {
        PocketInkJS::Value key;
        PocketInkJS::Value value;
        vm.objectPropertyAt(headers, i, &key, &value);
        if (!vm.isString(value)) {
          releaseFetchRequest(request);
          return vm.fail(PocketInkJS::ErrorCode{179});
        }
        const PocketInkJS::StringView keyText = vm.stringView(key);
        const PocketInkJS::StringView valueText = vm.stringView(value);
        if (keyText.size >= sizeof(request->headers[i].name) ||
            valueText.size >= sizeof(request->headers[i].value)) {
          releaseFetchRequest(request);
          return vm.fail(PocketInkJS::ErrorCode{180});
        }
        memcpy(request->headers[i].name, keyText.data, keyText.size);
        memcpy(request->headers[i].value, valueText.data, valueText.size);
        request->headers[i].name[keyText.size] = '\0';
        request->headers[i].value[valueText.size] = '\0';
        request->headerCount++;
      }
    }
  }
  PocketInkJS::Value promise = vm.makePromise();
  if (vm.isUndefined(promise)) {
    releaseFetchRequest(request);
    return promise;
  }
  const PocketInkJS::RootHandle retained = vm.retain(promise);
  if (retained == PocketInkJS::kInvalidRoot) {
    releaseFetchRequest(request);
    return vm.undefined();
  }
  request->promise = retained;
  if (!ensureAsyncWorker() ||
      xQueueSend(asyncState->requests, &request, 0) != pdPASS) {
    releaseFetchRequest(request);
    vm.settlePromise(promise, vm.makeString("fetch worker is busy"), true);
    vm.release(retained);
  }
  return promise;
}

bool PocketInkJSApp::pumpAsyncCompletions() {
  if (asyncState == nullptr || runtime == nullptr)
    return false;
  bool completedAny = false;
  FetchCompletion *completion = nullptr;
  while (xQueueReceive(asyncState->completions, &completion, 0) == pdPASS) {
    completedAny = true;
    if (completion == nullptr)
      continue;
    const PocketInkJS::Value promise = runtime->retained(completion->promise);
    if (completion->error != PocketInkJS::ErrorCode{}) {
      char error[5];
      PocketInkJS::formatErrorCode(completion->error, error);
      runtime->settlePromise(promise, runtime->makeString(error, 4), true);
      runtime->release(completion->promise);
      releaseFetchCompletion(completion);
      continue;
    }
    PocketInkJS::Value response = runtime->makeObject();
    PocketInkJS::Value body = runtime->makeString(
        reinterpret_cast<const char *>(completion->body), completion->bodyBytes);
    PocketInkJS::Value headers = runtime->makeObject();
    runtime->objectSet(response, "ok",
                       runtime->boolean(completion->statusCode >= 200 &&
                                        completion->statusCode < 300));
    runtime->objectSet(response, "status",
                       runtime->number(completion->statusCode));
    runtime->objectSet(response, "statusText", runtime->makeString(""));
    runtime->objectSet(response, "url", runtime->makeString(completion->url));
    runtime->objectSet(response, "headers", headers);
    runtime->objectSet(response, "bodyUsed", runtime->boolean(false));
    runtime->objectSet(response, "_body", body);
    runtime->objectSet(response, "text", runtime->get("__responseText"));
    runtime->objectSet(response, "json", runtime->get("__responseJson"));
    if (runtime->error().message[0] == '\0')
      runtime->settlePromise(promise, response, false);
    runtime->release(completion->promise);
    releaseFetchCompletion(completion);
  }
  if (runtime->error().message[0] != '\0')
    setRuntimeError();
  return completedAny;
}

bool PocketInkJSApp::waitForPromise(PocketInkJS::Runtime &vm, PocketInkJS::Value promise) {
  const uint32_t startedAt = millis();
  while (vm.promiseState(promise) == PocketInkJS::PromiseState::Pending &&
         status[0] == '\0' && millis() - startedAt < 31000U) {
    pumpAsyncCompletions();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return vm.promiseState(promise) != PocketInkJS::PromiseState::Pending;
}

PocketInkJS::Value PocketInkJSApp::scheduleTimer(PocketInkJS::Runtime &vm,
                                      const PocketInkJS::Value *args, size_t argc,
                                      bool repeating) {
  if (argc < 1 || !vm.isCallable(args[0]))
    return vm.fail(PocketInkJS::ErrorCode{181});
  const double rawDelay = argc > 1 ? vm.toNumber(args[1]) : 0;
  if (!std::isfinite(rawDelay) || rawDelay < 0 || rawDelay > UINT32_MAX)
    return vm.fail(PocketInkJS::ErrorCode{182});
  if (timerState == nullptr) {
    timerState = static_cast<TimerState *>(allocatePsram(sizeof(TimerState)));
    if (timerState == nullptr)
      return vm.fail(PocketInkJS::ErrorCode{183});
    new (timerState) TimerState();
  }
  TimerSlot *slot = nullptr;
  for (TimerSlot &candidate : timerState->slots) {
    if (!candidate.active) {
      slot = &candidate;
      break;
    }
  }
  if (slot == nullptr)
    return vm.fail(PocketInkJS::ErrorCode{184});
  PocketInkJS::Value arguments = vm.makeArray(argc > 2 ? argc - 2U : 0);
  for (size_t i = 2; i < argc; i++)
    vm.arrayPush(arguments, args[i]);
  const PocketInkJS::RootHandle callback = vm.retain(args[0]);
  const PocketInkJS::RootHandle argumentRoot = vm.retain(arguments);
  if (callback == PocketInkJS::kInvalidRoot ||
      argumentRoot == PocketInkJS::kInvalidRoot) {
    vm.release(callback);
    vm.release(argumentRoot);
    return vm.undefined();
  }
  uint32_t id = timerState->nextId++;
  if (id == 0)
    id = timerState->nextId++;
  const uint32_t delayMs = static_cast<uint32_t>(rawDelay);
  *slot = TimerSlot{id, static_cast<uint32_t>(millis()) + delayMs,
                    repeating ? (delayMs == 0 ? 1U : delayMs) : 0U,
                    callback, argumentRoot, true};
  return vm.number(id);
}

PocketInkJS::Value PocketInkJSApp::cancelTimer(PocketInkJS::Runtime &vm,
                                    const PocketInkJS::Value *args, size_t argc) {
  if (argc != 1 || !std::isfinite(vm.toNumber(args[0])))
    return vm.fail(PocketInkJS::ErrorCode{185});
  const uint32_t id = static_cast<uint32_t>(vm.toNumber(args[0]));
  if (timerState != nullptr) {
    for (TimerSlot &slot : timerState->slots) {
      if (slot.active && slot.id == id) {
        vm.release(slot.callback);
        vm.release(slot.arguments);
        slot = TimerSlot{};
        break;
      }
    }
  }
  return vm.undefined();
}

bool PocketInkJSApp::pumpTimers() {
  if (timerState == nullptr || runtime == nullptr || status[0] != '\0')
    return false;
  bool dirty = false;
  const uint32_t now = millis();
  for (TimerSlot &slot : timerState->slots) {
    if (!slot.active || static_cast<int32_t>(now - slot.dueAt) < 0)
      continue;
    const PocketInkJS::Value callback = runtime->retained(slot.callback);
    const PocketInkJS::Value arguments = runtime->retained(slot.arguments);
    const size_t argc = runtime->arrayLength(arguments);
    PocketInkJS::Value argv[16];
    for (size_t i = 0; i < argc && i < sizeof(argv) / sizeof(argv[0]); i++)
      argv[i] = runtime->arrayAt(arguments, i);
    const bool oneShot = slot.intervalMs == 0;
    if (oneShot) {
      slot.active = false;
    } else {
      slot.dueAt = now + slot.intervalMs;
    }
    logPocketInkJSStackLow("timer");
    const PocketInkJS::RunResult result = runtime->call(callback, argv, argc);
    logPocketInkJSStackLow("timer");
    if (oneShot) {
      runtime->release(slot.callback);
      runtime->release(slot.arguments);
      slot = TimerSlot{};
    }
    if (!result.ok) {
      setRuntimeError();
      break;
    }
    dirty = runtime->isTruthy(result.value) || dirty;
  }
  return dirty;
}

void PocketInkJSApp::releaseTimers() {
  if (timerState == nullptr)
    return;
  if (runtime != nullptr) {
    for (TimerSlot &slot : timerState->slots) {
      if (slot.active) {
        runtime->release(slot.callback);
        runtime->release(slot.arguments);
      }
    }
  }
  timerState->~TimerState();
  heap_caps_free(timerState);
  timerState = nullptr;
}

bool PocketInkJSApp::beginSystemModal(PocketInkJSSystemModal kind,
                                 PocketInkJS::Value promise,
                                 uint32_t captureDurationMs,
                                 DatePickerDate initialDate) {
  if (systemModal != PocketInkJSSystemModal::None || runtime == nullptr) {
    return false;
  }
  const PocketInkJS::RootHandle retained = runtime->retain(promise);
  if (retained == PocketInkJS::kInvalidRoot) {
    return false;
  }
  systemModal = kind;
  systemModalPromise = retained;
  systemModalCaptureDurationMs = captureDurationMs;
  systemModalDate = initialDate;
  return true;
}

bool PocketInkJSApp::pumpSystemModal() {
  if (systemModal == PocketInkJSSystemModal::None || runtime == nullptr) {
    return false;
  }
  AppApiStatus status = AppApiStatus::Pending;
  PocketInkJS::Value result = runtime->undefined();
  switch (systemModal) {
  case PocketInkJSSystemModal::FilePicker: {
    char path[288];
    status = appApiTakePickedFile(path, sizeof(path));
    if (status == AppApiStatus::Ok) {
      result = runtime->makeString(path);
    }
    break;
  }
  case PocketInkJSSystemModal::Keyboard: {
    char text[256];
    status = appApiGetKeyboardInput("", 0, 255, text, sizeof(text));
    if (status == AppApiStatus::Ok) {
      result = runtime->makeString(text);
    }
    break;
  }
  case PocketInkJSSystemModal::DatePicker: {
    DatePickerDate selected;
    status = appApiGetDateInput(systemModalDate, selected);
    if (status == AppApiStatus::Ok) {
      result = runtime->makeObject();
      runtime->objectSet(result, "year", runtime->number(selected.year));
      runtime->objectSet(result, "month", runtime->number(selected.month));
      runtime->objectSet(result, "day", runtime->number(selected.day));
    }
    break;
  }
  case PocketInkJSSystemModal::AudioCapture: {
    const uint8_t *data = nullptr;
    size_t size = 0;
    status = appApiCaptureWav(systemModalCaptureDurationMs, &data, &size);
    if (status == kAppApiEnd && data != nullptr) {
      result = runtime->makeString(reinterpret_cast<const char *>(data), size);
      status = AppApiStatus::Ok;
    }
    break;
  }
  case PocketInkJSSystemModal::AudioCapturePcm16: {
    const uint8_t *data = nullptr;
    size_t size = 0;
    status = appApiCapturePcm16(systemModalCaptureDurationMs, &data, &size);
    if (status == kAppApiEnd && data != nullptr) {
      result = runtime->makeString(reinterpret_cast<const char *>(data), size);
      status = AppApiStatus::Ok;
    }
    break;
  }
  case PocketInkJSSystemModal::None:
    return false;
  }
  if (status == AppApiStatus::Pending) {
    return false;
  }
  const bool rejected = status != AppApiStatus::Ok &&
                        status != AppApiStatus::Denied;
  if (status == AppApiStatus::Denied) {
    result = runtime->boolean(false);
  } else if (rejected) {
    result = runtime->makeString("system input failed");
  }
  const PocketInkJS::Value promise = runtime->retained(systemModalPromise);
  runtime->settlePromise(promise, result, rejected);
  runtime->release(systemModalPromise);
  systemModal = PocketInkJSSystemModal::None;
  systemModalPromise = PocketInkJS::kInvalidRoot;
  systemModalCaptureDurationMs = 0;
  return true;
}

void PocketInkJSApp::stopAsyncWorker() {
  if (asyncState == nullptr)
    return;
  asyncState->stopping.store(true);
  asyncState = nullptr; // The worker owns and releases its detached state.
}

PocketInkJS::Value nativeConfigGet(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (activePocketInkJSApp == nullptr || argc != 1 || !runtime.isString(args[0])) {
    return runtime.fail(PocketInkJS::ErrorCode{186});
  }
  const PocketInkJS::StringView key = runtime.stringView(args[0]);
  if (key.size >= 64) {
    return runtime.fail(PocketInkJS::ErrorCode{187});
  }
  char keyText[64];
  memcpy(keyText, key.data, key.size);
  keyText[key.size] = '\0';
  char value[CONFIG_VALUE_MAX];
  if (!configGetForApp(activePocketInkJSApp->id(), keyText, value, sizeof(value))) {
    return runtime.undefined();
  }
  return runtime.makeString(value);
}

PocketInkJS::Value nativeConfigSet(PocketInkJS::Runtime &runtime,
                              const PocketInkJS::Value *args, size_t argc, void *) {
  if (activePocketInkJSApp == nullptr || argc != 2 || !runtime.isString(args[0])) {
    return runtime.fail(PocketInkJS::ErrorCode{188});
  }
  const PocketInkJS::StringView key = runtime.stringView(args[0]);
  if (key.size >= 64) {
    return runtime.fail(PocketInkJS::ErrorCode{187});
  }
  char keyText[64];
  memcpy(keyText, key.data, key.size);
  keyText[key.size] = '\0';
  char value[CONFIG_VALUE_MAX];
  runtime.format(args[1], value, sizeof(value));
  return runtime.boolean(
      configSetForApp(activePocketInkJSApp->id(), keyText, value));
}

PocketInkJS::Value nativeHttpGet(PocketInkJS::Runtime &runtime,
                            const PocketInkJS::Value *args, size_t argc, void *) {
#if ENABLE_NETWORK_APPS
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail(PocketInkJS::ErrorCode{189});
  }
  if (!wifiIsConnected()) {
    return runtime.fail(PocketInkJS::ErrorCode{190});
  }
  const PocketInkJS::StringView url = runtime.stringView(args[0]);
  if (url.size == 0 || url.size >= 512) {
    return runtime.fail(PocketInkJS::ErrorCode{191});
  }
  char urlText[512];
  memcpy(urlText, url.data, url.size);
  urlText[url.size] = '\0';
  size_t limit = kPocketInkJSMaxHttpBytes;
  if (argc == 2) {
    const double requested = runtime.toNumber(args[1]);
    if (!std::isfinite(requested) || requested < 1 ||
        requested > static_cast<double>(kPocketInkJSMaxHttpBytes)) {
      return runtime.fail(PocketInkJS::ErrorCode{192});
    }
    limit = static_cast<size_t>(requested);
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kPocketInkJSHttpTimeoutMs);
  if (!http.begin(client, urlText)) {
    return runtime.fail(PocketInkJS::ErrorCode{193});
  }
  http.addHeader("User-Agent", "PocketInk-PocketInkJS/0.1");
  const int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return runtime.fail(PocketInkJS::ErrorCode{194});
  }
  const int expected = http.getSize();
  if (expected > 0 && static_cast<size_t>(expected) > limit) {
    http.end();
    return runtime.fail(PocketInkJS::ErrorCode{195});
  }
  if (expected == 0) {
    http.end();
    return runtime.makeString("");
  }
  const size_t allocationSize =
      expected > 0 ? static_cast<size_t>(expected) : limit;
  if (allocationSize == 0) {
    http.end();
    return runtime.makeString("");
  }
  uint8_t *buffer = static_cast<uint8_t *>(allocatePsram(allocationSize));
  if (buffer == nullptr) {
    http.end();
    return runtime.fail(PocketInkJS::ErrorCode{196});
  }
  auto *stream = http.getStreamPtr();
  size_t length = 0;
  const unsigned long startedAt = millis();
  while (length < allocationSize) {
    while (stream->available() > 0 && length < allocationSize) {
      const int byte = stream->read();
      if (byte < 0) {
        break;
      }
      buffer[length++] = static_cast<uint8_t>(byte);
    }
    if ((expected >= 0 && length >= static_cast<size_t>(expected)) ||
        (!stream->connected() && stream->available() <= 0)) {
      break;
    }
    if (millis() - startedAt >= kPocketInkJSHttpTimeoutMs) {
      break;
    }
    delay(1);
  }
  http.end();
  if (expected > 0 && length != static_cast<size_t>(expected)) {
    heap_caps_free(buffer);
    return runtime.fail(PocketInkJS::ErrorCode{197});
  }
  PocketInkJS::Value result = runtime.makeString(
      reinterpret_cast<const char *>(buffer), length);
  heap_caps_free(buffer);
  return result;
#else
  (void)args;
  (void)argc;
  return runtime.fail(PocketInkJS::ErrorCode{198});
#endif
}

PocketInkJS::Value nativeFetch(PocketInkJS::Runtime &runtime, const PocketInkJS::Value *args,
                          size_t argc, void *) {
  return activePocketInkJSApp != nullptr
             ? activePocketInkJSApp->queueFetch(runtime, args, argc)
             : runtime.fail(PocketInkJS::ErrorCode{199});
}

PocketInkJS::Value nativeSetTimeout(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc, void *) {
  return activePocketInkJSApp != nullptr
             ? activePocketInkJSApp->scheduleTimer(runtime, args, argc, false)
             : runtime.fail(PocketInkJS::ErrorCode{199});
}

PocketInkJS::Value nativeSetInterval(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  return activePocketInkJSApp != nullptr
             ? activePocketInkJSApp->scheduleTimer(runtime, args, argc, true)
             : runtime.fail(PocketInkJS::ErrorCode{199});
}

PocketInkJS::Value nativeClearTimer(PocketInkJS::Runtime &runtime,
                               const PocketInkJS::Value *args, size_t argc, void *) {
  return activePocketInkJSApp != nullptr
             ? activePocketInkJSApp->cancelTimer(runtime, args, argc)
             : runtime.undefined();
}

bool awaitPendingPromise(PocketInkJS::Runtime &runtime, PocketInkJS::Value promise,
                         void *userData) {
  PocketInkJSApp *app = static_cast<PocketInkJSApp *>(userData);
  return app != nullptr && app->waitForPromise(runtime, promise);
}

PocketInkJS::Value nativeResponseText(PocketInkJS::Runtime &runtime,
                                 PocketInkJS::Value receiver,
                                 const PocketInkJS::Value *, size_t argc, void *) {
  if (argc != 0 || runtime.typeOf(receiver) != PocketInkJS::ValueType::Object)
    return runtime.fail(PocketInkJS::ErrorCode{200});
  const PocketInkJS::Value body = runtime.objectGet(receiver, "_body");
  PocketInkJS::Value promise = runtime.makePromise();
  runtime.settlePromise(promise, body, false);
  return promise;
}

PocketInkJS::Value nativeResponseJson(PocketInkJS::Runtime &runtime,
                                 PocketInkJS::Value receiver,
                                 const PocketInkJS::Value *, size_t argc, void *) {
  if (argc != 0 || runtime.typeOf(receiver) != PocketInkJS::ValueType::Object)
    return runtime.fail(PocketInkJS::ErrorCode{201});
  return runtime.parseJsonPromise(runtime.objectGet(receiver, "_body"));
}

PocketInkJS::Value nativeUiButton(PocketInkJS::Runtime &runtime,
                             const PocketInkJS::Value *args, size_t argc, void *) {
  if (argc < 5 || argc > 7 || !runtime.isString(args[4]))
    return runtime.fail(PocketInkJS::ErrorCode{202});
  if (!numericArguments(runtime, args, 4, 4))
    return runtime.undefined();
  if (PocketInkJSDrawTarget == nullptr)
    return runtime.undefined();
  const PocketInkJS::StringView label = runtime.stringView(args[4]);
  if (label.size >= 96)
    return runtime.fail(PocketInkJS::ErrorCode{203});
  char text[96];
  memcpy(text, label.data, label.size);
  text[label.size] = '\0';
  TouchButtonContent content;
  content.text = text;
  content.textSize = argc > 6 ? toU8(runtime.toNumber(args[6])) : 1;
  const UiRect bounds = {toI16(runtime.toNumber(args[0])),
                         toI16(runtime.toNumber(args[1])),
                         toI16(runtime.toNumber(args[2])),
                         toI16(runtime.toNumber(args[3]))};
  TouchButton(bounds, content).draw(*PocketInkJSDrawTarget,
                                    argc > 5 && runtime.toBoolean(args[5]));
  return runtime.undefined();
}

PocketInkJS::Value nativeUiButtonContains(PocketInkJS::Runtime &runtime,
                                     const PocketInkJS::Value *args, size_t argc,
                                     void *) {
  if (argc < 6 || argc > 7 || !numericArguments(runtime, args, argc, argc))
    return runtime.undefined();
  TouchButtonContent content;
  TouchButtonStyle style;
  style.touchPadding = argc > 6 ? toU8(runtime.toNumber(args[6]))
                                : UI_TOUCH_ASSIST_PX;
  const UiRect bounds = {toI16(runtime.toNumber(args[2])),
                         toI16(runtime.toNumber(args[3])),
                         toI16(runtime.toNumber(args[4])),
                         toI16(runtime.toNumber(args[5]))};
  const TouchPoint point = {toU16(runtime.toNumber(args[0])),
                            toU16(runtime.toNumber(args[1]))};
  return runtime.boolean(
      TouchButton(bounds, content, TouchButtonShape::rectangle(), style)
          .contains(point));
}

PocketInkJS::Value nativeUiListTouch(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (!numericArguments(runtime, args, argc, 10))
    return runtime.undefined();
  const int modeValue = static_cast<int>(runtime.toNumber(args[8]));
  const UiListNavMode mode = modeValue == 1   ? UiListNavMode::Select
                             : modeValue == 2 ? UiListNavMode::Split
                                              : UiListNavMode::Scroll;
  uint16_t offset = toU16(runtime.toNumber(args[9]));
  const UiListTouchResult hit = uiListHandleTouch(
      TouchPoint{toU16(runtime.toNumber(args[0])),
                 toU16(runtime.toNumber(args[1]))},
      toU16(runtime.toNumber(args[2])), toU8(runtime.toNumber(args[3])),
      toI16(runtime.toNumber(args[4])), toI16(runtime.toNumber(args[5])),
      toI16(runtime.toNumber(args[6])), toI16(runtime.toNumber(args[7])), mode,
      offset);
  PocketInkJS::Value result = runtime.makeObject();
  const char *action = hit.action == UiListTouchAction::Scroll
                           ? "scroll"
                           : (hit.action == UiListTouchAction::Select ? "select"
                                                                     : "none");
  runtime.objectSet(result, "action", runtime.makeString(action));
  runtime.objectSet(result, "index", runtime.number(hit.index));
  runtime.objectSet(result, "offset", runtime.number(offset));
  return result;
}

bool readOptionRect(PocketInkJS::Runtime &runtime, PocketInkJS::Value option,
                    UiRect &rect) {
  if (runtime.typeOf(option) != PocketInkJS::ValueType::Object)
    return false;
  const PocketInkJS::Value x = runtime.objectGet(option, "x");
  const PocketInkJS::Value y = runtime.objectGet(option, "y");
  const PocketInkJS::Value w = runtime.objectGet(option, "w");
  const PocketInkJS::Value h = runtime.objectGet(option, "h");
  if (!runtime.isNumber(x) || !runtime.isNumber(y) || !runtime.isNumber(w) ||
      !runtime.isNumber(h))
    return false;
  rect = {toI16(runtime.toNumber(x)), toI16(runtime.toNumber(y)),
          toI16(runtime.toNumber(w)), toI16(runtime.toNumber(h))};
  return true;
}

PocketInkJS::Value nativeUiSelectDraw(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  if (argc < 2 || argc > 3 ||
      runtime.typeOf(args[0]) != PocketInkJS::ValueType::Array)
    return runtime.fail(PocketInkJS::ErrorCode{204});
  if (PocketInkJSDrawTarget == nullptr)
    return runtime.undefined();
  const double selected = runtime.toNumber(args[1]);
  for (size_t i = 0; i < runtime.arrayLength(args[0]); i++) {
    const PocketInkJS::Value option = runtime.arrayAt(args[0], i);
    UiRect rect;
    const PocketInkJS::Value labelValue = runtime.objectGet(option, "label");
    if (!readOptionRect(runtime, option, rect) ||
        !runtime.isString(labelValue))
      return runtime.fail(PocketInkJS::ErrorCode{205});
    const PocketInkJS::StringView label = runtime.stringView(labelValue);
    if (label.size >= 64)
      return runtime.fail(PocketInkJS::ErrorCode{206});
    char text[64];
    memcpy(text, label.data, label.size);
    text[label.size] = '\0';
    const bool pressed = runtime.toNumber(runtime.objectGet(option, "value")) ==
                         selected;
    uiDrawButton(*PocketInkJSDrawTarget, rect, text, pressed,
                 argc > 2 ? toU8(runtime.toNumber(args[2])) : 1);
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeUiSelectHit(PocketInkJS::Runtime &runtime,
                                const PocketInkJS::Value *args, size_t argc,
                                void *) {
  if (argc < 3 || argc > 4 || !runtime.isNumber(args[0]) ||
      !runtime.isNumber(args[1]) ||
      runtime.typeOf(args[2]) != PocketInkJS::ValueType::Array)
    return runtime.fail(PocketInkJS::ErrorCode{207});
  const TouchPoint point = {toU16(runtime.toNumber(args[0])),
                            toU16(runtime.toNumber(args[1]))};
  const uint8_t padding = argc > 3 ? toU8(runtime.toNumber(args[3]))
                                   : UI_TOUCH_ASSIST_PX;
  for (size_t i = 0; i < runtime.arrayLength(args[2]); i++) {
    const PocketInkJS::Value option = runtime.arrayAt(args[2], i);
    UiRect rect;
    if (!readOptionRect(runtime, option, rect))
      return runtime.fail(PocketInkJS::ErrorCode{205});
    if (uiTouchContains(rect, point, padding))
      return runtime.objectGet(option, "value");
  }
  return runtime.undefined();
}

PocketInkJS::Value nativeGetKeyboardInput(PocketInkJS::Runtime &runtime,
                                     const PocketInkJS::Value *args, size_t argc,
                                     void *) {
  if (activePocketInkJSApp == nullptr || argc < 1 || argc > 3 ||
      !runtime.isString(args[0])) {
    return runtime.fail("ui.getKeyboardInput expects initialText, layout?, maxLength?");
  }
  const double layoutValue = argc > 1 ? runtime.toNumber(args[1]) : 0;
  const double maxValue = argc > 2 ? runtime.toNumber(args[2]) : 255;
  if (!std::isfinite(layoutValue) || !std::isfinite(maxValue) ||
      layoutValue < 0 || layoutValue > 2 || maxValue < 1 || maxValue > 255 ||
      std::floor(layoutValue) != layoutValue || std::floor(maxValue) != maxValue) {
    return runtime.fail("invalid keyboard options");
  }
  char initial[256];
  if (!copyJsString(runtime, args[0], initial, sizeof(initial)) ||
      strlen(initial) > static_cast<size_t>(maxValue)) {
    return runtime.fail("initial keyboard text is too long");
  }
  PocketInkJS::Value promise = runtime.makePromise();
  if (runtime.isUndefined(promise)) {
    return promise;
  }
  if (!activePocketInkJSApp->systemModalIdle() ||
      appApiGetKeyboardInput(initial, static_cast<uint8_t>(layoutValue),
                             static_cast<size_t>(maxValue), initial,
                             sizeof(initial)) != AppApiStatus::Pending ||
      !activePocketInkJSApp->beginSystemModal(PocketInkJSSystemModal::Keyboard,
                                         promise)) {
    runtime.settlePromise(promise, runtime.makeString("keyboard is busy"), true);
  }
  return promise;
}

PocketInkJS::Value nativeGetDateInput(PocketInkJS::Runtime &runtime,
                                 const PocketInkJS::Value *args, size_t argc,
                                 void *) {
  if (activePocketInkJSApp == nullptr || !numericArguments(runtime, args, argc, 3)) {
    return runtime.fail("ui.getDateInput expects year, month, day");
  }
  const double yearValue = runtime.toNumber(args[0]);
  const double monthValue = runtime.toNumber(args[1]);
  const double dayValue = runtime.toNumber(args[2]);
  if (yearValue < 1 || yearValue > 9999 || monthValue < 1 ||
      monthValue > 12 || dayValue < 1 || dayValue > 31 ||
      std::floor(yearValue) != yearValue ||
      std::floor(monthValue) != monthValue || std::floor(dayValue) != dayValue)
    return runtime.fail("invalid date picker date");
  const int year = static_cast<int>(yearValue);
  const int month = static_cast<int>(monthValue);
  const int day = static_cast<int>(dayValue);
  if (day > DateTimePickerComponent::daysInMonth(year, month))
    return runtime.fail("invalid date picker date");
  const DatePickerDate initial{year, month, day};
  PocketInkJS::Value promise = runtime.makePromise();
  DatePickerDate ignored;
  if (runtime.isUndefined(promise)) {
    return promise;
  }
  if (!activePocketInkJSApp->systemModalIdle() ||
      appApiGetDateInput(initial, ignored) != AppApiStatus::Pending ||
      !activePocketInkJSApp->beginSystemModal(PocketInkJSSystemModal::DatePicker,
                                         promise, 0, initial)) {
    runtime.settlePromise(promise, runtime.makeString("date picker is busy"),
                          true);
  }
  return promise;
}

bool PocketInkJSApp::installBindings() {
  if (runtime == nullptr) {
    return false;
  }
  const struct {
    const char *path;
    PocketInkJS::NativeFunction function;
  } bindings[] = {
      //JS/ Prints values to the PocketInkJS debug log.
      //JS/ Returns: void
      //JS/ Args: ...values (any)
      {"console.log", nativeConsoleLog},
      //JS/ Returns the number of milliseconds since boot.
      //JS/ Returns: number
      {"time.millis", nativeMillis},
      //JS/ Returns the number of heap bytes currently used by the JS runtime.
      //JS/ Returns: number
      {"PocketInkJS.heapUsed", nativePocketInkJSHeapUsed},
      //JS/ Returns the number of heap bytes currently free in the JS runtime.
      //JS/ Returns: number
      {"PocketInkJS.heapFree", nativePocketInkJSHeapFree},
      //JS/ Returns the peak heap usage since the JS runtime was created.
      //JS/ Returns: number
      {"PocketInkJS.heapPeak", nativePocketInkJSHeapPeak},
      //JS/ Checks whether Wi-Fi is connected.
      //JS/ Returns: boolean
      {"network.connected", nativeNetworkConnected},
      //JS/ Starts an asynchronous scan for nearby Wi-Fi networks.
      //JS/ Returns: Permissions
      {"network.scan", nativeNetworkScan},
      //JS/ Returns the list of networks found by the last scan.
      //JS/ Returns: object[] - { ssid: string, rssi: number }
      {"network.networks", nativeNetworkNetworks},
      //JS/ Performs a synchronous HTTP GET request and returns the response body.
      //JS/ Returns: string
      //JS/ Args: url (string), maxBytes? (number)
      {"http.get", nativeHttpGet},
      //JS/ Queues an async HTTP request. Returns a Promise that resolves with a Response object.
      //JS/ Returns: Promise
      //JS/ Args: url (string), options? (object)
      {"fetch", nativeFetch},
      //JS/ Schedules a function to run once after a delay in milliseconds.
      //JS/ Returns: number
      //JS/ Args: callback (function), delayMs (number)
      {"setTimeout", nativeSetTimeout},
      //JS/ Schedules a function to run repeatedly at an interval in milliseconds.
      //JS/ Returns: number
      //JS/ Args: callback (function), intervalMs (number)
      {"setInterval", nativeSetInterval},
      //JS/ Cancels a timeout or interval previously created with setTimeout or setInterval.
      //JS/ Returns: void
      //JS/ Args: id (number)
      {"clearTimeout", nativeClearTimer},
      //JS/ Cancels a timeout or interval previously created with setTimeout or setInterval.
      //JS/ Returns: void
      //JS/ Args: id (number)
      {"clearInterval", nativeClearTimer},
      //JS/ Prevents the device from entering sleep while the app is active.
      //JS/ Returns: void
      //JS/ Args: active (boolean)
      {"app.setWakeLock", nativeSetWakeLock},
      //JS/ Reads a per-app configuration value by key. Returns the value string or undefined.
      //JS/ Returns: string | undefined
      //JS/ Args: key (string)
      {"app.configGet", nativeConfigGet},
      //JS/ Writes a per-app configuration value by key.
      //JS/ Returns: boolean
      //JS/ Args: key (string), value (string)
      {"app.configSet", nativeConfigSet},
      //JS/ Launches another app by its app ID.
      //JS/ Returns: boolean
      //JS/ Args: appId (string)
      {"app.open", nativeAppOpen},
      //JS/ Reads a file and returns an object with data, bytes, size, offset, and eof properties.
      //JS/ Returns: object - { data: string, bytes: number, size: number, offset: number, eof: boolean }
      //JS/ Args: path (string), offset? (number), maxBytes? (number)
      {"files.readText", nativeFilesReadText},
      //JS/ Reads a file and returns binary data with metadata (same as readText).
      //JS/ Returns: object - { data: string, bytes: number, size: number, offset: number, eof: boolean }
      //JS/ Args: path (string), offset? (number), maxBytes? (number)
      {"files.readBinary", nativeFilesReadBinary},
      //JS/ Writes string data to a file. Optionally appends instead of overwriting.
      //JS/ Returns: Permissions
      //JS/ Args: path (string), data (string), append? (boolean)
      {"files.write", nativeFilesWrite},
      //JS/ Deletes a file by path.
      //JS/ Returns: Permissions
      //JS/ Args: path (string)
      {"files.delete", nativeFilesDelete},
      //JS/ Opens the system file picker UI. Returns a Promise that resolves with the selected path.
      //JS/ Returns: Promise
      //JS/ Args: initialPath? (string), filter? (string)
      {"files.pick", nativeFilesPick},
      //JS/ Reads an entire file as a string. Returns a Promise.
      //JS/ Returns: Promise
      //JS/ Args: path (string), encoding? (string)
      {"fs.readFile", nativeFsReadFile},
      //JS/ Reads an entire file as a string. Synchronous.
      //JS/ Returns: string
      //JS/ Args: path (string), encoding? (string)
      {"fs.readFileSync", nativeFsReadFileSync},
      //JS/ Writes data to a file. Returns a Promise.
      //JS/ Returns: Promise
      //JS/ Args: path (string), data (string), encoding? (string)
      {"fs.writeFile", nativeFsWriteFile},
      //JS/ Writes data to a file. Synchronous.
      //JS/ Returns: Permissions
      //JS/ Args: path (string), data (string), encoding? (string)
      {"fs.writeFileSync", nativeFsWriteFileSync},
      //JS/ Appends data to a file. Returns a Promise.
      //JS/ Returns: Promise
      //JS/ Args: path (string), data (string), encoding? (string)
      {"fs.appendFile", nativeFsAppendFile},
      //JS/ Appends data to a file. Synchronous.
      //JS/ Returns: Permissions
      //JS/ Args: path (string), data (string), encoding? (string)
      {"fs.appendFileSync", nativeFsAppendFileSync},
      //JS/ Lists the entries in a directory. Returns a Promise that resolves with an array of names.
      //JS/ Returns: Promise
      //JS/ Args: path (string), encoding? (string)
      {"fs.readdir", nativeFsReaddir},
      //JS/ Lists the entries in a directory. Synchronous. Returns an array of names.
      //JS/ Returns: string[]
      //JS/ Args: path (string), encoding? (string)
      {"fs.readdirSync", nativeFsReaddirSync},
      //JS/ Checks whether a file exists at the given path.
      //JS/ Returns: boolean
      //JS/ Args: path (string)
      {"fs.existsSync", nativeFsExistsSync},
      //JS/ Gets file stats (size, isFile, isDirectory). Returns a Promise.
      //JS/ Returns: Promise
      //JS/ Args: path (string)
      {"fs.stat", nativeFsStat},
      //JS/ Gets file stats (size, isFile, isDirectory). Synchronous.
      //JS/ Returns: object - { size: number, isFile: boolean, isDirectory: boolean }
      //JS/ Args: path (string)
      {"fs.statSync", nativeFsStatSync},
      //JS/ Deletes a file. Returns a Promise.
      //JS/ Returns: Promise
      //JS/ Args: path (string)
      {"fs.unlink", nativeFsUnlink},
      //JS/ Deletes a file. Synchronous.
      //JS/ Returns: Permissions
      //JS/ Args: path (string)
      {"fs.unlinkSync", nativeFsUnlinkSync},
      //JS/ Configures a GPIO pin as input, input pull-up, or output.
      //JS/ Returns: Permissions
      //JS/ Args: pin (number), mode (number)
      {"gpio.mode", nativeGpioMode},
      //JS/ Reads the current value of a GPIO pin. Returns an object with status and value.
      //JS/ Returns: object - { status: number, value: number }
      //JS/ Args: pin (number)
      {"gpio.read", nativeGpioRead},
      //JS/ Sets the output value of a GPIO pin.
      //JS/ Returns: Permissions
      //JS/ Args: pin (number), value (boolean)
      {"gpio.write", nativeGpioWrite},
      //JS/ Reads environment sensor values (temperature, humidity). Optionally forces a refresh.
      //JS/ Returns: object - { temperatureValid: boolean, humidityValid: boolean, cpuTemperatureValid: boolean, temperatureC: number, humidityPct: number, cpuTemperatureC: number }
      //JS/ Args: refresh? (boolean)
      {"sensors.read", nativeSensorsRead},
      //JS/ Captures audio as a WAV file. Returns a Promise that resolves with the audio data.
      //JS/ Returns: Promise
      //JS/ Args: durationMs (number)
      {"audio.captureWav", nativeAudioCaptureWav},
      //JS/ Captures audio as raw PCM16 data. Returns a Promise that resolves with the audio data.
      //JS/ Returns: Promise
      //JS/ Args: durationMs (number)
      {"audio.capturePcm16", nativeAudioCapturePcm16},
      //JS/ Checks whether a given audio format string is supported.
      //JS/ Returns: boolean
      //JS/ Args: format (string)
      {"audio.supports", nativeAudioSupports},
      //JS/ Loads an audio file from a path. Returns a numeric handle.
      //JS/ Returns: number
      //JS/ Args: path (string)
      {"audio.load", nativeAudioLoad},
      //JS/ Loads audio from a binary data buffer with the given format. Returns a numeric handle.
      //JS/ Returns: number
      //JS/ Args: data (string), format (string)
      {"audio.loadBuffer", nativeAudioLoadBuffer},
      //JS/ Starts playback of a previously loaded audio handle.
      //JS/ Returns: stream.OK | stream.ERROR
      //JS/ Args: handle (number)
      {"audio.play", nativeAudioPlay},
      //JS/ Stops playback of a previously loaded audio handle.
      //JS/ Returns: stream.OK | stream.ERROR
      //JS/ Args: handle (number)
      {"audio.stop", nativeAudioStop},
      //JS/ Updates audio playback state for a given handle.
      //JS/ Returns: stream.END | stream.OK
      //JS/ Args: handle (number)
      {"audio.update", nativeAudioUpdate},
      //JS/ Releases resources for a previously loaded audio handle.
      //JS/ Returns: void
      //JS/ Args: handle (number)
      {"audio.release", nativeAudioRelease},
      //JS/ Sets the current font by name string or numeric font ID.
      //JS/ Returns: void
      //JS/ Args: font (string|number)
      {"ui.setFont", nativeUiSetFont},
      //JS/ Returns an array of objects describing available system fonts.
      //JS/ Returns: object[] - { name: string, size: number }
      {"ui.fonts", nativeUiFonts},
      //JS/ Sets the text size multiplier.
      //JS/ Returns: void
      //JS/ Args: size (number)
      {"ui.setTextSize", nativeUiSetTextSize},
      //JS/ Sets the text color (0 = white, 1 = black).
      //JS/ Returns: void
      //JS/ Args: color (number)
      {"ui.setTextColor", nativeUiSetTextColor},
      //JS/ Moves the text cursor to the given x, y position.
      //JS/ Returns: void
      //JS/ Args: x (number), y (number)
      {"ui.setCursor", nativeUiSetCursor},
      //JS/ Prints text at the current cursor position.
      //JS/ Returns: void
      //JS/ Args: text (any)
      {"ui.print", nativeUiPrint},
      //JS/ Draws a single pixel at (x, y) with the given color.
      //JS/ Returns: void
      //JS/ Args: x (number), y (number), color (number)
      {"ui.drawPixel", nativeUiDrawPixel},
      //JS/ Draws a line from (x0, y0) to (x1, y1) with the given color.
      //JS/ Returns: void
      //JS/ Args: x0 (number), y0 (number), x1 (number), y1 (number), color (number)
      {"ui.drawLine", nativeUiDrawLine},
      //JS/ Draws a rectangle outline at (x, y) with the given width, height, and color.
      //JS/ Returns: void
      //JS/ Args: x (number), y (number), w (number), h (number), color (number)
      {"ui.drawRect", nativeUiDrawRect},
      //JS/ Draws a filled rectangle at (x, y) with the given width, height, and color.
      //JS/ Returns: void
      //JS/ Args: x (number), y (number), w (number), h (number), color (number)
      {"ui.fillRect", nativeUiFillRect},
      //JS/ Fills the entire screen with the given color.
      //JS/ Returns: void
      //JS/ Args: color (number)
      {"ui.fillScreen", nativeUiFillScreen},
      //JS/ Draws an image loaded from a file path with optional position and transform options.
      //JS/ Returns: void
      //JS/ Args: path (string), options? (object)
      {"ui.imagePath", nativeUiImagePath},
      //JS/ Draws an image from a binary string buffer with the given format and optional options.
      //JS/ Returns: void
      //JS/ Args: data (string), format (string), options? (object)
      {"ui.imageBuffer", nativeUiImageBuffer},
      //JS/ Draws a vector graphic (SVG) loaded from a file with optional position and transform.
      //JS/ Returns: void
      //JS/ Args: path (string), options? (object)
      {"ui.vectorPath", nativeUiVectorPath},
      //JS/ Draws a vector graphic (SVG) from an inline string with optional position and transform.
      //JS/ Returns: void
      //JS/ Args: svg (string), options? (object)
      {"ui.vectorString", nativeUiVectorString},
      //JS/ Draws a QR code from the given text at the specified position and size.
      //JS/ Returns: void
      //JS/ Args: text (string), x (number), y (number), size (number), color? (number)
      {"ui.qr", nativeUiQr},
      //JS/ Draws a touch button at the given bounds with a text label.
      //JS/ Returns: void
      //JS/ Args: x (number), y (number), w (number), h (number), label (string), pressed? (boolean), textSize? (number)
      {"ui.button", nativeUiButton},
      //JS/ Tests whether a touch point hits a button drawn at the given bounds.
      //JS/ Returns: boolean
      //JS/ Args: x (number), y (number), bx (number), by (number), bw (number), bh (number), padding? (number)
      {"ui.buttonContains", nativeUiButtonContains},
      //JS/ Handles touch interaction for a scrollable list. Returns an object with action, index, and offset.
      //JS/ Returns: object - { action: string, index: number, offset: number }
      //JS/ Args: tx (number), ty (number), itemH (number), visible (number), x (number), y (number), w (number), h (number), mode (number), offset (number)
      {"ui.listTouch", nativeUiListTouch},
      //JS/ Draws a set of selectable option buttons from an array of option objects.
      //JS/ Returns: void
      //JS/ Args: options (array), selected (number), textSize? (number)
      {"ui.selectDraw", nativeUiSelectDraw},
      //JS/ Hits tests a set of selectable option buttons. Returns the value of the hit option or undefined.
      //JS/ Returns: any | undefined
      //JS/ Args: x (number), y (number), options (array), padding? (number)
      {"ui.selectHit", nativeUiSelectHit},
      //JS/ Opens the system on-screen keyboard. Returns a Promise that resolves with the entered text.
      //JS/ Returns: Promise
      //JS/ Args: initialText (string), layout? (number), maxLength? (number)
      {"ui.getKeyboardInput", nativeGetKeyboardInput},
      //JS/ Opens the system date picker. Returns a Promise that resolves with the selected date string.
      //JS/ Returns: Promise
      //JS/ Args: year (number), month (number), day (number)
      {"ui.getDateInput", nativeGetDateInput},
  };
  for (const auto &binding : bindings) {
    if (!runtime->bind(binding.path, binding.function)) {
      return false;
    }
  }
  //JS/ Alias: fs.promises.readFile - Async file read (Promise).
  //JS/ Args: path (string), encoding? (string)
  //JS/ Alias: fs.promises.writeFile - Async file write (Promise).
  //JS/ Args: path (string), data (string), encoding? (string)
  //JS/ Alias: fs.promises.appendFile - Async file append (Promise).
  //JS/ Args: path (string), data (string), encoding? (string)
  //JS/ Alias: fs.promises.readdir - Async directory listing (Promise).
  //JS/ Args: path (string), encoding? (string)
  //JS/ Alias: fs.promises.stat - Async file stat (Promise).
  //JS/ Args: path (string)
  //JS/ Alias: fs.promises.unlink - Async file delete (Promise).
  //JS/ Args: path (string)
  if (!runtime->set("fs.promises.readFile", runtime->get("fs.readFile")) ||
      !runtime->set("fs.promises.writeFile", runtime->get("fs.writeFile")) ||
      !runtime->set("fs.promises.appendFile",
                    runtime->get("fs.appendFile")) ||
      !runtime->set("fs.promises.readdir", runtime->get("fs.readdir")) ||
      !runtime->set("fs.promises.stat", runtime->get("fs.stat")) ||
      !runtime->set("fs.promises.unlink", runtime->get("fs.unlink"))) {
    return false;
  }
  //JS/ Internal: extracts the response body as text from a fetch Response object.
  //JS/ Internal: parses the response body as JSON from a fetch Response object.
  if (!runtime->bindMethod("__responseText", nativeResponseText) ||
      !runtime->bindMethod("__responseJson", nativeResponseJson))
    return false;
  //JS/ Runtime value: screen.width (number) - Screen width in pixels.
  //JS/ Runtime value: screen.height (number) - Screen height in pixels.
  //JS/ Constant: ui.WHITE (0) - White color value.
  //JS/ Constant: ui.BLACK (1) - Black color value.
  //JS/ Constant: ui.FONT_DEFAULT (0) - Default system font.
  //JS/ Constant: ui.FONT_ICON_8 (1) - 8-pixel icon font.
  //JS/ Constant: ui.FONT_ICON_12 (2) - 12-pixel icon font.
  //JS/ Constant: ui.LIST_SCROLL (0) - Scroll list navigation mode.
  //JS/ Constant: ui.LIST_SELECT (1) - Select list navigation mode.
  //JS/ Constant: ui.LIST_SPLIT (2) - Split list navigation mode.
  //JS/ Constant: ui.KEYBOARD_TEXT (0) - Full text keyboard layout.
  //JS/ Constant: ui.KEYBOARD_NUMBERS (1) - Numeric keypad layout.
  //JS/ Constant: ui.KEYBOARD_NUMERIC (2) - Numeric input layout.
  //JS/ Constant: permissions.ERROR (-1) - Operation failed.
  //JS/ Constant: permissions.DENIED (-2) - Permission denied.
  //JS/ Constant: permissions.UNSUPPORTED (-3) - Feature not supported.
  //JS/ Constant: permissions.PENDING (0) - Operation pending.
  //JS/ Constant: permissions.OK (1) - Operation succeeded.
  //JS/ Constant: gpio.INPUT (0) - GPIO digital input mode.
  //JS/ Constant: gpio.INPUT_PULLUP (1) - GPIO input with internal pull-up.
  //JS/ Constant: gpio.OUTPUT (2) - GPIO digital output mode.
  //JS/ Runtime value: app.id (string) - Current PocketInkJS app identifier string.
  //JS/ Runtime value: app.path (string) - File system path of the currently running script.
  //JS/ Constant: stream.END (2) - End of data reached.
  //JS/ Constant: stream.OK (1) - Stream operation succeeded.
  //JS/ Constant: stream.ERROR (-1) - Stream operation failed.
  return runtime->set("screen.width", runtime->number(EPD_WIDTH)) &&
         runtime->set("screen.height", runtime->number(EPD_HEIGHT)) &&
         runtime->set("ui.WHITE", runtime->number(0)) &&
         runtime->set("ui.BLACK", runtime->number(1)) &&
         runtime->set("ui.FONT_DEFAULT", runtime->number(0)) &&
         runtime->set("ui.FONT_ICON_8", runtime->number(1)) &&
         runtime->set("ui.FONT_ICON_12", runtime->number(2)) &&
         runtime->set("ui.LIST_SCROLL", runtime->number(0)) &&
         runtime->set("ui.LIST_SELECT", runtime->number(1)) &&
         runtime->set("ui.LIST_SPLIT", runtime->number(2)) &&
         runtime->set("ui.KEYBOARD_TEXT", runtime->number(0)) &&
         runtime->set("ui.KEYBOARD_NUMBERS", runtime->number(1)) &&
         runtime->set("ui.KEYBOARD_NUMERIC", runtime->number(2)) &&
          runtime->set("permissions.ERROR", runtime->number(-1)) &&
          runtime->set("permissions.DENIED", runtime->number(-2)) &&
          runtime->set("permissions.UNSUPPORTED", runtime->number(-3)) &&
          runtime->set("permissions.PENDING", runtime->number(0)) &&
          runtime->set("permissions.OK", runtime->number(1)) &&
         runtime->set("gpio.INPUT", runtime->number(0)) &&
         runtime->set("gpio.INPUT_PULLUP", runtime->number(1)) &&
         runtime->set("gpio.OUTPUT", runtime->number(2)) &&
           runtime->set("stream.END", runtime->number(2)) &&
           runtime->set("stream.OK", runtime->number(1)) &&
           runtime->set("stream.ERROR", runtime->number(-1)) &&
          runtime->set("app.id", runtime->makeString(appId)) &&
         runtime->set("app.path", runtime->makeString(path));
}

void PocketInkJSApp::load() {
  if (path == nullptr) {
    copyText(status, sizeof(status), "PATH MISSING");
    return;
  }
  File errorCatalog = openProviderPath("sd", "/misc/js_errors.txt");
  hasErrorCatalog = errorCatalog && !errorCatalog.isDirectory();
  errorCatalog.close();
  File file = openProviderPath("sd", path);
  if (!file || file.isDirectory()) {
    copyText(status, sizeof(status), "OPEN FAILED");
    return;
  }
  const size_t sourceBytes = file.size();
  if (sourceBytes > kPocketInkJSMaxSourceBytes) {
    copyText(status, sizeof(status), "SCRIPT TOO LARGE");
    return;
  }
  char *source = static_cast<char *>(allocatePsram(sourceBytes + 1U));
  if (source == nullptr) {
    copyText(status, sizeof(status), "NO SOURCE PSRAM");
    return;
  }
  if (sourceBytes > 0 &&
      file.read(reinterpret_cast<uint8_t *>(source), sourceBytes) != sourceBytes) {
    heap_caps_free(source);
    copyText(status, sizeof(status), "READ FAILED");
    return;
  }
  source[sourceBytes] = '\0';

  workspaceAllocation = allocatePocketInkJSWorkspace(&workspaceBytes);
  if (workspaceAllocation == nullptr) {
    heap_caps_free(source);
    copyText(status, sizeof(status), "NO RUNTIME PSRAM");
    return;
  }
  runtimeStorage = heap_caps_malloc(sizeof(PocketInkJS::Runtime),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (runtimeStorage == nullptr) {
    heap_caps_free(source);
    heap_caps_free(workspaceAllocation);
    workspaceAllocation = nullptr;
    copyText(status, sizeof(status), "NO VM DRAM");
    return;
  }
  PocketInkJS::Options options;
  options.maxBindings = 192;
  options.maxNatives = 96;
  options.maxHostRoots = 48;
  options.maxCallDepth = 24;
  options.maxSteps = 160000;
  options.nowMilliseconds = PocketInkJSNowMilliseconds;
  options.awaitPending = awaitPendingPromise;
  options.awaitUserData = this;
#if POCKETINKJS_USE_LIGHTWEIGHT_JSON_PARSER
  options.jsonParse = parsePocketInkJSJson;
#endif
  runtime = new (runtimeStorage)
      PocketInkJS::Runtime(workspaceAllocation, workspaceBytes, options);
  if (!runtime->ready() || !installBindings()) {
    setRuntimeError();
    heap_caps_free(source);
    releaseRuntime();
    return;
  }
  const PocketInkJS::RunResult evaluated = runtime->eval(source, sourceBytes);
  heap_caps_free(source);
  if (!evaluated.ok) {
    setRuntimeError();
    releaseRuntime();
    return;
  }
  refreshTouchCallbacks();
  status[0] = '\0';
  const PocketInkJS::MemoryStats stats = runtime->memoryStats();
  PocketInkJSLogPrintf("[PocketInkJS] loaded %s workspace=%lu heap=%lu/%lu peak=%lu\n",
                  path, static_cast<unsigned long>(workspaceBytes),
                  static_cast<unsigned long>(stats.heapUsedBytes),
                  static_cast<unsigned long>(stats.heapBytes),
                  static_cast<unsigned long>(stats.peakHeapUsedBytes));
}

PocketInkJSApp PocketInkJSApp;

AppEventResult handleButton(const char *callback) {
  return activePocketInkJSApp != nullptr ? activePocketInkJSApp->button(callback)
                                    : AppEventResult::Unhandled;
}

AppEventResult handleMenu() { return handleButton("onMenu"); }
AppEventResult handleMenuDouble() { return handleButton("onMenuDouble"); }
AppEventResult handleMenuLong() {
  const AppEventResult result = handleButton("onMenuLong");
  return result == AppEventResult::Dirty ? result
                                         : AppEventResult::QuitRequested;
}
AppEventResult handlePower() { return handleButton("onPower"); }
AppEventResult handlePowerDouble() { return handleButton("onPowerDouble"); }
AppEventResult handlePowerLong() { return handleButton("onPowerLong"); }

void pausePocketInkJSApp() { PocketInkJSApp.pause(); }
void resumePocketInkJSApp() { PocketInkJSApp.resume(); }
void releasePocketInkJSApp() { PocketInkJSApp.stop(); }

void handleRawTouch(const TouchEvent &event) {
  if (activePocketInkJSApp != nullptr) {
    activePocketInkJSApp->rawTouch(event);
  }
}

size_t saveContext(uint8_t *buffer, size_t capacity) {
  return activePocketInkJSApp != nullptr ? activePocketInkJSApp->save(buffer, capacity) : 0;
}

void restoreContext(const uint8_t *buffer, size_t length) {
  if (activePocketInkJSApp != nullptr) {
    activePocketInkJSApp->restore(buffer, length);
  }
}

AppBehavior PocketInkJSBehavior() {
  AppBehavior behavior;
  behavior.onEnter = resumePocketInkJSApp;
  behavior.onExit = pausePocketInkJSApp;
  behavior.onMenu = handleMenu;
  behavior.onMenuDouble = handleMenuDouble;
  behavior.onMenuLong = handleMenuLong;
  behavior.onPower = handlePower;
  behavior.onPowerDouble = handlePowerDouble;
  behavior.onPowerLong = handlePowerLong;
  behavior.onRawTouch = handleRawTouch;
  behavior.onSaveContext = saveContext;
  behavior.onRestoreContext = restoreContext;
  behavior.onRelease = releasePocketInkJSApp;
  return behavior;
}

} // namespace

ActiveApp *PocketInkJSAppRuntime() { return &PocketInkJSApp; }

bool PocketInkJSAppLaunchPath(const char *path, AppDefinition *out, char *id,
                         size_t idSize, char *label, size_t labelSize,
                         char *icon, size_t iconSize) {
  if (path == nullptr || path[0] == '\0' || out == nullptr || id == nullptr ||
      label == nullptr || icon == nullptr || idSize == 0 || labelSize == 0 ||
      iconSize == 0) {
    return false;
  }
  copyLabel(label, labelSize, path);
  snprintf(id, idSize, "js:%08lx",
           static_cast<unsigned long>(hashPath(path)));
  icon[0] = 'J';
  if (iconSize > 1) {
    icon[1] = '\0';
  }
  PocketInkJSApp.launch(path, id);

  *out = AppDefinition{};
  out->id = id;
  out->label = label;
  out->icon = icon;
  out->category = MENU_APPS;
  out->runtime = PocketInkJSAppRuntime();
  out->visible = sdStorageMounted;
  out->behavior = PocketInkJSBehavior();
  return true;
}

void PocketInkJSAppStop() { PocketInkJSApp.stop(); }
