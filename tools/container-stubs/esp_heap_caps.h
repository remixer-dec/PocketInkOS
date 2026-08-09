#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_INTERNAL 8
#define MALLOC_CAP_EXEC 16
#define MALLOC_CAP_32BIT 32
struct multi_heap_info_t {
  size_t total_free_bytes;
  size_t total_allocated_bytes;
  size_t largest_free_block;
  size_t minimum_free_bytes;
  size_t allocated_blocks;
  size_t free_blocks;
  size_t total_blocks;
};

struct heap_caps_tracking_snapshot_t {
  size_t current_bytes;
  size_t peak_bytes;
  size_t total_allocated_bytes;
  size_t allocation_count;
};

struct heap_caps_tracking_header_t {
  size_t size;
  int caps;
  uint32_t magic;
};

inline heap_caps_tracking_snapshot_t &heap_caps_tracking_internal_stats() {
  static heap_caps_tracking_snapshot_t stats = {};
  return stats;
}

inline heap_caps_tracking_snapshot_t &heap_caps_tracking_spiram_stats() {
  static heap_caps_tracking_snapshot_t stats = {};
  return stats;
}

inline heap_caps_tracking_snapshot_t &heap_caps_tracking_total_stats() {
  static heap_caps_tracking_snapshot_t stats = {};
  return stats;
}

inline std::atomic<std::ptrdiff_t> &heap_caps_failure_countdown() {
  static std::atomic<std::ptrdiff_t> countdown{-1};
  return countdown;
}

/// Test-only deterministic allocation failure: allow `successes` allocations,
/// then fail subsequent allocations until disabled.
inline void heap_caps_fail_after(std::ptrdiff_t successes) {
  heap_caps_failure_countdown().store(successes, std::memory_order_release);
}

inline void heap_caps_disable_failures() {
  heap_caps_failure_countdown().store(-1, std::memory_order_release);
}

inline void heap_caps_tracking_add(heap_caps_tracking_snapshot_t &stats,
                                   size_t n) {
  stats.current_bytes += n;
  stats.total_allocated_bytes += n;
  stats.allocation_count++;
  if (stats.current_bytes > stats.peak_bytes) {
    stats.peak_bytes = stats.current_bytes;
  }
}

inline void heap_caps_tracking_sub(heap_caps_tracking_snapshot_t &stats,
                                   size_t n) {
  stats.current_bytes = stats.current_bytes > n ? stats.current_bytes - n : 0;
}

inline void heap_caps_reset_tracking() {
  heap_caps_tracking_internal_stats() = {};
  heap_caps_tracking_spiram_stats() = {};
  heap_caps_tracking_total_stats() = {};
}

inline heap_caps_tracking_snapshot_t heap_caps_get_tracking(int caps) {
  if ((caps & MALLOC_CAP_SPIRAM) != 0) {
    return heap_caps_tracking_spiram_stats();
  }
  if ((caps & MALLOC_CAP_INTERNAL) != 0) {
    return heap_caps_tracking_internal_stats();
  }
  return heap_caps_tracking_total_stats();
}

inline void *heap_caps_malloc(size_t n, int caps) {
  constexpr uint32_t kMagic = 0x48434150U;
  std::ptrdiff_t remaining =
      heap_caps_failure_countdown().load(std::memory_order_acquire);
  while (remaining >= 0) {
    if (remaining == 0) {
      return nullptr;
    }
    if (heap_caps_failure_countdown().compare_exchange_weak(
            remaining, remaining - 1, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      break;
    }
  }
  void *raw = std::malloc(sizeof(heap_caps_tracking_header_t) + n);
  if (raw == nullptr) {
    return nullptr;
  }
  auto *header = static_cast<heap_caps_tracking_header_t *>(raw);
  header->size = n;
  header->caps = caps;
  header->magic = kMagic;
  heap_caps_tracking_add(heap_caps_tracking_total_stats(), n);
  if ((caps & MALLOC_CAP_SPIRAM) != 0) {
    heap_caps_tracking_add(heap_caps_tracking_spiram_stats(), n);
  } else {
    heap_caps_tracking_add(heap_caps_tracking_internal_stats(), n);
  }
  return header + 1;
}

inline void heap_caps_free(void *p) {
  if (p == nullptr) {
    return;
  }
  constexpr uint32_t kMagic = 0x48434150U;
  auto *header = static_cast<heap_caps_tracking_header_t *>(p) - 1;
  if (header->magic == kMagic) {
    heap_caps_tracking_sub(heap_caps_tracking_total_stats(), header->size);
    if ((header->caps & MALLOC_CAP_SPIRAM) != 0) {
      heap_caps_tracking_sub(heap_caps_tracking_spiram_stats(), header->size);
    } else {
      heap_caps_tracking_sub(heap_caps_tracking_internal_stats(), header->size);
    }
    header->magic = 0;
    std::free(header);
    return;
  }
  std::free(p);
}
inline void *heap_caps_realloc(void *p, size_t n, int caps) {
  if (p == nullptr) {
    return heap_caps_malloc(n, caps);
  }
  if (n == 0) {
    heap_caps_free(p);
    return nullptr;
  }
  constexpr uint32_t kMagic = 0x48434150U;
  auto *header = static_cast<heap_caps_tracking_header_t *>(p) - 1;
  if (header->magic != kMagic) {
    return std::realloc(p, n);
  }
  const size_t oldSize = header->size;
  void *resized = heap_caps_malloc(n, caps);
  if (resized == nullptr) {
    return nullptr;
  }
  std::memcpy(resized, p, oldSize < n ? oldSize : n);
  heap_caps_free(p);
  return resized;
}
inline size_t heap_caps_get_free_size(int) { return 256 * 1024; }
inline size_t heap_caps_get_largest_free_block(int) { return 128 * 1024; }
inline void heap_caps_get_info(multi_heap_info_t *info, int) {
  if (info) {
    info->total_free_bytes = 256 * 1024;
    info->total_allocated_bytes = 0;
    info->largest_free_block = 128 * 1024;
    info->minimum_free_bytes = 200 * 1024;
    info->allocated_blocks = 0;
    info->free_blocks = 1;
    info->total_blocks = 1;
  }
}
