#include "sys/pinkjs_app.h"

#include "fs/file_provider.h"
#include "shell/connectivity.h"
#include "sys/device_clock.h"
#include "sys/global.h"
#include "sys/lockscreen_guard.h"
#include "sys/psram_allocator.h"
#ifndef PINKJS_USE_LIGHTWEIGHT_JSON_PARSER
#define PINKJS_USE_LIGHTWEIGHT_JSON_PARSER 0
#endif
#if PINKJS_USE_LIGHTWEIGHT_JSON_PARSER
#define PINKJS_DISABLE_BUILTIN_JSON_PARSER 1
#include "netapps/lightweight_json_parser.h"
#endif
#include "sys/runtime/pinkjs/pinkjs.h"
#include "sys/sd_storage.h"
#include "sys/system_config.h"
#include "ui/icon_ascii_font.h"
#include "ui/components/date_time_picker_component.h"
#include "ui/components/list_component.h"
#include "ui/components/t9_keyboard_component.h"
#include "ui/components/touch_button.h"
#include "ui/ui_helpers.h"
#include <Arduino.h>
#if ENABLE_NETWORK_APPS
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif
#include <cstdlib>
#include <atomic>
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
  pinkjs::Value promise = {};
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
  pinkjs::Value promise = {};
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

struct PinkJsUiState {
  T9KeyboardComponent keyboard;
  DateTimePickerComponent datePicker;
  char keyboardText[256] = {};
};

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
  Serial.print(text != nullptr ? text : "");
#endif
}

void logPinkJsStackLow(const char *callback) {
#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && INCLUDE_uxTaskGetStackHighWaterMark
  const UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(nullptr);
  if (highWaterMark >= pinkJsLowestStackHwm)
    return;
  pinkJsLowestStackHwm = highWaterMark;
#if PINKJS_HAS_ROM_PRINTF
  esp_rom_printf("[pinkjs] stack low callback=%s hwm=%u\n",
                 callback != nullptr ? callback : "?",
                 static_cast<unsigned>(highWaterMark));
#else
  Serial.printf("[pinkjs] stack low callback=%s hwm=%u\n",
                callback != nullptr ? callback : "?",
                static_cast<unsigned>(highWaterMark));
#endif
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
  if (!numericArguments(runtime, args, argc, 1)) {
    return runtime.undefined();
  }
  if (pinkJsDrawTarget == nullptr) {
    return runtime.undefined();
  }
  switch (toI16(runtime.toNumber(args[0]))) {
  case 1:
    pinkJsDrawTarget->setFont(&iconASCII8pt7b);
    break;
  case 2:
    pinkJsDrawTarget->setFont(&iconASCII12pt7b);
    break;
  default:
    pinkJsDrawTarget->setFont();
    break;
  }
  return runtime.undefined();
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
  PinkJsUiState *uiComponents();
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
  PinkJsUiState *uiState = nullptr;
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
    if (uiState != nullptr) {
      dirty = uiState->keyboard.update() || dirty;
      dirty = uiState->datePicker.update() || dirty;
    }
    pinkjs::RunResult result;
    bool called = false;
    if (run("onUpdate", nullptr, 0, &result, &called) && called) {
      dirty = dirty || runtime->isTruthy(result.value);
    }
    if (runtime != nullptr && status[0] == '\0') {
      const pinkjs::RunResult jobs = runtime->drainJobs(kPinkJsJobsPerUpdate);
      if (!jobs.ok)
        setRuntimeError();
    }
    return dirty;
  }

  AppEventResult buttonRuntime(const char *callback) {
    pinkjs::RunResult result;
    bool called = false;
    if (!run(callback, nullptr, 0, &result, &called) || !called) {
      return AppEventResult::Unhandled;
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
    Serial.print("[pinkjs] ");
    Serial.println(status);
    const pinkjs::MemoryStats stats = runtime->memoryStats();
#if PINKJS_HAS_ROM_PRINTF
    esp_rom_printf("[pinkjs] error heap=%lu free=%lu peak=%lu workspace=%lu\n",
                   static_cast<unsigned long>(stats.heapUsedBytes),
                   static_cast<unsigned long>(stats.heapFreeBytes),
                   static_cast<unsigned long>(stats.peakHeapUsedBytes),
                   static_cast<unsigned long>(stats.workspaceBytes));
#else
    Serial.printf("[pinkjs] error heap=%lu free=%lu peak=%lu workspace=%lu\n",
                  static_cast<unsigned long>(stats.heapUsedBytes),
                  static_cast<unsigned long>(stats.heapFreeBytes),
                  static_cast<unsigned long>(stats.peakHeapUsedBytes),
                  static_cast<unsigned long>(stats.workspaceBytes));
#endif
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
  void stopAsyncWorker();
  void releaseTimers();
  void releaseUiState();

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
    releaseUiState();
    stopAsyncWorker();
    releaseSleepGuards();
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

  pinkjs::Value promise = vm.makePromise();
  if (vm.isUndefined(promise))
    return promise;
  FetchRequest *request =
      static_cast<FetchRequest *>(allocatePsram(sizeof(FetchRequest)));
  if (request == nullptr) {
    vm.settlePromise(promise, vm.makeString("no PSRAM for fetch request"), true);
    return promise;
  }
  new (request) FetchRequest();
  request->promise = promise;
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
  if (!ensureAsyncWorker() ||
      xQueueSend(asyncState->requests, &request, 0) != pdPASS) {
    releaseFetchRequest(request);
    vm.settlePromise(promise, vm.makeString("fetch worker is busy"), true);
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
    if (completion->error != pinkjs::ErrorCode{}) {
      char error[5];
      pinkjs::formatErrorCode(completion->error, error);
      runtime->settlePromise(completion->promise,
                             runtime->makeString(error, 4), true);
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
      runtime->settlePromise(completion->promise, response, false);
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

PinkJsUiState *PinkJsApp::uiComponents() {
  if (uiState != nullptr)
    return uiState;
  void *memory = allocatePsram(sizeof(PinkJsUiState));
  if (memory == nullptr)
    return nullptr;
  uiState = new (memory) PinkJsUiState();
  return uiState;
}

void PinkJsApp::releaseUiState() {
  if (uiState == nullptr)
    return;
  uiState->~PinkJsUiState();
  heap_caps_free(uiState);
  uiState = nullptr;
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

pinkjs::Value nativeKeyboardSetText(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  if (activePinkJsApp == nullptr || argc != 1 || !runtime.isString(args[0]))
    return runtime.fail(pinkjs::ErrorCode{208});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  const pinkjs::StringView text = runtime.stringView(args[0]);
  if (text.size >= sizeof(state->keyboardText))
    return runtime.fail(pinkjs::ErrorCode{210});
  memcpy(state->keyboardText, text.data, text.size);
  state->keyboardText[text.size] = '\0';
  return args[0];
}

pinkjs::Value nativeKeyboardGetText(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *, size_t argc,
                                    void *) {
  if (activePinkJsApp == nullptr || argc != 0)
    return runtime.fail(pinkjs::ErrorCode{211});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  return state != nullptr ? runtime.makeString(state->keyboardText)
                          : runtime.fail(pinkjs::ErrorCode{209});
}

pinkjs::Value nativeKeyboardSetLayout(pinkjs::Runtime &runtime,
                                      const pinkjs::Value *args, size_t argc,
                                      void *) {
  if (activePinkJsApp == nullptr || !numericArguments(runtime, args, argc, 1))
    return runtime.undefined();
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  const int layout = static_cast<int>(runtime.toNumber(args[0]));
  state->keyboard.setLayout(layout == 1   ? T9KeyboardComponent::Layout::Numbers
                            : layout == 2 ? T9KeyboardComponent::Layout::Numeric
                                          : T9KeyboardComponent::Layout::Text);
  return runtime.undefined();
}

pinkjs::Value nativeKeyboardDraw(pinkjs::Runtime &runtime,
                                 const pinkjs::Value *args, size_t argc,
                                 void *) {
  if (activePinkJsApp == nullptr || argc > 1)
    return runtime.fail(pinkjs::ErrorCode{212});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  if (pinkJsDrawTarget != nullptr)
    state->keyboard.draw(*pinkJsDrawTarget, state->keyboardText,
                         argc > 0 ? static_cast<int>(runtime.toNumber(args[0]))
                                  : 255);
  return runtime.undefined();
}

pinkjs::Value nativeKeyboardTouch(pinkjs::Runtime &runtime,
                                  const pinkjs::Value *args, size_t argc,
                                  void *) {
  if (activePinkJsApp == nullptr || !numericArguments(runtime, args, argc, 2))
    return runtime.undefined();
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  const KeyboardEvent event = state->keyboard.hitTest(
      TouchPoint{toU16(runtime.toNumber(args[0])),
                 toU16(runtime.toNumber(args[1]))},
      state->keyboardText, sizeof(state->keyboardText));
  static const char *const actions[] = {"none", "char", "backspace", "space",
                                        "shift", "nav", "ok"};
  const size_t action = static_cast<size_t>(event.action);
  pinkjs::Value result = runtime.makeObject();
  runtime.objectSet(result, "action",
                    runtime.makeString(action < sizeof(actions) / sizeof(actions[0])
                                           ? actions[action]
                                           : "none"));
  char value[2] = {event.value, '\0'};
  runtime.objectSet(result, "value", runtime.makeString(value));
  runtime.objectSet(result, "text", runtime.makeString(state->keyboardText));
  return result;
}

pinkjs::Value nativeKeyboardToggleCaps(pinkjs::Runtime &runtime,
                                       const pinkjs::Value *, size_t argc,
                                       void *) {
  if (activePinkJsApp == nullptr || argc != 0)
    return runtime.fail(pinkjs::ErrorCode{213});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  state->keyboard.toggleCaps();
  return runtime.undefined();
}

pinkjs::Value nativeDatePickerReset(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  if (activePinkJsApp == nullptr || !numericArguments(runtime, args, argc, 3))
    return runtime.undefined();
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  state->datePicker.reset({static_cast<int>(runtime.toNumber(args[0])),
                           static_cast<int>(runtime.toNumber(args[1])),
                           static_cast<int>(runtime.toNumber(args[2]))});
  return runtime.undefined();
}

pinkjs::Value nativeDatePickerDraw(pinkjs::Runtime &runtime,
                                   const pinkjs::Value *, size_t argc,
                                   void *) {
  if (activePinkJsApp == nullptr || argc != 0)
    return runtime.fail(pinkjs::ErrorCode{214});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  if (pinkJsDrawTarget != nullptr)
    state->datePicker.draw(*pinkJsDrawTarget);
  return runtime.undefined();
}

pinkjs::Value nativeDatePickerTouch(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *args, size_t argc,
                                    void *) {
  if (activePinkJsApp == nullptr || !numericArguments(runtime, args, argc, 2))
    return runtime.undefined();
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  return runtime.boolean(state->datePicker.handleTouch(
      {toU16(runtime.toNumber(args[0])), toU16(runtime.toNumber(args[1]))}));
}

pinkjs::Value nativeDatePickerValue(pinkjs::Runtime &runtime,
                                    const pinkjs::Value *, size_t argc,
                                    void *) {
  if (activePinkJsApp == nullptr || argc != 0)
    return runtime.fail(pinkjs::ErrorCode{215});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  const DatePickerDate date = state->datePicker.selectedDate();
  const TimePickerTime time = state->datePicker.selectedTime();
  pinkjs::Value result = runtime.makeObject();
  runtime.objectSet(result, "year", runtime.number(date.year));
  runtime.objectSet(result, "month", runtime.number(date.month));
  runtime.objectSet(result, "day", runtime.number(date.day));
  runtime.objectSet(result, "hour", runtime.number(time.hour));
  runtime.objectSet(result, "minute", runtime.number(time.minute));
  return result;
}

pinkjs::Value nativeDatePickerOptions(pinkjs::Runtime &runtime,
                                      const pinkjs::Value *args, size_t argc,
                                      void *) {
  if (activePinkJsApp == nullptr || argc != 2)
    return runtime.fail(pinkjs::ErrorCode{216});
  PinkJsUiState *state = activePinkJsApp->uiComponents();
  if (state == nullptr)
    return runtime.fail(pinkjs::ErrorCode{209});
  state->datePicker.setTimePickerEnabled(runtime.toBoolean(args[0]));
  state->datePicker.setMondayFirst(runtime.toBoolean(args[1]));
  return runtime.undefined();
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
      {"http.get", nativeHttpGet},
      {"fetch", nativeFetch},
      {"setTimeout", nativeSetTimeout},
      {"setInterval", nativeSetInterval},
      {"clearTimeout", nativeClearTimer},
      {"clearInterval", nativeClearTimer},
      {"app.setWakeLock", nativeSetWakeLock},
      {"app.configGet", nativeConfigGet},
      {"app.configSet", nativeConfigSet},
      {"ui.setFont", nativeUiSetFont},
      {"ui.setTextSize", nativeUiSetTextSize},
      {"ui.setTextColor", nativeUiSetTextColor},
      {"ui.setCursor", nativeUiSetCursor},
      {"ui.print", nativeUiPrint},
      {"ui.drawPixel", nativeUiDrawPixel},
      {"ui.drawLine", nativeUiDrawLine},
      {"ui.drawRect", nativeUiDrawRect},
      {"ui.fillRect", nativeUiFillRect},
      {"ui.fillScreen", nativeUiFillScreen},
      {"ui.button", nativeUiButton},
      {"ui.buttonContains", nativeUiButtonContains},
      {"ui.listTouch", nativeUiListTouch},
      {"ui.selectDraw", nativeUiSelectDraw},
      {"ui.selectHit", nativeUiSelectHit},
      {"ui.keyboard.setText", nativeKeyboardSetText},
      {"ui.keyboard.getText", nativeKeyboardGetText},
      {"ui.keyboard.setLayout", nativeKeyboardSetLayout},
      {"ui.keyboard.draw", nativeKeyboardDraw},
      {"ui.keyboard.touch", nativeKeyboardTouch},
      {"ui.keyboard.toggleCaps", nativeKeyboardToggleCaps},
      {"ui.datePicker.reset", nativeDatePickerReset},
      {"ui.datePicker.draw", nativeDatePickerDraw},
      {"ui.datePicker.touch", nativeDatePickerTouch},
      {"ui.datePicker.value", nativeDatePickerValue},
      {"ui.datePicker.options", nativeDatePickerOptions},
  };
  for (const auto &binding : bindings) {
    if (!runtime->bind(binding.path, binding.function)) {
      return false;
    }
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
  options.maxNatives = 64;
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
  Serial.printf("[pinkjs] loaded %s workspace=%lu heap=%lu/%lu peak=%lu\n",
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
