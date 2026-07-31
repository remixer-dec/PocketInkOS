#pragma once
#include <cstdint>
inline uint32_t esp_random() {
  static uint32_t state = 0x9e3779b9U;
  state ^= state << 13U;
  state ^= state >> 17U;
  state ^= state << 5U;
  return state;
}
