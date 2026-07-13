#include <stddef.h>

extern "C" void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *out = static_cast<unsigned char *>(dest);
  const unsigned char *in = static_cast<const unsigned char *>(src);
  for (size_t i = 0; i < n; i++) {
    out[i] = in[i];
  }
  return dest;
}

extern "C" void *memset(void *dest, int value, size_t n) {
  unsigned char *out = static_cast<unsigned char *>(dest);
  const unsigned char byte = static_cast<unsigned char>(value);
  for (size_t i = 0; i < n; i++) {
    out[i] = byte;
  }
  return dest;
}

extern "C" void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *out = static_cast<unsigned char *>(dest);
  const unsigned char *in = static_cast<const unsigned char *>(src);
  if (out <= in) {
    for (size_t i = 0; i < n; i++) {
      out[i] = in[i];
    }
  } else {
    for (size_t i = n; i > 0; i--) {
      out[i - 1] = in[i - 1];
    }
  }
  return dest;
}

// Small freestanding math primitive used by codecs. Unreferenced helpers are
// removed by --gc-sections from ordinary .pink apps.
extern "C" float sqrtf(float value) {
  if (value <= 0.0f) {
    return 0.0f;
  }
  union {
    float number;
    unsigned int bits;
  } estimate = {value};
  estimate.bits = (estimate.bits >> 1U) + 0x1fc00000U;
  float root = estimate.number;
  for (unsigned int i = 0; i < 4U; i++) {
    root = 0.5f * (root + value / root);
  }
  return root;
}

extern "C" float ldexpf(float value, int exponent) {
  if (value == 0.0f) {
    return value;
  }
  union {
    float number;
    unsigned int bits;
  } scaled = {value};
  const unsigned int sign = scaled.bits & 0x80000000U;
  int current = static_cast<int>((scaled.bits >> 23U) & 0xffU);
  unsigned int fraction = scaled.bits & 0x007fffffU;
  if (current == 0) {
    // Normalize the uncommon subnormal input without another library call.
    while ((scaled.bits & 0x00800000U) == 0U) {
      value *= 2.0f;
      scaled.number = value;
      exponent--;
    }
    current = static_cast<int>((scaled.bits >> 23U) & 0xffU);
    fraction = scaled.bits & 0x007fffffU;
  }
  const int target = current + exponent;
  if (target <= 0) {
    return 0.0f;
  }
  if (target >= 255) {
    scaled.bits = sign | 0x7f800000U;
    return scaled.number;
  }
  scaled.bits = sign | (static_cast<unsigned int>(target) << 23U) | fraction;
  return scaled.number;
}
