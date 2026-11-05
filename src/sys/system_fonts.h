/// Registry of fonts deliberately linked into every firmware build. It lets
/// runtime-driven UI discover and select known fonts by stable names without
/// pulling unrelated generated font assets into the binary. Entries are either
/// Adafruit GFX fonts or Unicode InkFonts (ui/inkfont); the text helpers below
/// route printing to whichever kind is applied, so callers stay agnostic.
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

/// Sets the text color on gfx and remembers it for Unicode fonts.
void systemFontSetTextColor(Adafruit_GFX &gfx, uint16_t color);
/// Sets the text scale on gfx; Unicode fonts render at nominal size * scale.
void systemFontSetTextSize(Adafruit_GFX &gfx, uint8_t size);
/// Prints at the gfx cursor with the applied font and advances the cursor.
/// Unicode fonts decode UTF-8, wrap at the surface edge and treat the cursor
/// y as the top of the line. `length` == SIZE_MAX reads up to the NUL.
void systemFontPrint(Adafruit_GFX &gfx, const char *text,
                     size_t length = SIZE_MAX);

#endif
