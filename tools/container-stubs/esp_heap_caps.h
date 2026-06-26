#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_INTERNAL 8
#define MALLOC_CAP_EXEC 16
#define MALLOC_CAP_32BIT 32
inline void *heap_caps_malloc(size_t n, int) { return std::malloc(n); }
inline void heap_caps_free(void *p) { std::free(p); }
