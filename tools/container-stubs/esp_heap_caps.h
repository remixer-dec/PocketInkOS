#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
inline void *heap_caps_malloc(size_t n, int) { return std::malloc(n); }
inline void heap_caps_free(void *p) { std::free(p); }
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
