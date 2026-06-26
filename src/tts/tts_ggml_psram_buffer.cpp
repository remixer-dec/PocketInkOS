#include "tts/tts_ggml_psram_buffer.h"

#include <ggml-backend-impl.h>
#include <ggml-impl.h>
#include <esp_heap_caps.h>

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define TTS_PSRAM_HAS_ROM_PRINTF 1
#else
#define TTS_PSRAM_HAS_ROM_PRINTF 0
#endif

namespace tts {
namespace {

void ttsLog(const char *format, ...) {
  char line[160];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
#if TTS_PSRAM_HAS_ROM_PRINTF
  esp_rom_printf("%s", line);
#else
  Serial.print(line);
#endif
}

void *alignedBase(ggml_backend_buffer_t buffer) {
  uintptr_t data = reinterpret_cast<uintptr_t>(buffer->context);
  if (data % TENSOR_ALIGNMENT != 0) {
    data = GGML_PAD(data, TENSOR_ALIGNMENT);
  }
  return reinterpret_cast<void *>(data);
}

void freeBuffer(ggml_backend_buffer_t buffer) {
  if (buffer != nullptr && buffer->context != nullptr) {
    heap_caps_free(buffer->context);
  }
}

void memsetTensor(ggml_backend_buffer_t, ggml_tensor *tensor, uint8_t value,
                  size_t offset, size_t size) {
  memset(static_cast<char *>(tensor->data) + offset, value, size);
}

void setTensor(ggml_backend_buffer_t, ggml_tensor *tensor, const void *data,
               size_t offset, size_t size) {
  memcpy(static_cast<char *>(tensor->data) + offset, data, size);
}

void getTensor(ggml_backend_buffer_t, const ggml_tensor *tensor, void *data,
               size_t offset, size_t size) {
  memcpy(data, static_cast<const char *>(tensor->data) + offset, size);
}

bool copyTensor(ggml_backend_buffer_t, const ggml_tensor *src,
                ggml_tensor *dst) {
  if (!ggml_backend_buffer_is_host(src->buffer)) {
    return false;
  }
  memcpy(dst->data, src->data, ggml_nbytes(src));
  return true;
}

void clearBuffer(ggml_backend_buffer_t buffer, uint8_t value) {
  memset(alignedBase(buffer), value, buffer->size);
}

const ggml_backend_buffer_i kPsramBufferInterface = {
    freeBuffer,
    alignedBase,
    nullptr,
    memsetTensor,
    setTensor,
    getTensor,
    nullptr,
    nullptr,
    copyTensor,
    clearBuffer,
    nullptr,
};

const char *bufferTypeName(ggml_backend_buffer_type_t) { return "ESP32_PSRAM"; }

ggml_backend_buffer_t allocBuffer(ggml_backend_buffer_type_t buft,
                                  size_t size) {
  void *data = heap_caps_malloc(size + TENSOR_ALIGNMENT,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (data == nullptr) {
    ttsLog("[tts] ggml psram alloc failed bytes=%u\n",
           static_cast<unsigned>(size + TENSOR_ALIGNMENT));
    return nullptr;
  }
  ttsLog("[tts] ggml psram allocated bytes=%u\n",
         static_cast<unsigned>(size));
  return ggml_backend_buffer_init(buft, kPsramBufferInterface, data, size);
}

size_t alignment(ggml_backend_buffer_type_t) { return TENSOR_ALIGNMENT; }

bool isHost(ggml_backend_buffer_type_t) { return true; }

ggml_backend_buffer_type kPsramBufferType = {
    {bufferTypeName, allocBuffer, alignment, nullptr, nullptr, isHost},
    nullptr,
    nullptr,
};

} // namespace

ggml_backend_buffer_type_t psramCpuBufferType() { return &kPsramBufferType; }

} // namespace tts
