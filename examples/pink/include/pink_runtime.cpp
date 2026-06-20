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
