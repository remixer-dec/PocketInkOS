/// Small allocation policy helpers for large firmware data structures. They
/// prefer ESP32 PSRAM, offer explicit fallback orderings for constrained paths,
/// and map to the standard heap in host builds so the same code remains testable.
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

/// Allocates only from PSRAM on ESP32, with no internal-memory fallback.
inline void *allocate(size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}

/// Prefers PSRAM, then permits any 8-bit-capable heap as a fallback.
inline void *allocateOrDefault(size_t bytes) {
  void *memory = allocate(bytes);
#if POCKETINK_HAS_HEAP_CAPS
  if (memory == nullptr) {
    memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
#endif
  return memory;
}

/// Prefers PSRAM, then explicitly falls back to internal 8-bit memory.
inline void *allocateOrInternal(size_t bytes) {
  void *memory = allocate(bytes);
#if POCKETINK_HAS_HEAP_CAPS
  if (memory == nullptr) {
    memory = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
#endif
  return memory;
}

/// Prefers low-latency internal memory and uses PSRAM only when necessary.
inline void *allocateInternalOrPsram(size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS
  void *memory =
      heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return memory != nullptr ? memory : allocate(bytes);
#else
  return std::malloc(bytes);
#endif
}

/// Resizes a block preferring PSRAM, then the default 8-bit heap.
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

/// Resizes a block in PSRAM without changing the allocation policy.
inline void *reallocate(void *memory, size_t bytes) {
#if POCKETINK_HAS_HEAP_CAPS
  return heap_caps_realloc(memory, bytes,
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return std::realloc(memory, bytes);
#endif
}

/// Releases memory returned by any allocator in this namespace.
inline void release(void *memory) {
#if POCKETINK_HAS_HEAP_CAPS
  heap_caps_free(memory);
#else
  std::free(memory);
#endif
}

} // namespace psram
