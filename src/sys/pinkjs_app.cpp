#include "sys/pinkjs_app.h"

#include "fs/file_provider.h"
#include "fs/providers/image_file_viewer.h"
#include "fs/providers/svg_file_viewer.h"
#include "sys/services/connectivity_service.h"
#include "sys/app_api_services.h"
#include "sys/services/device_clock.h"
#include "sys/global.h"
#include "sys/services/lockscreen_guard.h"
#include "sys/psram_allocator.h"
#ifndef PINKJS_USE_LIGHTWEIGHT_JSON_PARSER
#define PINKJS_USE_LIGHTWEIGHT_JSON_PARSER 0
#endif
#if PINKJS_USE_LIGHTWEIGHT_JSON_PARSER
#define PINKJS_DISABLE_BUILTIN_JSON_PARSER 1
#include "netapps/lightweight_json_parser.h"
#endif
#include "sys/runtime/pinkjs/pinkjs.h"
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
#define PINKJS_HAS_ROM_PRINTF 1
#else
#define PINKJS_HAS_ROM_PRINTF 0
#endif

namespace {

constexpr size_t kPinkJsMinWorkspaceBytes = 160U * 1024U;
constexpr size_t kPinkJsPsramReserveBytes = 512U * 1024U;
constexpr size_t kPinkJsAllocationHeadroomBytes = 32U * 1024U;
constexpr size_t kPinkJsMaxSourceBytes = 96U * 1024U;
constexpr size_t kPinkJsMaxHttpBytes = 64U * 1024U;
constexpr uint32_t kPinkJsHttpTimeoutMs = 8000;
constexpr uint8_t kPinkJsMaxTimers = 16;
constexpr size_t kPinkJsJobsPerUpdate = 32;
constexpr uint8_t kPinkJsFetchQueueDepth = 3;
constexpr uint32_t kPinkJsWorkerStackBytes = 7168;
constexpr uint32_t kPinkJsExecutorStackBytes = 40U * 1024U;
constexpr UBaseType_t kPinkJsWorkerPriority = 1;
constexpr BaseType_t kPinkJsWorkerCore = 0;
constexpr UBaseType_t kPinkJsExecutorPriority = 1;
constexpr BaseType_t kPinkJsExecutorCore = 1;
constexpr size_t kIdCapacity = 20;
constexpr size_t kStatusCapacity = 96;

class PinkJsApp;

using PinkJsExecutorOperation = void (*)(PinkJsApp &, void *);

struct PinkJsExecutorCommand {
  PinkJsExecutorOperation operation = nullptr;
  void *context = nullptr;
};

enum PinkJsTouchCallback : uint8_t {
  PinkJsTouchGeneric = 1U << 0U,
  PinkJsTouchDown = 1U << 1U,
  PinkJsTouchMove = 1U << 2U,
  PinkJsTouchUp = 1U << 3U,
};

struct FetchHeader {
  char name[48] = {};
  char value[160] = {};
};

struct FetchRequest {
  pinkjs::RootHandle promise = pinkjs::kInvalidRoot;
  char url[512] = {};
  char method[12] = "GET";
  uint8_t *body = nullptr;
  size_t bodyBytes = 0;
  size_t maxBytes = kPinkJsMaxHttpBytes;
  uint32_t timeoutMs = kPinkJsHttpTimeoutMs;
  FetchHeader headers[6] = {};
  uint8_t headerCount = 0;
};

struct FetchCompletion {
  pinkjs::RootHandle promise = pinkjs::kInvalidRoot;
  char url[512] = {};
  uint8_t *body = nullptr;
  size_t bodyBytes = 0;
  int statusCode = 0;
  pinkjs::ErrorCode error = {};
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
  pinkjs::RootHandle callback = pinkjs::kInvalidRoot;
  pinkjs::RootHandle arguments = pinkjs::kInvalidRoot;
  bool active = false;
};

struct TimerState {
  TimerSlot slots[kPinkJsMaxTimers] = {};
  uint32_t nextId = 1;
};

enum class PinkJsSystemModal : uint8_t {
  None,
  FilePicker,
  Keyboard,
  DatePicker,
  AudioCapture,
  AudioCapturePcm16,
};

bool activeSystemModalIdle();
bool beginActiveSystemModal(PinkJsSystemModal kind, pinkjs::Value promise,
                            uint32_t captureDurationMs = 0,
                            DatePickerDate initialDate = {});

void setFetchError(FetchCompletion *completion, pinkjs::ErrorCode error) {
  completion->error = error;
}

PinkJsApp *activePinkJsApp = nullptr;
Adafruit_GFX *pinkJsDrawTarget = nullptr;
uint8_t pinkJsSleepGuardDepth = 0;
UBaseType_t pinkJsLowestStackHwm = UINT_MAX;
char *pinkJsLogBuffer = nullptr;

void pinkJsLogWrite(const char *text) {
#if PINKJS_HAS_ROM_PRINTF
  esp_rom_printf("%s", text != nullptr ? text : "");
#else
  std::fputs(text != nullptr ? text : "", stdout);
#endif
}

void pinkJsLogPrintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
#if PINKJS_HAS_ROM_PRINTF
  esp_rom_vprintf(format, args);
#else
  std::vprintf(format, args);
#endif
  va_end(args);
}

void logPinkJsStackLow(const char *callback) {
#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && INCLUDE_uxTaskGetStackHighWaterMark
  const UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(nullptr);
  if (highWaterMark >= pinkJsLowestStackHwm)
    return;
  pinkJsLowestStackHwm = highWaterMark;
  pinkJsLogPrintf("[pinkjs] stack low callback=%s hwm=%u\n",
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

void drawPinkJsCentered(Adafruit_GFX &gfx, const char *text, int16_t y,
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

void drawPinkJsWrappedCentered(Adafruit_GFX &gfx, const char *text,
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
    drawPinkJsCentered(gfx, line, y, 1);
    y += 14;
    at = end;
  }
}

bool pinkJsErrorNumber(const char *message, uint8_t *number) {
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

void *allocatePinkJsWorkspace(size_t *allocatedBytes) {
  if (allocatedBytes == nullptr)
    return nullptr;
  *allocatedBytes = 0;
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t freeBytes = heap_caps_get_free_size(caps);
  const size_t largestBlock = heap_caps_get_largest_free_block(caps);
  // Leave at least 512 KiB, or 25% on larger devices, for fetch bodies,
  // component state, TLS, and other active services.
  const size_t reserve = freeBytes / 4U > kPinkJsPsramReserveBytes
                             ? freeBytes / 4U
                             : kPinkJsPsramReserveBytes;
  const size_t freeBudget = freeBytes > reserve ? freeBytes - reserve : 0;
  const size_t blockBudget =
      largestBlock > kPinkJsAllocationHeadroomBytes
          ? largestBlock - kPinkJsAllocationHeadroomBytes
          : 0;
  size_t candidate = freeBudget < blockBudget ? freeBudget : blockBudget;
  candidate &= ~size_t(4095U);
  if (candidate < kPinkJsMinWorkspaceBytes)
    candidate = kPinkJsMinWorkspaceBytes;

  while (candidate >= kPinkJsMinWorkspaceBytes) {
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
    setFetchError(completion, pinkjs::ErrorCode{190});
    return completion;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(request->timeoutMs);
  if (!http.begin(client, request->url)) {
    setFetchError(completion, pinkjs::ErrorCode{193});
    return completion;
  }
  http.addHeader("User-Agent", "PocketInk-PinkJS/0.2");
  for (uint8_t i = 0; i < request->headerCount; i++)
    http.addHeader(request->headers[i].name, request->headers[i].value);
  const int code = strcmp(request->method, "GET") == 0
                       ? http.GET()
                       : http.sendRequest(request->method, request->body,
                                          request->bodyBytes);
  completion->statusCode = code;
  if (code <= 0) {
    http.end();
    setFetchError(completion, pinkjs::ErrorCode{194});
    return completion;
  }
  const int expected = http.getSize();
  if (expected > 0 && static_cast<size_t>(expected) > request->maxBytes) {
    http.end();
    setFetchError(completion, pinkjs::ErrorCode{195});
    return completion;
  }
  const size_t allocationBytes =
      expected > 0 ? static_cast<size_t>(expected) : request->maxBytes;
  if (allocationBytes > 0) {
    completion->body = static_cast<uint8_t *>(allocatePsram(allocationBytes));
    if (completion->body == nullptr) {
      http.end();
      setFetchError(completion, pinkjs::ErrorCode{196});
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
    setFetchError(completion, pinkjs::ErrorCode{197});
  }
#else
  (void)stopping;
  setFetchError(completion, pinkjs::ErrorCode{198});
#endif
  return completion;
}

void pinkJsWorker(void *context) {
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
  while (pinkJsSleepGuardDepth > 0) {
    pinkJsSleepGuardDepth--;
    lockscreenGuardRelease();
  }
}

bool numericArguments(pinkjs::Runtime &runtime, const pinkjs::Value *args,
                      size_t argc, size_t expected) {
  if (argc != expected) {
    runtime.fail(pinkjs::ErrorCode{157});
    return false;
  }
  for (size_t i = 0; i < argc; i++) {
    if (runtime.typeOf(args[i]) != pinkjs::ValueType::Number ||
        !std::isfinite(runtime.toNumber(args[i]))) {
      runtime.fail(pinkjs::ErrorCode{158});
      return false;
    }
  }
  return true;
}

bool copyJsString(pinkjs::Runtime &runtime, pinkjs::Value value, char *out,
                  size_t outSize) {
  if (!runtime.isString(value) || out == nullptr || outSize == 0) {
    return false;
  }
  const pinkjs::StringView text = runtime.stringView(value);
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

pinkjs::Value nativeConsoleLog(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc, void *) {
  if (pinkJsLogBuffer == nullptr) {
    pinkJsLogBuffer = static_cast<char *>(allocatePsram(160));
  }
  if (pinkJsLogBuffer == nullptr) {
    pinkJsLogWrite("[pinkjs] [log buffer unavailable]\n");
    return runtime.undefined();
  }
  pinkJsLogWrite("[pinkjs]");
  for (size_t i = 0; i < argc; i++) {
    runtime.format(args[i], pinkJsLogBuffer, 160);
    pinkJsLogWrite(" ");
    pinkJsLogWrite(pinkJsLogBuffer);
  }
  pinkJsLogWrite("\n");
  return runtime.undefined();
}

pinkjs::Value nativeMillis(pinkjs::Runtime &runtime, const pinkjs::Value *,
                           size_t argc, void *) {
  if (argc != 0) {
    return runtime.fail(pinkjs::ErrorCode{159});
  }
  return runtime.number(static_cast<double>(millis()));
}

pinkjs::Value nativePinkJsHeapUsed(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *, size_t argc,
                                   void *) {
  return argc == 0
             ? runtime.number(
                   static_cast<double>(runtime.memoryStats().heapUsedBytes))
             : runtime.fail(pinkjs::ErrorCode{157});
}

pinkjs::Value nativePinkJsHeapFree(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *, size_t argc,
                                   void *) {
  return argc == 0
             ? runtime.number(
                   static_cast<double>(runtime.memoryStats().heapFreeBytes))
             : runtime.fail(pinkjs::ErrorCode{157});
}

pinkjs::Value nativePinkJsHeapPeak(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *, size_t argc,
                                   void *) {
  return argc == 0
             ? runtime.number(static_cast<double>(
                   runtime.memoryStats().peakHeapUsedBytes))
             : runtime.fail(pinkjs::ErrorCode{157});
}

pinkjs::Value nativeNetworkConnected(pinkjs::Runtime &runtime,
                                     const pinkjs::Value *, size_t argc,
                                     void *) {
  if (argc != 0) {
    return runtime.fail(pinkjs::ErrorCode{160});
  }
  return runtime.boolean(wifiIsConnected());
}

pinkjs::Value nativeSetWakeLock(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  if (argc != 1) {
    return runtime.fail(pinkjs::ErrorCode{162});
  }
  if (runtime.toBoolean(args[0])) {
    if (pinkJsSleepGuardDepth < 255U) {
      pinkJsSleepGuardDepth++;
      lockscreenGuardAcquire();
    }
  } else if (pinkJsSleepGuardDepth > 0U) {
    pinkJsSleepGuardDepth--;
    lockscreenGuardRelease();
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiSetFont(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
  if (argc != 1)
    return runtime.fail(pinkjs::ErrorCode{157});
  if (pinkJsDrawTarget == nullptr) {
    return runtime.undefined();
  }
  if (runtime.isString(args[0])) {
    char name[32];
    return copyJsString(runtime, args[0], name, sizeof(name)) &&
                   systemFontApply(*pinkJsDrawTarget, name)
               ? runtime.boolean(true)
               : runtime.boolean(false);
  }
  if (!runtime.isNumber(args[0]) ||
      !std::isfinite(runtime.toNumber(args[0])))
    return runtime.fail(pinkjs::ErrorCode{158});
  switch (toI16(runtime.toNumber(args[0]))) {
  case 1:
    systemFontApply(*pinkJsDrawTarget, size_t{1});
    break;
  case 2:
    systemFontApply(*pinkJsDrawTarget, size_t{2});
    break;
  default:
    systemFontApply(*pinkJsDrawTarget, size_t{0});
    break;
  }
  return runtime.boolean(true);
}

pinkjs::Value nativeUiSetTextSize(pinkjs::Runtime &runtime,
                                  const pinkjs::Value *args, size_t argc,
                                  void *) {
  if (numericArguments(runtime, args, argc, 1) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->setTextSize(toU8(runtime.toNumber(args[0])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiSetTextColor(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *args, size_t argc,
                                   void *) {
  if (numericArguments(runtime, args, argc, 1) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->setTextColor(toU16(runtime.toNumber(args[0])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiSetCursor(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  if (numericArguments(runtime, args, argc, 2) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->setCursor(toI16(runtime.toNumber(args[0])),
                                toI16(runtime.toNumber(args[1])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiPrint(pinkjs::Runtime &runtime,
                            const pinkjs::Value *args, size_t argc, void *) {
  if (argc != 1) {
    return runtime.fail(pinkjs::ErrorCode{163});
  }
  if (pinkJsDrawTarget == nullptr) {
    return runtime.undefined();
  }
  if (runtime.isString(args[0])) {
    const pinkjs::StringView text = runtime.stringView(args[0]);
    for (size_t i = 0; i < text.size; i++) {
      char character[2] = {text.data[i], '\0'};
      pinkJsDrawTarget->print(character);
    }
  } else {
    char text[96];
    runtime.format(args[0], text, sizeof(text));
    pinkJsDrawTarget->print(text);
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiDrawPixel(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  if (numericArguments(runtime, args, argc, 3) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->drawPixel(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toU16(runtime.toNumber(args[2])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiDrawLine(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc, void *) {
  if (numericArguments(runtime, args, argc, 5) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->drawLine(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toI16(runtime.toNumber(args[2])), toI16(runtime.toNumber(args[3])),
        toU16(runtime.toNumber(args[4])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiDrawRect(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc, void *) {
  if (numericArguments(runtime, args, argc, 5) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->drawRect(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toI16(runtime.toNumber(args[2])), toI16(runtime.toNumber(args[3])),
        toU16(runtime.toNumber(args[4])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiFillRect(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc, void *) {
  if (numericArguments(runtime, args, argc, 5) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->fillRect(
        toI16(runtime.toNumber(args[0])), toI16(runtime.toNumber(args[1])),
        toI16(runtime.toNumber(args[2])), toI16(runtime.toNumber(args[3])),
        toU16(runtime.toNumber(args[4])));
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiFillScreen(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  if (numericArguments(runtime, args, argc, 1) && pinkJsDrawTarget != nullptr) {
    pinkJsDrawTarget->fillScreen(toU16(runtime.toNumber(args[0])));
  }
  return runtime.undefined();
}

pinkjs::Value apiStatusValue(pinkjs::Runtime &runtime, AppApiStatus status) {
  return runtime.number(static_cast<int8_t>(status));
}

double imageOptionNumber(pinkjs::Runtime &runtime, pinkjs::Value options,
                         const char *key, double fallback) {
  if (runtime.typeOf(options) != pinkjs::ValueType::Object) {
    return fallback;
  }
  const pinkjs::Value value = runtime.objectGet(options, key);
  return runtime.isNumber(value) && std::isfinite(runtime.toNumber(value))
             ? runtime.toNumber(value)
             : fallback;
}

bool imageOptions(pinkjs::Runtime &runtime, pinkjs::Value options, int16_t &x,
                  int16_t &y, int16_t &w, int16_t &h, uint8_t &dither,
                  bool &fit, ImageTransform &transform) {
  if (runtime.typeOf(options) != pinkjs::ValueType::Undefined &&
      runtime.typeOf(options) != pinkjs::ValueType::Object) {
    return false;
  }
  x = toI16(imageOptionNumber(runtime, options, "x", 0));
  y = toI16(imageOptionNumber(runtime, options, "y", 0));
  w = toI16(imageOptionNumber(runtime, options, "width", EPD_WIDTH));
  h = toI16(imageOptionNumber(runtime, options, "height", EPD_HEIGHT));
  dither = toU8(imageOptionNumber(runtime, options, "dither", 1));
  if (runtime.typeOf(options) == pinkjs::ValueType::Object) {
    const pinkjs::Value fitValue = runtime.objectGet(options, "scaleToFit");
    fit = runtime.typeOf(fitValue) == pinkjs::ValueType::Undefined
              ? true
              : runtime.toBoolean(fitValue);
    const pinkjs::Value matrix = runtime.objectGet(options, "matrix");
    if (runtime.typeOf(matrix) == pinkjs::ValueType::Array) {
      if (runtime.arrayLength(matrix) != 6) {
        return false;
      }
      float *values[] = {&transform.a, &transform.b, &transform.c,
                         &transform.d, &transform.tx, &transform.ty};
      for (size_t i = 0; i < 6; i++) {
        const pinkjs::Value value = runtime.arrayAt(matrix, i);
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

pinkjs::Value nativeUiImagePath(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail("ui.imagePath expects path and optional options");
  }
  if (pinkJsDrawTarget == nullptr) {
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
      *pinkJsDrawTarget, "sd", path, x, y, w, h, dither, fit, transform));
}

pinkjs::Value nativeUiImageBuffer(pinkjs::Runtime &runtime,
                                  const pinkjs::Value *args, size_t argc,
                                  void *) {
  if (argc < 2 || argc > 3 || !runtime.isString(args[0]) ||
      !runtime.isString(args[1])) {
    return runtime.fail(
        "ui.imageBuffer expects binary string, format, and options");
  }
  if (pinkJsDrawTarget == nullptr) {
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
  const pinkjs::StringView data = runtime.stringView(args[0]);
  return runtime.boolean(drawImageBufferTransformed(
      *pinkJsDrawTarget, reinterpret_cast<const uint8_t *>(data.data),
      data.size, format, x, y, w, h, dither, fit, transform));
}

pinkjs::Value nativeUiVectorPath(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail("ui.vectorPath expects path and optional options");
  }
  if (pinkJsDrawTarget == nullptr) {
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
      *pinkJsDrawTarget, "sd", path, x, y, w, h, transform));
}

pinkjs::Value nativeUiVectorString(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *args, size_t argc,
                                   void *) {
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail("ui.vectorString expects SVG text and optional options");
  }
  if (pinkJsDrawTarget == nullptr) {
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
  const pinkjs::StringView svg = runtime.stringView(args[0]);
  return runtime.boolean(drawVectorStringTransformed(
      *pinkJsDrawTarget, svg.data, svg.size, x, y, w, h, transform));
}

pinkjs::Value readPinkJsFile(pinkjs::Runtime &runtime,
                             const pinkjs::Value *args, size_t argc) {
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
  pinkjs::Value result = runtime.makeObject();
  pinkjs::Value data = runtime.makeString(
      reinterpret_cast<const char *>(buffer), bytes);
  psram::release(buffer);
  runtime.objectSet(result, "data", data);
  runtime.objectSet(result, "bytes", runtime.number(bytes));
  runtime.objectSet(result, "size", runtime.number(fileSize));
  runtime.objectSet(result, "offset", runtime.number(offset));
  runtime.objectSet(result, "eof",
                    runtime.boolean(status == AppApiStatus::End));
  return result;
}

pinkjs::Value nativeFilesReadText(pinkjs::Runtime &runtime,
                                  const pinkjs::Value *args, size_t argc,
                                  void *) {
  return readPinkJsFile(runtime, args, argc);
}

pinkjs::Value nativeFilesReadBinary(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  return readPinkJsFile(runtime, args, argc);
}

bool jsStringEquals(pinkjs::Runtime &runtime, pinkjs::Value value,
                    const char *expected) {
  if (!runtime.isString(value) || expected == nullptr) {
    return false;
  }
  const pinkjs::StringView text = runtime.stringView(value);
  const size_t length = strlen(expected);
  return text.size == length &&
         (length == 0 || memcmp(text.data, expected, length) == 0);
}

bool fsEncodingSupported(pinkjs::Runtime &runtime, pinkjs::Value value) {
  return runtime.isUndefined(value) || jsStringEquals(runtime, value, "utf8") ||
         jsStringEquals(runtime, value, "utf-8") ||
         jsStringEquals(runtime, value, "binary") ||
         jsStringEquals(runtime, value, "buffer");
}

pinkjs::Value resolvedPromise(pinkjs::Runtime &runtime,
                              pinkjs::Value result) {
  if (runtime.error().message[0] != '\0') {
    return runtime.rejectCurrentError();
  }
  pinkjs::Value promise = runtime.makePromise();
  if (!runtime.isUndefined(promise)) {
    runtime.settlePromise(promise, result, false);
  }
  return promise;
}

struct PinkJsDirectoryContext {
  pinkjs::Runtime *runtime = nullptr;
  pinkjs::Value entries = {};
  size_t count = 0;
  bool truncated = false;
};

bool appendPinkJsDirectoryEntry(const char *name, bool, uint32_t, void *raw) {
  PinkJsDirectoryContext *context =
      static_cast<PinkJsDirectoryContext *>(raw);
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

pinkjs::Value nativeFsReadFileSync(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *args, size_t argc,
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
  if ((status != AppApiStatus::Ok && status != AppApiStatus::End) ||
      bytes != fileSize) {
    psram::release(buffer);
    return runtime.fail("file read failed");
  }
  const pinkjs::Value result = runtime.makeString(
      reinterpret_cast<const char *>(buffer), fileSize);
  psram::release(buffer);
  return result;
}

pinkjs::Value nativeFsExistsSync(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  char path[288];
  if (argc != 1 || !copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("fs.existsSync expects a file path");
  }
  return runtime.boolean(appApiReadFile(path, 0, nullptr, 0, nullptr,
                                        nullptr) != AppApiStatus::Error);
}

pinkjs::Value nativeFsStatSync(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc,
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
  pinkjs::Value result = runtime.makeObject();
  runtime.objectSet(result, "size", runtime.number(fileSize));
  runtime.objectSet(result, "isFile", runtime.boolean(true));
  runtime.objectSet(result, "isDirectory", runtime.boolean(false));
  return result;
}

pinkjs::Value nativeFsReaddirSync(pinkjs::Runtime &runtime,
                                  const pinkjs::Value *args, size_t argc,
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
  PinkJsDirectoryContext context;
  context.runtime = &runtime;
  context.entries = runtime.makeArray();
  const AppApiStatus status = appApiListDirectory(
      path, appendPinkJsDirectoryEntry, &context);
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

pinkjs::Value fsWriteFile(pinkjs::Runtime &runtime,
                          const pinkjs::Value *args, size_t argc,
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
  const pinkjs::StringView data = runtime.stringView(args[1]);
  return apiStatusValue(
      runtime,
      appApiWriteFile(path, reinterpret_cast<const uint8_t *>(data.data),
                      data.size, append));
}

pinkjs::Value nativeFsWriteFileSync(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  return fsWriteFile(runtime, args, argc, false);
}

pinkjs::Value nativeFsAppendFileSync(pinkjs::Runtime &runtime,
                                     const pinkjs::Value *args, size_t argc,
                                     void *) {
  return fsWriteFile(runtime, args, argc, true);
}

pinkjs::Value nativeFsUnlinkSync(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  char path[288];
  return argc == 1 && copyJsString(runtime, args[0], path, sizeof(path))
             ? apiStatusValue(runtime, appApiDeleteFile(path))
             : runtime.fail("fs.unlinkSync expects a file path");
}

pinkjs::Value nativeFsReadFile(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc,
                               void *userData) {
  return resolvedPromise(
      runtime, nativeFsReadFileSync(runtime, args, argc, userData));
}

pinkjs::Value nativeFsWriteFile(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *userData) {
  return resolvedPromise(
      runtime, nativeFsWriteFileSync(runtime, args, argc, userData));
}

pinkjs::Value nativeFsAppendFile(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *userData) {
  return resolvedPromise(
      runtime, nativeFsAppendFileSync(runtime, args, argc, userData));
}

pinkjs::Value nativeFsReaddir(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc,
                              void *userData) {
  return resolvedPromise(
      runtime, nativeFsReaddirSync(runtime, args, argc, userData));
}

pinkjs::Value nativeFsStat(pinkjs::Runtime &runtime,
                           const pinkjs::Value *args, size_t argc,
                           void *userData) {
  return resolvedPromise(runtime,
                         nativeFsStatSync(runtime, args, argc, userData));
}

pinkjs::Value nativeFsUnlink(pinkjs::Runtime &runtime,
                             const pinkjs::Value *args, size_t argc,
                             void *userData) {
  return resolvedPromise(runtime,
                         nativeFsUnlinkSync(runtime, args, argc, userData));
}

pinkjs::Value nativeFilesWrite(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc,
                               void *) {
  if (argc < 2 || argc > 3 || !runtime.isString(args[0]) ||
      !runtime.isString(args[1])) {
    return runtime.fail("files.write expects path, data, append?");
  }
  char path[288];
  if (!copyJsString(runtime, args[0], path, sizeof(path))) {
    return runtime.fail("file path is too long");
  }
  const pinkjs::StringView data = runtime.stringView(args[1]);
  return apiStatusValue(
      runtime,
      appApiWriteFile(path, reinterpret_cast<const uint8_t *>(data.data),
                      data.size, argc == 3 && runtime.toBoolean(args[2])));
}

pinkjs::Value nativeFilesPick(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
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
  pinkjs::Value promise = runtime.makePromise();
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
      !beginActiveSystemModal(PinkJsSystemModal::FilePicker, promise)) {
    runtime.settlePromise(promise, runtime.makeString("file picker is busy"),
                          true);
  }
  return promise;
}

pinkjs::Value nativeFilesDelete(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  char path[288];
  return argc == 1 && copyJsString(runtime, args[0], path, sizeof(path))
             ? apiStatusValue(runtime, appApiDeleteFile(path))
             : runtime.fail("files.delete expects a path");
}

pinkjs::Value nativeAppOpen(pinkjs::Runtime &runtime,
                            const pinkjs::Value *args, size_t argc, void *) {
  char appId[24];
  return argc == 1 && copyJsString(runtime, args[0], appId, sizeof(appId))
             ? runtime.boolean(appApiRequestOpenApp(appId))
             : runtime.fail("app.open expects an app id");
}

pinkjs::Value nativeGpioMode(pinkjs::Runtime &runtime,
                             const pinkjs::Value *args, size_t argc, void *) {
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

pinkjs::Value nativeGpioRead(pinkjs::Runtime &runtime,
                             const pinkjs::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 1)) {
    return runtime.undefined();
  }
  const double pin = runtime.toNumber(args[0]);
  if (pin < 0 || pin > UINT8_MAX || std::floor(pin) != pin)
    return runtime.fail("invalid GPIO pin");
  int value = 0;
  const AppApiStatus status =
      appApiGpioRead(toU8(pin), &value);
  pinkjs::Value result = runtime.makeObject();
  runtime.objectSet(result, "status", apiStatusValue(runtime, status));
  runtime.objectSet(result, "value", runtime.number(value));
  return result;
}

pinkjs::Value nativeGpioWrite(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
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

pinkjs::Value nativeSensorsRead(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  if (argc > 1) {
    return runtime.fail("sensors.read expects refresh?");
  }
  const EnvironmentSnapshot snapshot =
      appApiSensorSnapshot(argc == 1 && runtime.toBoolean(args[0]));
  pinkjs::Value result = runtime.makeObject();
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

pinkjs::Value nativeNetworkScan(pinkjs::Runtime &runtime,
                                const pinkjs::Value *, size_t argc, void *) {
  return argc == 0 ? apiStatusValue(runtime, appApiWifiScan())
                   : runtime.fail("network.scan expects no arguments");
}

pinkjs::Value nativeNetworkNetworks(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *, size_t argc,
                                    void *) {
  if (argc != 0) {
    return runtime.fail("network.networks expects no arguments");
  }
  pinkjs::Value result = runtime.makeArray(appApiWifiNetworkCount());
  for (size_t i = 0; i < appApiWifiNetworkCount(); i++) {
    AppApiWifiNetwork network;
    if (!appApiWifiNetworkAt(i, network)) {
      continue;
    }
    pinkjs::Value item = runtime.makeObject();
    runtime.objectSet(item, "ssid", runtime.makeString(network.ssid));
    runtime.objectSet(item, "rssi", runtime.number(network.rssi));
    runtime.arrayPush(result, item);
  }
  return result;
}

pinkjs::Value nativeUiFonts(pinkjs::Runtime &runtime, const pinkjs::Value *,
                            size_t argc, void *) {
  if (argc != 0) {
    return runtime.fail("ui.fonts expects no arguments");
  }
  pinkjs::Value result = runtime.makeArray(systemFontCount());
  for (size_t i = 0; i < systemFontCount(); i++) {
    SystemFontInfo info;
    if (!systemFontInfo(i, info)) {
      continue;
    }
    pinkjs::Value item = runtime.makeObject();
    runtime.objectSet(item, "name", runtime.makeString(info.name));
    runtime.objectSet(item, "size", runtime.number(info.nominalSize));
    runtime.arrayPush(result, item);
  }
  return result;
}

pinkjs::Value nativeAudioCapture(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 bool wav) {
  if (!numericArguments(runtime, args, argc, 1)) {
    return runtime.undefined();
  }
  const double duration = runtime.toNumber(args[0]);
  if (duration < 1 || duration > 60000 || std::floor(duration) != duration) {
    return runtime.fail("invalid capture duration");
  }
  pinkjs::Value promise = runtime.makePromise();
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
      !beginActiveSystemModal(wav ? PinkJsSystemModal::AudioCapture
                                  : PinkJsSystemModal::AudioCapturePcm16,
                              promise, durationMs)) {
    runtime.settlePromise(promise, runtime.makeString("audio capture is busy"),
                          true);
  }
  return promise;
}

pinkjs::Value nativeAudioCaptureWav(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  return nativeAudioCapture(runtime, args, argc, true);
}

pinkjs::Value nativeAudioCapturePcm16(pinkjs::Runtime &runtime,
                                      const pinkjs::Value *args, size_t argc,
                                      void *) {
  return nativeAudioCapture(runtime, args, argc, false);
}

pinkjs::Value nativeAudioLoad(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
  char path[288];
  return argc == 1 && copyJsString(runtime, args[0], path, sizeof(path))
             ? runtime.number(appApiAudioLoadPath(path))
             : runtime.fail("audio.load expects a path");
}

pinkjs::Value nativeAudioSupports(pinkjs::Runtime &runtime,
                                  const pinkjs::Value *args, size_t argc,
                                  void *) {
  char format[24];
  return argc == 1 && copyJsString(runtime, args[0], format, sizeof(format))
             ? runtime.boolean(appApiAudioFormatSupported(format))
             : runtime.fail("audio.supports expects a format");
}

pinkjs::Value nativeAudioLoadBuffer(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  if (argc != 2 || !runtime.isString(args[0]) || !runtime.isString(args[1])) {
    return runtime.fail("audio.loadBuffer expects data and format");
  }
  char format[24];
  if (!copyJsString(runtime, args[1], format, sizeof(format))) {
    return runtime.fail("audio format is too long");
  }
  const pinkjs::StringView data = runtime.stringView(args[0]);
  return runtime.number(appApiAudioLoadBuffer(
      reinterpret_cast<const uint8_t *>(data.data), data.size, format));
}

pinkjs::Value nativeAudioPlay(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  const double handle = runtime.toNumber(args[0]);
  return handle >= 1 && handle <= UINT32_MAX && std::floor(handle) == handle
             ? apiStatusValue(runtime,
                              appApiAudioPlay(static_cast<uint32_t>(handle)))
             : runtime.fail("invalid audio handle");
}

pinkjs::Value nativeAudioStop(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
  if (!numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  const double handle = runtime.toNumber(args[0]);
  return handle >= 1 && handle <= UINT32_MAX && std::floor(handle) == handle
             ? apiStatusValue(runtime,
                              appApiAudioStop(static_cast<uint32_t>(handle)))
             : runtime.fail("invalid audio handle");
}

pinkjs::Value nativeAudioUpdate(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  if (!numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  const double handle = runtime.toNumber(args[0]);
  return handle >= 1 && handle <= UINT32_MAX && std::floor(handle) == handle
             ? apiStatusValue(runtime,
                              appApiAudioUpdate(static_cast<uint32_t>(handle)))
             : runtime.fail("invalid audio handle");
}

pinkjs::Value nativeAudioRelease(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
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

pinkjs::Value nativeUiQr(pinkjs::Runtime &runtime, const pinkjs::Value *args,
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
  if (pinkJsDrawTarget == nullptr) {
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
  qr.draw(*pinkJsDrawTarget, toI16(runtime.toNumber(args[1])),
          toI16(runtime.toNumber(args[2])),
          toI16(runtime.toNumber(args[3])),
          argc == 5 ? toU16(runtime.toNumber(args[4])) : 1);
  return runtime.boolean(true);
}

int64_t pinkJsNowMilliseconds(void *) {
  int64_t seconds = 0;
  uint16_t milliseconds = 0;
  if (deviceClock.snapshotUnixMillis(seconds, milliseconds)) {
    return seconds * INT64_C(1000) + milliseconds;
  }
  return static_cast<int64_t>(millis());
}

#if PINKJS_USE_LIGHTWEIGHT_JSON_PARSER
class PinkJsJsonStream : public Stream {
public:
  explicit PinkJsJsonStream(pinkjs::StringView input) : input(input) {}

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
  pinkjs::StringView input;
  size_t position = 0;
};

class PinkJsJsonListener : public JsonStreamListener {
public:
  explicit PinkJsJsonListener(pinkjs::Runtime &runtime) : runtime(runtime) {}

  pinkjs::Value result() const { return root; }
  bool valid() const { return hasRoot; }

  void onObjectStart(int depth, const char *key) override {
    pinkjs::Value object = runtime.makeObject();
    attachContainer(depth, key, object);
  }
  void onArrayStart(int depth, const char *key) override {
    pinkjs::Value array = runtime.makeArray();
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
  pinkjs::Runtime &runtime;
  pinkjs::Value root = {};
  pinkjs::Value containers[20] = {};
  bool hasRoot = false;

  void append(pinkjs::Value parent, const char *key, pinkjs::Value value) {
    if (runtime.typeOf(parent) == pinkjs::ValueType::Array)
      runtime.arrayPush(parent, value);
    else
      runtime.objectSet(parent, key != nullptr ? key : "", value);
  }
  void attachContainer(int depth, const char *key, pinkjs::Value value) {
    if (depth <= 0 || depth > static_cast<int>(sizeof(containers) /
                                               sizeof(containers[0]))) {
      runtime.fail(pinkjs::ErrorCode{164});
      return;
    }
    if (depth == 1) {
      root = value;
      hasRoot = true;
    } else
      append(containers[depth - 2], key, value);
    containers[depth - 1] = value;
  }
  void attachScalar(int depth, const char *key, pinkjs::Value value) {
    if (depth == 0) {
      root = value;
      hasRoot = true;
      return;
    }
    if (depth < 0 || depth > static_cast<int>(sizeof(containers) /
                                              sizeof(containers[0]))) {
      runtime.fail(pinkjs::ErrorCode{164});
      return;
    }
    append(containers[depth - 1], key, value);
  }
};

pinkjs::Value parsePinkJsJson(pinkjs::Runtime &runtime, pinkjs::StringView json,
                              void *) {
  if (json.size > INT_MAX - 1)
    return runtime.fail(pinkjs::ErrorCode{165});
  PinkJsJsonStream stream(json);
  PinkJsJsonListener listener(runtime);
  LightweightJsonParser parser;
  char error[64] = {};
  if (!parser.parse(&stream, static_cast<int>(json.size), 1000,
                    static_cast<int>(json.size + 1U), listener, error,
                    sizeof(error))) {
    if (error[0] != '\0')
      return runtime.fail(error);
    return runtime.fail(pinkjs::ErrorCode{221});
  }
  if (!listener.valid())
    return runtime.fail(pinkjs::ErrorCode{166});
  return listener.result();
}
#endif

class PinkJsApp : public ActiveApp {
public:
  void launch(const char *nextPath, const char *nextId) {
    unload();
    pinkJsLowestStackHwm = UINT_MAX;
    copyText(appId, sizeof(appId), nextId);
    activePinkJsApp = this;
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

  pinkjs::Value queueFetch(pinkjs::Runtime &vm, const pinkjs::Value *args,
                           size_t argc);
  pinkjs::Value scheduleTimer(pinkjs::Runtime &vm,
                              const pinkjs::Value *args, size_t argc,
                              bool repeating);
  pinkjs::Value cancelTimer(pinkjs::Runtime &vm, const pinkjs::Value *args,
                            size_t argc);
  bool beginSystemModal(PinkJsSystemModal kind, pinkjs::Value promise,
                        uint32_t captureDurationMs = 0,
                        DatePickerDate initialDate = {});
  bool systemModalIdle() const {
    return systemModal == PinkJsSystemModal::None;
  }
  bool waitForPromise(pinkjs::Runtime &vm, pinkjs::Value promise);

  void draw(Adafruit_GFX &gfx) override {
    if (pendingStart) {
      gfx.setTextColor(1);
      drawPinkJsCentered(gfx, "PINKJS", 70, 2);
      drawPinkJsCentered(gfx, "STARTING", 102, 1);
      return;
    }
    if (status[0] != '\0') {
      gfx.setTextColor(1);
      drawPinkJsCentered(gfx, "PINKJS", 54, 2);
      drawPinkJsWrappedCentered(gfx, status, 86);
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
    if ((touchCallbacks & (callback | PinkJsTouchGeneric)) == 0)
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
  pinkjs::Runtime *runtime = nullptr;
  AsyncState *asyncState = nullptr;
  TimerState *timerState = nullptr;
  pinkjs::RootHandle systemModalPromise = pinkjs::kInvalidRoot;
  DatePickerDate systemModalDate = {};
  uint32_t systemModalCaptureDurationMs = 0;
  PinkJsSystemModal systemModal = PinkJsSystemModal::None;
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
      return PinkJsTouchDown;
    case TOUCH_EVENT_MOVE:
      return PinkJsTouchMove;
    case TOUCH_EVENT_UP:
      return PinkJsTouchUp;
    }
    return 0;
  }

  static void executorTaskMain(void *context) {
    PinkJsApp *app = static_cast<PinkJsApp *>(context);
    if (app != nullptr)
      app->executorLoop();
    vTaskDelete(nullptr);
  }

  void executorLoop() {
    while (true) {
      PinkJsExecutorCommand *command = nullptr;
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
    executorCommands = xQueueCreate(1, sizeof(PinkJsExecutorCommand *));
    executorCompletions = xQueueCreate(1, sizeof(PinkJsExecutorCommand *));
    if (executorCommands == nullptr || executorCompletions == nullptr) {
      stopExecutor();
      return false;
    }
    if (xTaskCreatePinnedToCore(executorTaskMain, "pinkjs",
                                kPinkJsExecutorStackBytes, this,
                                kPinkJsExecutorPriority, &executorTask,
                                kPinkJsExecutorCore) != pdPASS) {
      executorTask = nullptr;
      stopExecutor();
      return false;
    }
    return true;
  }

  bool execute(PinkJsExecutorOperation operation, void *context) {
    if (executorTask == nullptr || executorCommands == nullptr ||
        executorCompletions == nullptr) {
      return false;
    }
    PinkJsExecutorCommand command{operation, context};
    PinkJsExecutorCommand *sent = &command;
    if (xQueueSend(executorCommands, &sent, portMAX_DELAY) != pdPASS)
      return false;
    PinkJsExecutorCommand *completed = nullptr;
    return xQueueReceive(executorCompletions, &completed, portMAX_DELAY) ==
               pdPASS &&
           completed == &command;
  }

  void stopExecutor() {
    if (executorTask != nullptr && executorCommands != nullptr &&
        executorCompletions != nullptr) {
      PinkJsExecutorCommand command;
      PinkJsExecutorCommand *sent = &command;
      if (xQueueSend(executorCommands, &sent, portMAX_DELAY) == pdPASS) {
        PinkJsExecutorCommand *completed = nullptr;
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

  static void loadOperation(PinkJsApp &app, void *) { app.load(); }
  static void unloadOperation(PinkJsApp &app, void *) { app.unloadRuntime(); }
  static void drawOperation(PinkJsApp &app, void *raw) {
    DrawContext *context = static_cast<DrawContext *>(raw);
    if (context == nullptr || context->gfx == nullptr)
      return;
    pinkJsDrawTarget = context->gfx;
    app.run("onDraw", nullptr, 0, nullptr);
    pinkJsDrawTarget = nullptr;
  }
  static void touchOperation(PinkJsApp &app, void *raw) {
    TouchContext *context = static_cast<TouchContext *>(raw);
    if (context != nullptr) {
      context->dirty =
          app.dispatchTouch("onTouch", "touch", context->point);
    }
  }
  static void updateOperation(PinkJsApp &app, void *raw) {
    UpdateContext *context = static_cast<UpdateContext *>(raw);
    if (context != nullptr)
      context->dirty = app.updateRuntime();
  }
  static void buttonOperation(PinkJsApp &app, void *raw) {
    ButtonContext *context = static_cast<ButtonContext *>(raw);
    if (context != nullptr)
      context->result = app.buttonRuntime(context->callback);
  }
  static void rawTouchOperation(PinkJsApp &app, void *raw) {
    RawTouchContext *context = static_cast<RawTouchContext *>(raw);
    if (context != nullptr)
      app.rawTouchRuntime(context->event);
  }
  static void saveOperation(PinkJsApp &app, void *raw) {
    SaveContext *context = static_cast<SaveContext *>(raw);
    if (context != nullptr)
      context->length = app.saveRuntime(context->buffer, context->capacity);
  }
  static void restoreOperation(PinkJsApp &app, void *raw) {
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
      const pinkjs::RunResult jobs = runtime->drainJobs(kPinkJsJobsPerUpdate);
      if (!jobs.ok)
        setRuntimeError();
    }
    pinkjs::RunResult result;
    bool called = false;
    if (run("onUpdate", nullptr, 0, &result, &called) && called) {
      dirty = dirty || runtime->isTruthy(result.value);
    }
    return dirty;
  }

  AppEventResult buttonRuntime(const char *callback) {
    pinkjs::RunResult result;
    bool called = false;
    if (!run(callback, nullptr, 0, &result, &called)) {
      return AppEventResult::Unhandled;
    }
    if (!called) {
      pinkjs::Value event = runtime->makeObject();
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
    uint8_t callback = PinkJsTouchGeneric;
    switch (event.type) {
    case TOUCH_EVENT_DOWN:
      specific = "onTouchDown";
      type = "down";
      callback = PinkJsTouchDown;
      break;
    case TOUCH_EVENT_MOVE:
      specific = "onTouchMove";
      type = "move";
      callback = PinkJsTouchMove;
      break;
    case TOUCH_EVENT_UP:
      specific = "onTouchUp";
      type = "up";
      callback = PinkJsTouchUp;
      break;
    }
    const char *handler = (touchCallbacks & callback) != 0 ? specific
                                                           : "onTouch";
    pendingDirty = pendingDirty || dispatchTouch(handler, type, event.point);
  }

  size_t saveRuntime(uint8_t *buffer, size_t capacity) {
    pinkjs::RunResult result;
    bool called = false;
    if (buffer == nullptr || capacity == 0 ||
        !run("onSave", nullptr, 0, &result, &called) || !called ||
        !runtime->isString(result.value)) {
      return 0;
    }
    const pinkjs::StringView data = runtime->stringView(result.value);
    const size_t amount = data.size < capacity ? data.size : capacity;
    memcpy(buffer, data.data, amount);
    return amount;
  }

  void restoreRuntime(const uint8_t *buffer, size_t length) {
    if (runtime == nullptr || buffer == nullptr) {
      return;
    }
    pinkjs::Value state =
        runtime->makeString(reinterpret_cast<const char *>(buffer), length);
    pinkjs::RunResult result;
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
    const pinkjs::Error &error = runtime->error();
    char described[72];
    const char *message = error.message;
    uint8_t errorNumber = 0;
    char description[60];
    if (pinkJsErrorNumber(error.message, &errorNumber) &&
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
    const pinkjs::MemoryStats stats = runtime->memoryStats();
    if (error.sourceLine[0] != '\0') {
      pinkJsLogPrintf(
          "[pinkjs] %s\n[pinkjs] > %s\n[pinkjs]   %s\n"
          "[pinkjs] error heap=%lu free=%lu peak=%lu workspace=%lu\n",
          status, error.sourceLine, caret,
          static_cast<unsigned long>(stats.heapUsedBytes),
          static_cast<unsigned long>(stats.heapFreeBytes),
          static_cast<unsigned long>(stats.peakHeapUsedBytes),
          static_cast<unsigned long>(stats.workspaceBytes));
    } else {
      pinkJsLogPrintf(
          "[pinkjs] %s\n"
          "[pinkjs] error heap=%lu free=%lu peak=%lu workspace=%lu\n",
          status, static_cast<unsigned long>(stats.heapUsedBytes),
          static_cast<unsigned long>(stats.heapFreeBytes),
          static_cast<unsigned long>(stats.peakHeapUsedBytes),
          static_cast<unsigned long>(stats.workspaceBytes));
    }
    pendingDirty = true;
  }

  bool run(const char *callback, const pinkjs::Value *args, size_t argc,
           pinkjs::RunResult *out, bool *calledOut = nullptr) {
    if (calledOut != nullptr) {
      *calledOut = false;
    }
    if (runtime == nullptr || status[0] != '\0') {
      return false;
    }
    logPinkJsStackLow(callback);
    bool called = false;
    const pinkjs::RunResult result =
        runtime->callIfFunction(callback, args, argc, &called);
    logPinkJsStackLow(callback);
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
    pinkjs::Value event = runtime->makeObject();
    runtime->objectSet(event, "type", runtime->makeString(type));
    runtime->objectSet(event, "x", runtime->number(point.x));
    runtime->objectSet(event, "y", runtime->number(point.y));
    pinkjs::RunResult result;
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
      touchCallbacks |= PinkJsTouchGeneric;
    if (runtime->isCallable(runtime->get("onTouchDown")))
      touchCallbacks |= PinkJsTouchDown;
    if (runtime->isCallable(runtime->get("onTouchMove")))
      touchCallbacks |= PinkJsTouchMove;
    if (runtime->isCallable(runtime->get("onTouchUp")))
      touchCallbacks |= PinkJsTouchUp;
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
    if (runtime != nullptr && systemModalPromise != pinkjs::kInvalidRoot) {
      runtime->release(systemModalPromise);
    }
    systemModal = PinkJsSystemModal::None;
    systemModalPromise = pinkjs::kInvalidRoot;
    stopAsyncWorker();
    releaseSleepGuards();
    if (appApiSessionMatches(appId)) {
      appApiEndSession();
    }
    releaseRuntime();
    if (pinkJsLogBuffer != nullptr) {
      heap_caps_free(pinkJsLogBuffer);
      pinkJsLogBuffer = nullptr;
    }
    status[0] = '\0';
    pendingDirty = false;
    if (path != nullptr) {
      heap_caps_free(path);
      path = nullptr;
    }
    appId[0] = '\0';
    if (activePinkJsApp == this) {
      activePinkJsApp = nullptr;
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
  return activePinkJsApp != nullptr && activePinkJsApp->systemModalIdle();
}

bool beginActiveSystemModal(PinkJsSystemModal kind, pinkjs::Value promise,
                            uint32_t captureDurationMs,
                            DatePickerDate initialDate) {
  return activePinkJsApp != nullptr &&
         activePinkJsApp->beginSystemModal(kind, promise, captureDurationMs,
                                           initialDate);
}

bool PinkJsApp::ensureAsyncWorker() {
  if (asyncState != nullptr)
    return true;
  void *memory = allocatePsram(sizeof(AsyncState));
  if (memory == nullptr)
    return false;
  AsyncState *created = new (memory) AsyncState();
  created->requests =
      xQueueCreate(kPinkJsFetchQueueDepth, sizeof(FetchRequest *));
  created->completions =
      xQueueCreate(kPinkJsFetchQueueDepth, sizeof(FetchCompletion *));
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
  if (xTaskCreatePinnedToCore(pinkJsWorker, "pinkjs-fetch",
                              kPinkJsWorkerStackBytes, created,
                              kPinkJsWorkerPriority, &task,
                              kPinkJsWorkerCore) != pdPASS) {
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

pinkjs::Value PinkJsApp::queueFetch(pinkjs::Runtime &vm,
                                    const pinkjs::Value *args, size_t argc) {
  if (argc < 1 || argc > 2 || !vm.isString(args[0]))
    return vm.fail(pinkjs::ErrorCode{167});
  const pinkjs::StringView url = vm.stringView(args[0]);
  if (url.size == 0 ||
      url.size >= sizeof(static_cast<FetchRequest *>(nullptr)->url))
    return vm.fail(pinkjs::ErrorCode{168});
  if (argc == 2 && vm.typeOf(args[1]) != pinkjs::ValueType::Object)
    return vm.fail(pinkjs::ErrorCode{169});

  FetchRequest *request =
      static_cast<FetchRequest *>(allocatePsram(sizeof(FetchRequest)));
  if (request == nullptr) {
    pinkjs::Value promise = vm.makePromise();
    if (vm.isUndefined(promise))
      return promise;
    vm.settlePromise(promise, vm.makeString("no PSRAM for fetch request"), true);
    return promise;
  }
  new (request) FetchRequest();
  memcpy(request->url, url.data, url.size);
  request->url[url.size] = '\0';

  const pinkjs::Value options = argc == 2 ? args[1] : vm.undefined();
  if (!vm.isUndefined(options)) {
    const pinkjs::Value method = vm.objectGet(options, "method");
    if (!vm.isUndefined(method)) {
      if (!vm.isString(method)) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{170});
      }
      const pinkjs::StringView text = vm.stringView(method);
      if (text.size == 0 || text.size >= sizeof(request->method)) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{171});
      }
      for (size_t i = 0; i < text.size; i++) {
        const char c = text.data[i];
        request->method[i] = c >= 'a' && c <= 'z'
                                 ? static_cast<char>(c - 'a' + 'A')
                                 : c;
      }
      request->method[text.size] = '\0';
    }
    const pinkjs::Value maxBytes = vm.objectGet(options, "maxBytes");
    if (!vm.isUndefined(maxBytes)) {
      const double raw = vm.toNumber(maxBytes);
      if (!std::isfinite(raw) || raw < 1 || raw > kPinkJsMaxHttpBytes) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{172});
      }
      request->maxBytes = static_cast<size_t>(raw);
    }
    const pinkjs::Value timeout = vm.objectGet(options, "timeout");
    if (!vm.isUndefined(timeout)) {
      const double raw = vm.toNumber(timeout);
      if (!std::isfinite(raw) || raw < 100 || raw > 30000) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{173});
      }
      request->timeoutMs = static_cast<uint32_t>(raw);
    }
    const pinkjs::Value body = vm.objectGet(options, "body");
    if (!vm.isUndefined(body)) {
      if (!vm.isString(body)) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{174});
      }
      const pinkjs::StringView text = vm.stringView(body);
      if (text.size > kPinkJsMaxHttpBytes) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{175});
      }
      if (text.size > 0) {
        request->body = static_cast<uint8_t *>(allocatePsram(text.size));
        if (request->body == nullptr) {
          releaseFetchRequest(request);
          return vm.fail(pinkjs::ErrorCode{176});
        }
        memcpy(request->body, text.data, text.size);
        request->bodyBytes = text.size;
      }
    }
    const pinkjs::Value headers = vm.objectGet(options, "headers");
    if (!vm.isUndefined(headers)) {
      if (vm.typeOf(headers) != pinkjs::ValueType::Object) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{177});
      }
      const size_t count = vm.objectPropertyCount(headers);
      if (count > sizeof(request->headers) / sizeof(request->headers[0])) {
        releaseFetchRequest(request);
        return vm.fail(pinkjs::ErrorCode{178});
      }
      for (size_t i = 0; i < count; i++) {
        pinkjs::Value key;
        pinkjs::Value value;
        vm.objectPropertyAt(headers, i, &key, &value);
        if (!vm.isString(value)) {
          releaseFetchRequest(request);
          return vm.fail(pinkjs::ErrorCode{179});
        }
        const pinkjs::StringView keyText = vm.stringView(key);
        const pinkjs::StringView valueText = vm.stringView(value);
        if (keyText.size >= sizeof(request->headers[i].name) ||
            valueText.size >= sizeof(request->headers[i].value)) {
          releaseFetchRequest(request);
          return vm.fail(pinkjs::ErrorCode{180});
        }
        memcpy(request->headers[i].name, keyText.data, keyText.size);
        memcpy(request->headers[i].value, valueText.data, valueText.size);
        request->headers[i].name[keyText.size] = '\0';
        request->headers[i].value[valueText.size] = '\0';
        request->headerCount++;
      }
    }
  }
  pinkjs::Value promise = vm.makePromise();
  if (vm.isUndefined(promise)) {
    releaseFetchRequest(request);
    return promise;
  }
  const pinkjs::RootHandle retained = vm.retain(promise);
  if (retained == pinkjs::kInvalidRoot) {
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

bool PinkJsApp::pumpAsyncCompletions() {
  if (asyncState == nullptr || runtime == nullptr)
    return false;
  bool completedAny = false;
  FetchCompletion *completion = nullptr;
  while (xQueueReceive(asyncState->completions, &completion, 0) == pdPASS) {
    completedAny = true;
    if (completion == nullptr)
      continue;
    const pinkjs::Value promise = runtime->retained(completion->promise);
    if (completion->error != pinkjs::ErrorCode{}) {
      char error[5];
      pinkjs::formatErrorCode(completion->error, error);
      runtime->settlePromise(promise, runtime->makeString(error, 4), true);
      runtime->release(completion->promise);
      releaseFetchCompletion(completion);
      continue;
    }
    pinkjs::Value response = runtime->makeObject();
    pinkjs::Value body = runtime->makeString(
        reinterpret_cast<const char *>(completion->body), completion->bodyBytes);
    pinkjs::Value headers = runtime->makeObject();
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

bool PinkJsApp::waitForPromise(pinkjs::Runtime &vm, pinkjs::Value promise) {
  const uint32_t startedAt = millis();
  while (vm.promiseState(promise) == pinkjs::PromiseState::Pending &&
         status[0] == '\0' && millis() - startedAt < 31000U) {
    pumpAsyncCompletions();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return vm.promiseState(promise) != pinkjs::PromiseState::Pending;
}

pinkjs::Value PinkJsApp::scheduleTimer(pinkjs::Runtime &vm,
                                      const pinkjs::Value *args, size_t argc,
                                      bool repeating) {
  if (argc < 1 || !vm.isCallable(args[0]))
    return vm.fail(pinkjs::ErrorCode{181});
  const double rawDelay = argc > 1 ? vm.toNumber(args[1]) : 0;
  if (!std::isfinite(rawDelay) || rawDelay < 0 || rawDelay > UINT32_MAX)
    return vm.fail(pinkjs::ErrorCode{182});
  if (timerState == nullptr) {
    timerState = static_cast<TimerState *>(allocatePsram(sizeof(TimerState)));
    if (timerState == nullptr)
      return vm.fail(pinkjs::ErrorCode{183});
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
    return vm.fail(pinkjs::ErrorCode{184});
  pinkjs::Value arguments = vm.makeArray(argc > 2 ? argc - 2U : 0);
  for (size_t i = 2; i < argc; i++)
    vm.arrayPush(arguments, args[i]);
  const pinkjs::RootHandle callback = vm.retain(args[0]);
  const pinkjs::RootHandle argumentRoot = vm.retain(arguments);
  if (callback == pinkjs::kInvalidRoot ||
      argumentRoot == pinkjs::kInvalidRoot) {
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

pinkjs::Value PinkJsApp::cancelTimer(pinkjs::Runtime &vm,
                                    const pinkjs::Value *args, size_t argc) {
  if (argc != 1 || !std::isfinite(vm.toNumber(args[0])))
    return vm.fail(pinkjs::ErrorCode{185});
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

bool PinkJsApp::pumpTimers() {
  if (timerState == nullptr || runtime == nullptr || status[0] != '\0')
    return false;
  bool dirty = false;
  const uint32_t now = millis();
  for (TimerSlot &slot : timerState->slots) {
    if (!slot.active || static_cast<int32_t>(now - slot.dueAt) < 0)
      continue;
    const pinkjs::Value callback = runtime->retained(slot.callback);
    const pinkjs::Value arguments = runtime->retained(slot.arguments);
    const size_t argc = runtime->arrayLength(arguments);
    pinkjs::Value argv[16];
    for (size_t i = 0; i < argc && i < sizeof(argv) / sizeof(argv[0]); i++)
      argv[i] = runtime->arrayAt(arguments, i);
    const bool oneShot = slot.intervalMs == 0;
    if (oneShot) {
      slot.active = false;
    } else {
      slot.dueAt = now + slot.intervalMs;
    }
    logPinkJsStackLow("timer");
    const pinkjs::RunResult result = runtime->call(callback, argv, argc);
    logPinkJsStackLow("timer");
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

void PinkJsApp::releaseTimers() {
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

bool PinkJsApp::beginSystemModal(PinkJsSystemModal kind,
                                 pinkjs::Value promise,
                                 uint32_t captureDurationMs,
                                 DatePickerDate initialDate) {
  if (systemModal != PinkJsSystemModal::None || runtime == nullptr) {
    return false;
  }
  const pinkjs::RootHandle retained = runtime->retain(promise);
  if (retained == pinkjs::kInvalidRoot) {
    return false;
  }
  systemModal = kind;
  systemModalPromise = retained;
  systemModalCaptureDurationMs = captureDurationMs;
  systemModalDate = initialDate;
  return true;
}

bool PinkJsApp::pumpSystemModal() {
  if (systemModal == PinkJsSystemModal::None || runtime == nullptr) {
    return false;
  }
  AppApiStatus status = AppApiStatus::Pending;
  pinkjs::Value result = runtime->undefined();
  switch (systemModal) {
  case PinkJsSystemModal::FilePicker: {
    char path[288];
    status = appApiTakePickedFile(path, sizeof(path));
    if (status == AppApiStatus::Ok) {
      result = runtime->makeString(path);
    }
    break;
  }
  case PinkJsSystemModal::Keyboard: {
    char text[256];
    status = appApiGetKeyboardInput("", 0, 255, text, sizeof(text));
    if (status == AppApiStatus::Ok) {
      result = runtime->makeString(text);
    }
    break;
  }
  case PinkJsSystemModal::DatePicker: {
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
  case PinkJsSystemModal::AudioCapture: {
    const uint8_t *data = nullptr;
    size_t size = 0;
    status = appApiCaptureWav(systemModalCaptureDurationMs, &data, &size);
    if (status == AppApiStatus::End && data != nullptr) {
      result = runtime->makeString(reinterpret_cast<const char *>(data), size);
      status = AppApiStatus::Ok;
    }
    break;
  }
  case PinkJsSystemModal::AudioCapturePcm16: {
    const uint8_t *data = nullptr;
    size_t size = 0;
    status = appApiCapturePcm16(systemModalCaptureDurationMs, &data, &size);
    if (status == AppApiStatus::End && data != nullptr) {
      result = runtime->makeString(reinterpret_cast<const char *>(data), size);
      status = AppApiStatus::Ok;
    }
    break;
  }
  case PinkJsSystemModal::None:
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
  const pinkjs::Value promise = runtime->retained(systemModalPromise);
  runtime->settlePromise(promise, result, rejected);
  runtime->release(systemModalPromise);
  systemModal = PinkJsSystemModal::None;
  systemModalPromise = pinkjs::kInvalidRoot;
  systemModalCaptureDurationMs = 0;
  return true;
}

void PinkJsApp::stopAsyncWorker() {
  if (asyncState == nullptr)
    return;
  asyncState->stopping.store(true);
  asyncState = nullptr; // The worker owns and releases its detached state.
}

pinkjs::Value nativeConfigGet(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
  if (activePinkJsApp == nullptr || argc != 1 || !runtime.isString(args[0])) {
    return runtime.fail(pinkjs::ErrorCode{186});
  }
  const pinkjs::StringView key = runtime.stringView(args[0]);
  if (key.size >= 64) {
    return runtime.fail(pinkjs::ErrorCode{187});
  }
  char keyText[64];
  memcpy(keyText, key.data, key.size);
  keyText[key.size] = '\0';
  char value[CONFIG_VALUE_MAX];
  if (!configGetForApp(activePinkJsApp->id(), keyText, value, sizeof(value))) {
    return runtime.undefined();
  }
  return runtime.makeString(value);
}

pinkjs::Value nativeConfigSet(pinkjs::Runtime &runtime,
                              const pinkjs::Value *args, size_t argc, void *) {
  if (activePinkJsApp == nullptr || argc != 2 || !runtime.isString(args[0])) {
    return runtime.fail(pinkjs::ErrorCode{188});
  }
  const pinkjs::StringView key = runtime.stringView(args[0]);
  if (key.size >= 64) {
    return runtime.fail(pinkjs::ErrorCode{187});
  }
  char keyText[64];
  memcpy(keyText, key.data, key.size);
  keyText[key.size] = '\0';
  char value[CONFIG_VALUE_MAX];
  runtime.format(args[1], value, sizeof(value));
  return runtime.boolean(
      configSetForApp(activePinkJsApp->id(), keyText, value));
}

pinkjs::Value nativeHttpGet(pinkjs::Runtime &runtime,
                            const pinkjs::Value *args, size_t argc, void *) {
#if ENABLE_NETWORK_APPS
  if (argc < 1 || argc > 2 || !runtime.isString(args[0])) {
    return runtime.fail(pinkjs::ErrorCode{189});
  }
  if (!wifiIsConnected()) {
    return runtime.fail(pinkjs::ErrorCode{190});
  }
  const pinkjs::StringView url = runtime.stringView(args[0]);
  if (url.size == 0 || url.size >= 512) {
    return runtime.fail(pinkjs::ErrorCode{191});
  }
  char urlText[512];
  memcpy(urlText, url.data, url.size);
  urlText[url.size] = '\0';
  size_t limit = kPinkJsMaxHttpBytes;
  if (argc == 2) {
    const double requested = runtime.toNumber(args[1]);
    if (!std::isfinite(requested) || requested < 1 ||
        requested > static_cast<double>(kPinkJsMaxHttpBytes)) {
      return runtime.fail(pinkjs::ErrorCode{192});
    }
    limit = static_cast<size_t>(requested);
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kPinkJsHttpTimeoutMs);
  if (!http.begin(client, urlText)) {
    return runtime.fail(pinkjs::ErrorCode{193});
  }
  http.addHeader("User-Agent", "PocketInk-PinkJS/0.1");
  const int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return runtime.fail(pinkjs::ErrorCode{194});
  }
  const int expected = http.getSize();
  if (expected > 0 && static_cast<size_t>(expected) > limit) {
    http.end();
    return runtime.fail(pinkjs::ErrorCode{195});
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
    return runtime.fail(pinkjs::ErrorCode{196});
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
    if (millis() - startedAt >= kPinkJsHttpTimeoutMs) {
      break;
    }
    delay(1);
  }
  http.end();
  if (expected > 0 && length != static_cast<size_t>(expected)) {
    heap_caps_free(buffer);
    return runtime.fail(pinkjs::ErrorCode{197});
  }
  pinkjs::Value result = runtime.makeString(
      reinterpret_cast<const char *>(buffer), length);
  heap_caps_free(buffer);
  return result;
#else
  (void)args;
  (void)argc;
  return runtime.fail(pinkjs::ErrorCode{198});
#endif
}

pinkjs::Value nativeFetch(pinkjs::Runtime &runtime, const pinkjs::Value *args,
                          size_t argc, void *) {
  return activePinkJsApp != nullptr
             ? activePinkJsApp->queueFetch(runtime, args, argc)
             : runtime.fail(pinkjs::ErrorCode{199});
}

pinkjs::Value nativeSetTimeout(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc, void *) {
  return activePinkJsApp != nullptr
             ? activePinkJsApp->scheduleTimer(runtime, args, argc, false)
             : runtime.fail(pinkjs::ErrorCode{199});
}

pinkjs::Value nativeSetInterval(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  return activePinkJsApp != nullptr
             ? activePinkJsApp->scheduleTimer(runtime, args, argc, true)
             : runtime.fail(pinkjs::ErrorCode{199});
}

pinkjs::Value nativeClearTimer(pinkjs::Runtime &runtime,
                               const pinkjs::Value *args, size_t argc, void *) {
  return activePinkJsApp != nullptr
             ? activePinkJsApp->cancelTimer(runtime, args, argc)
             : runtime.undefined();
}

bool awaitPendingPromise(pinkjs::Runtime &runtime, pinkjs::Value promise,
                         void *userData) {
  PinkJsApp *app = static_cast<PinkJsApp *>(userData);
  return app != nullptr && app->waitForPromise(runtime, promise);
}

pinkjs::Value nativeResponseText(pinkjs::Runtime &runtime,
                                 pinkjs::Value receiver,
                                 const pinkjs::Value *, size_t argc, void *) {
  if (argc != 0 || runtime.typeOf(receiver) != pinkjs::ValueType::Object)
    return runtime.fail(pinkjs::ErrorCode{200});
  const pinkjs::Value body = runtime.objectGet(receiver, "_body");
  pinkjs::Value promise = runtime.makePromise();
  runtime.settlePromise(promise, body, false);
  return promise;
}

pinkjs::Value nativeResponseJson(pinkjs::Runtime &runtime,
                                 pinkjs::Value receiver,
                                 const pinkjs::Value *, size_t argc, void *) {
  if (argc != 0 || runtime.typeOf(receiver) != pinkjs::ValueType::Object)
    return runtime.fail(pinkjs::ErrorCode{201});
  return runtime.parseJsonPromise(runtime.objectGet(receiver, "_body"));
}

pinkjs::Value nativeUiButton(pinkjs::Runtime &runtime,
                             const pinkjs::Value *args, size_t argc, void *) {
  if (argc < 5 || argc > 7 || !runtime.isString(args[4]))
    return runtime.fail(pinkjs::ErrorCode{202});
  if (!numericArguments(runtime, args, 4, 4))
    return runtime.undefined();
  if (pinkJsDrawTarget == nullptr)
    return runtime.undefined();
  const pinkjs::StringView label = runtime.stringView(args[4]);
  if (label.size >= 96)
    return runtime.fail(pinkjs::ErrorCode{203});
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
  TouchButton(bounds, content).draw(*pinkJsDrawTarget,
                                    argc > 5 && runtime.toBoolean(args[5]));
  return runtime.undefined();
}

pinkjs::Value nativeUiButtonContains(pinkjs::Runtime &runtime,
                                     const pinkjs::Value *args, size_t argc,
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

pinkjs::Value nativeUiListTouch(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
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
  pinkjs::Value result = runtime.makeObject();
  const char *action = hit.action == UiListTouchAction::Scroll
                           ? "scroll"
                           : (hit.action == UiListTouchAction::Select ? "select"
                                                                     : "none");
  runtime.objectSet(result, "action", runtime.makeString(action));
  runtime.objectSet(result, "index", runtime.number(hit.index));
  runtime.objectSet(result, "offset", runtime.number(offset));
  return result;
}

bool readOptionRect(pinkjs::Runtime &runtime, pinkjs::Value option,
                    UiRect &rect) {
  if (runtime.typeOf(option) != pinkjs::ValueType::Object)
    return false;
  const pinkjs::Value x = runtime.objectGet(option, "x");
  const pinkjs::Value y = runtime.objectGet(option, "y");
  const pinkjs::Value w = runtime.objectGet(option, "w");
  const pinkjs::Value h = runtime.objectGet(option, "h");
  if (!runtime.isNumber(x) || !runtime.isNumber(y) || !runtime.isNumber(w) ||
      !runtime.isNumber(h))
    return false;
  rect = {toI16(runtime.toNumber(x)), toI16(runtime.toNumber(y)),
          toI16(runtime.toNumber(w)), toI16(runtime.toNumber(h))};
  return true;
}

pinkjs::Value nativeUiSelectDraw(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  if (argc < 2 || argc > 3 ||
      runtime.typeOf(args[0]) != pinkjs::ValueType::Array)
    return runtime.fail(pinkjs::ErrorCode{204});
  if (pinkJsDrawTarget == nullptr)
    return runtime.undefined();
  const double selected = runtime.toNumber(args[1]);
  for (size_t i = 0; i < runtime.arrayLength(args[0]); i++) {
    const pinkjs::Value option = runtime.arrayAt(args[0], i);
    UiRect rect;
    const pinkjs::Value labelValue = runtime.objectGet(option, "label");
    if (!readOptionRect(runtime, option, rect) ||
        !runtime.isString(labelValue))
      return runtime.fail(pinkjs::ErrorCode{205});
    const pinkjs::StringView label = runtime.stringView(labelValue);
    if (label.size >= 64)
      return runtime.fail(pinkjs::ErrorCode{206});
    char text[64];
    memcpy(text, label.data, label.size);
    text[label.size] = '\0';
    const bool pressed = runtime.toNumber(runtime.objectGet(option, "value")) ==
                         selected;
    uiDrawButton(*pinkJsDrawTarget, rect, text, pressed,
                 argc > 2 ? toU8(runtime.toNumber(args[2])) : 1);
  }
  return runtime.undefined();
}

pinkjs::Value nativeUiSelectHit(pinkjs::Runtime &runtime,
                                const pinkjs::Value *args, size_t argc,
                                void *) {
  if (argc < 3 || argc > 4 || !runtime.isNumber(args[0]) ||
      !runtime.isNumber(args[1]) ||
      runtime.typeOf(args[2]) != pinkjs::ValueType::Array)
    return runtime.fail(pinkjs::ErrorCode{207});
  const TouchPoint point = {toU16(runtime.toNumber(args[0])),
                            toU16(runtime.toNumber(args[1]))};
  const uint8_t padding = argc > 3 ? toU8(runtime.toNumber(args[3]))
                                   : UI_TOUCH_ASSIST_PX;
  for (size_t i = 0; i < runtime.arrayLength(args[2]); i++) {
    const pinkjs::Value option = runtime.arrayAt(args[2], i);
    UiRect rect;
    if (!readOptionRect(runtime, option, rect))
      return runtime.fail(pinkjs::ErrorCode{205});
    if (uiTouchContains(rect, point, padding))
      return runtime.objectGet(option, "value");
  }
  return runtime.undefined();
}

pinkjs::Value nativeGetKeyboardInput(pinkjs::Runtime &runtime,
                                     const pinkjs::Value *args, size_t argc,
                                     void *) {
  if (activePinkJsApp == nullptr || argc < 1 || argc > 3 ||
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
  pinkjs::Value promise = runtime.makePromise();
  if (runtime.isUndefined(promise)) {
    return promise;
  }
  if (!activePinkJsApp->systemModalIdle() ||
      appApiGetKeyboardInput(initial, static_cast<uint8_t>(layoutValue),
                             static_cast<size_t>(maxValue), initial,
                             sizeof(initial)) != AppApiStatus::Pending ||
      !activePinkJsApp->beginSystemModal(PinkJsSystemModal::Keyboard,
                                         promise)) {
    runtime.settlePromise(promise, runtime.makeString("keyboard is busy"), true);
  }
  return promise;
}

pinkjs::Value nativeGetDateInput(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  if (activePinkJsApp == nullptr || !numericArguments(runtime, args, argc, 3)) {
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
  pinkjs::Value promise = runtime.makePromise();
  DatePickerDate ignored;
  if (runtime.isUndefined(promise)) {
    return promise;
  }
  if (!activePinkJsApp->systemModalIdle() ||
      appApiGetDateInput(initial, ignored) != AppApiStatus::Pending ||
      !activePinkJsApp->beginSystemModal(PinkJsSystemModal::DatePicker,
                                         promise, 0, initial)) {
    runtime.settlePromise(promise, runtime.makeString("date picker is busy"),
                          true);
  }
  return promise;
}

bool PinkJsApp::installBindings() {
  if (runtime == nullptr) {
    return false;
  }
  const struct {
    const char *path;
    pinkjs::NativeFunction function;
  } bindings[] = {
      {"console.log", nativeConsoleLog},
      {"time.millis", nativeMillis},
      {"pinkjs.heapUsed", nativePinkJsHeapUsed},
      {"pinkjs.heapFree", nativePinkJsHeapFree},
      {"pinkjs.heapPeak", nativePinkJsHeapPeak},
      {"network.connected", nativeNetworkConnected},
      {"network.scan", nativeNetworkScan},
      {"network.networks", nativeNetworkNetworks},
      {"http.get", nativeHttpGet},
      {"fetch", nativeFetch},
      {"setTimeout", nativeSetTimeout},
      {"setInterval", nativeSetInterval},
      {"clearTimeout", nativeClearTimer},
      {"clearInterval", nativeClearTimer},
      {"app.setWakeLock", nativeSetWakeLock},
      {"app.configGet", nativeConfigGet},
      {"app.configSet", nativeConfigSet},
      {"app.open", nativeAppOpen},
      {"files.readText", nativeFilesReadText},
      {"files.readBinary", nativeFilesReadBinary},
      {"files.write", nativeFilesWrite},
      {"files.delete", nativeFilesDelete},
      {"files.pick", nativeFilesPick},
      {"fs.readFile", nativeFsReadFile},
      {"fs.readFileSync", nativeFsReadFileSync},
      {"fs.writeFile", nativeFsWriteFile},
      {"fs.writeFileSync", nativeFsWriteFileSync},
      {"fs.appendFile", nativeFsAppendFile},
      {"fs.appendFileSync", nativeFsAppendFileSync},
      {"fs.readdir", nativeFsReaddir},
      {"fs.readdirSync", nativeFsReaddirSync},
      {"fs.existsSync", nativeFsExistsSync},
      {"fs.stat", nativeFsStat},
      {"fs.statSync", nativeFsStatSync},
      {"fs.unlink", nativeFsUnlink},
      {"fs.unlinkSync", nativeFsUnlinkSync},
      {"gpio.mode", nativeGpioMode},
      {"gpio.read", nativeGpioRead},
      {"gpio.write", nativeGpioWrite},
      {"sensors.read", nativeSensorsRead},
      {"audio.captureWav", nativeAudioCaptureWav},
      {"audio.capturePcm16", nativeAudioCapturePcm16},
      {"audio.supports", nativeAudioSupports},
      {"audio.load", nativeAudioLoad},
      {"audio.loadBuffer", nativeAudioLoadBuffer},
      {"audio.play", nativeAudioPlay},
      {"audio.stop", nativeAudioStop},
      {"audio.update", nativeAudioUpdate},
      {"audio.release", nativeAudioRelease},
      {"ui.setFont", nativeUiSetFont},
      {"ui.fonts", nativeUiFonts},
      {"ui.setTextSize", nativeUiSetTextSize},
      {"ui.setTextColor", nativeUiSetTextColor},
      {"ui.setCursor", nativeUiSetCursor},
      {"ui.print", nativeUiPrint},
      {"ui.drawPixel", nativeUiDrawPixel},
      {"ui.drawLine", nativeUiDrawLine},
      {"ui.drawRect", nativeUiDrawRect},
      {"ui.fillRect", nativeUiFillRect},
      {"ui.fillScreen", nativeUiFillScreen},
      {"ui.imagePath", nativeUiImagePath},
      {"ui.imageBuffer", nativeUiImageBuffer},
      {"ui.vectorPath", nativeUiVectorPath},
      {"ui.vectorString", nativeUiVectorString},
      {"ui.qr", nativeUiQr},
      {"ui.button", nativeUiButton},
      {"ui.buttonContains", nativeUiButtonContains},
      {"ui.listTouch", nativeUiListTouch},
      {"ui.selectDraw", nativeUiSelectDraw},
      {"ui.selectHit", nativeUiSelectHit},
      {"ui.getKeyboardInput", nativeGetKeyboardInput},
      {"ui.getDateInput", nativeGetDateInput},
  };
  for (const auto &binding : bindings) {
    if (!runtime->bind(binding.path, binding.function)) {
      return false;
    }
  }
  if (!runtime->set("fs.promises.readFile", runtime->get("fs.readFile")) ||
      !runtime->set("fs.promises.writeFile", runtime->get("fs.writeFile")) ||
      !runtime->set("fs.promises.appendFile",
                    runtime->get("fs.appendFile")) ||
      !runtime->set("fs.promises.readdir", runtime->get("fs.readdir")) ||
      !runtime->set("fs.promises.stat", runtime->get("fs.stat")) ||
      !runtime->set("fs.promises.unlink", runtime->get("fs.unlink"))) {
    return false;
  }
  if (!runtime->bindMethod("__responseText", nativeResponseText) ||
      !runtime->bindMethod("__responseJson", nativeResponseJson))
    return false;
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
         runtime->set("api.ERROR", runtime->number(-1)) &&
         runtime->set("api.DENIED", runtime->number(-2)) &&
         runtime->set("api.UNSUPPORTED", runtime->number(-3)) &&
         runtime->set("api.PENDING", runtime->number(0)) &&
         runtime->set("api.OK", runtime->number(1)) &&
         runtime->set("api.END", runtime->number(2)) &&
         runtime->set("gpio.INPUT", runtime->number(0)) &&
         runtime->set("gpio.INPUT_PULLUP", runtime->number(1)) &&
         runtime->set("gpio.OUTPUT", runtime->number(2)) &&
         runtime->set("app.id", runtime->makeString(appId)) &&
         runtime->set("app.path", runtime->makeString(path));
}

void PinkJsApp::load() {
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
  if (sourceBytes > kPinkJsMaxSourceBytes) {
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

  workspaceAllocation = allocatePinkJsWorkspace(&workspaceBytes);
  if (workspaceAllocation == nullptr) {
    heap_caps_free(source);
    copyText(status, sizeof(status), "NO RUNTIME PSRAM");
    return;
  }
  runtimeStorage = heap_caps_malloc(sizeof(pinkjs::Runtime),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (runtimeStorage == nullptr) {
    heap_caps_free(source);
    heap_caps_free(workspaceAllocation);
    workspaceAllocation = nullptr;
    copyText(status, sizeof(status), "NO VM DRAM");
    return;
  }
  pinkjs::Options options;
  options.maxBindings = 192;
  options.maxNatives = 96;
  options.maxHostRoots = 48;
  options.maxCallDepth = 24;
  options.maxSteps = 160000;
  options.nowMilliseconds = pinkJsNowMilliseconds;
  options.awaitPending = awaitPendingPromise;
  options.awaitUserData = this;
#if PINKJS_USE_LIGHTWEIGHT_JSON_PARSER
  options.jsonParse = parsePinkJsJson;
#endif
  runtime = new (runtimeStorage)
      pinkjs::Runtime(workspaceAllocation, workspaceBytes, options);
  if (!runtime->ready() || !installBindings()) {
    setRuntimeError();
    heap_caps_free(source);
    releaseRuntime();
    return;
  }
  const pinkjs::RunResult evaluated = runtime->eval(source, sourceBytes);
  heap_caps_free(source);
  if (!evaluated.ok) {
    setRuntimeError();
    releaseRuntime();
    return;
  }
  refreshTouchCallbacks();
  status[0] = '\0';
  const pinkjs::MemoryStats stats = runtime->memoryStats();
  pinkJsLogPrintf("[pinkjs] loaded %s workspace=%lu heap=%lu/%lu peak=%lu\n",
                  path, static_cast<unsigned long>(workspaceBytes),
                  static_cast<unsigned long>(stats.heapUsedBytes),
                  static_cast<unsigned long>(stats.heapBytes),
                  static_cast<unsigned long>(stats.peakHeapUsedBytes));
}

PinkJsApp pinkJsApp;

AppEventResult handleButton(const char *callback) {
  return activePinkJsApp != nullptr ? activePinkJsApp->button(callback)
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

void pausePinkJsApp() { pinkJsApp.pause(); }
void resumePinkJsApp() { pinkJsApp.resume(); }
void releasePinkJsApp() { pinkJsApp.stop(); }

void handleRawTouch(const TouchEvent &event) {
  if (activePinkJsApp != nullptr) {
    activePinkJsApp->rawTouch(event);
  }
}

size_t saveContext(uint8_t *buffer, size_t capacity) {
  return activePinkJsApp != nullptr ? activePinkJsApp->save(buffer, capacity) : 0;
}

void restoreContext(const uint8_t *buffer, size_t length) {
  if (activePinkJsApp != nullptr) {
    activePinkJsApp->restore(buffer, length);
  }
}

AppBehavior pinkJsBehavior() {
  AppBehavior behavior;
  behavior.onEnter = resumePinkJsApp;
  behavior.onExit = pausePinkJsApp;
  behavior.onMenu = handleMenu;
  behavior.onMenuDouble = handleMenuDouble;
  behavior.onMenuLong = handleMenuLong;
  behavior.onPower = handlePower;
  behavior.onPowerDouble = handlePowerDouble;
  behavior.onPowerLong = handlePowerLong;
  behavior.onRawTouch = handleRawTouch;
  behavior.onSaveContext = saveContext;
  behavior.onRestoreContext = restoreContext;
  behavior.onRelease = releasePinkJsApp;
  return behavior;
}

} // namespace

ActiveApp *pinkJsAppRuntime() { return &pinkJsApp; }

bool pinkJsAppLaunchPath(const char *path, AppDefinition *out, char *id,
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
  pinkJsApp.launch(path, id);

  *out = AppDefinition{};
  out->id = id;
  out->label = label;
  out->icon = icon;
  out->category = MENU_APPS;
  out->runtime = pinkJsAppRuntime();
  out->visible = sdStorageMounted;
  out->behavior = pinkJsBehavior();
  return true;
}

void pinkJsAppStop() { pinkJsApp.stop(); }
