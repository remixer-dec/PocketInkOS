#pragma once

#include <cstddef>
#include <cstdlib>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define POCKETINK_HAS_HEAP_CAPS 1
#else
#define POCKETINK_HAS_HEAP_CAPS 0
#endif

namespace psram {

inline void *allocate(size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}

inline void *allocateOrDefault(size_t bytes) {
  void *memory = allocate(bytes);
#if POCKETINK_HAS_HEAP_CAPS
  if (memory == nullptr) {
    memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
#endif
  return memory;
}

inline void *allocateOrInternal(size_t bytes) {
  void *memory = allocate(bytes);
#if POCKETINK_HAS_HEAP_CAPS
  if (memory == nullptr) {
    memory = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
#endif
  return memory;
}

inline void *allocateInternalOrPsram(size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS
  void *memory =
      heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return memory != nullptr ? memory : allocate(bytes);
#else
  return std::malloc(bytes);
#endif
}

inline void *reallocateOrDefault(void *memory, size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS &&                                               \
    (defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32))
  void *resized = heap_caps_realloc(
      memory, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (resized != nullptr || bytes == 0) {
    return resized;
  }
  return heap_caps_realloc(memory, bytes, MALLOC_CAP_8BIT);
#else
  return std::realloc(memory, bytes);
#endif
}

inline void *reallocate(void *memory, size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS
  return heap_caps_realloc(memory, bytes,
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::realloc(memory, bytes);
#endif
}

inline void release(void *memory) {
#if POCKETINK_HAS_HEAP_CAPS
  heap_caps_free(memory);
#else
  std::free(memory);
#endif
}

} // namespace psram
