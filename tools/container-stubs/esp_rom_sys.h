#pragma once
#include <cstdarg>
#include <cstdio>
inline void esp_rom_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}
