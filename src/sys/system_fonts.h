/// Registry of fonts deliberately linked into every firmware build. It lets
/// runtime-driven UI discover and select known GFX fonts by stable names without
/// pulling unrelated generated font assets into the binary.
#ifndef SYSTEM_FONTS_H
#define SYSTEM_FONTS_H

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

/// Describes a font already compiled into the firmware.
struct SystemFontInfo {
  /// Stable name accepted by systemFontIndex() and systemFontApply().
  const char *name = nullptr;
  /// Approximate point size for discovery and UI presentation.
  uint8_t nominalSize = 0;
};

/// Returns the number of fonts in the firmware-wide registry.
size_t systemFontCount();
/// Copies metadata for one registry entry; returns false out of range.
bool systemFontInfo(size_t index, SystemFontInfo &out);
/// Looks up a stable font name, returning -1 when it is unknown.
int systemFontIndex(const char *name);
/// Applies a registry font by index to a GFX drawing surface.
bool systemFontApply(Adafruit_GFX &gfx, size_t index);
/// Applies a registry font by name to a GFX drawing surface.
bool systemFontApply(Adafruit_GFX &gfx, const char *name);

#endif
