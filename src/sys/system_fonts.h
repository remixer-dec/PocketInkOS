#ifndef SYSTEM_FONTS_H
#define SYSTEM_FONTS_H

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Describes a font already compiled into the firmware.
struct SystemFontInfo {
  const char *name = nullptr;
  uint8_t nominalSize = 0;
};

size_t systemFontCount();
bool systemFontInfo(size_t index, SystemFontInfo &out);
int systemFontIndex(const char *name);
bool systemFontApply(Adafruit_GFX &gfx, size_t index);
bool systemFontApply(Adafruit_GFX &gfx, const char *name);

#endif
