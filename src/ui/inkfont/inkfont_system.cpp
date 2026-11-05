// Built-in font registry and the SD-card Unicode fallback behind it.
#include "ui/inkfont/inkfont.h"

#include "ui/inkfont/fonts/display_32.h"
#include "ui/inkfont/fonts/sans_12.h"
#include "ui/inkfont/fonts/sans_16.h"
#include "ui/inkfont/fonts/sans_bold_12.h"
#include "ui/inkfont/fonts/sans_bold_16.h"

#include <SD_MMC.h>
#include <string.h>

const char *const INK_FONT_UNICODE_PATH = "/fonts/unicode.ifnt";

namespace {

struct BuiltinFont {
  const char *name;
  InkFont font;
};

// Function-local so other static initializers can safely use the fonts.
BuiltinFont *builtins() {
  static BuiltinFont fonts[] = {
      {"sans-12", {kInkFontSans12, sizeof(kInkFontSans12)}},
      {"sans-bold-12", {kInkFontSansBold12, sizeof(kInkFontSansBold12)}},
      {"sans-16", {kInkFontSans16, sizeof(kInkFontSans16)}},
      {"sans-bold-16", {kInkFontSansBold16, sizeof(kInkFontSansBold16)}},
      {"display-32", {kInkFontDisplay32, sizeof(kInkFontDisplay32)}},
  };
  static bool chained = false;
  if (!chained) {
    // The display face only carries Latin-1; other scripts come from the
    // bold 16px face at twice its size.
    fonts[4].font.fallback = &fonts[3].font;
    chained = true;
  }
  return fonts;
}

constexpr size_t kBuiltinCount = 5;
constexpr size_t kDisplayIndex = 4;

InkFont &unicodeFont() {
  static InkFont font;
  return font;
}

void attachFallback(const InkFont *fallback) {
  BuiltinFont *fonts = builtins();
  for (size_t i = 0; i < kBuiltinCount; i++) {
    if (i != kDisplayIndex) {
      fonts[i].font.fallback = fallback;
    }
  }
}

} // namespace

size_t inkFontBuiltinCount() { return kBuiltinCount; }

const char *inkFontBuiltinName(size_t index) {
  return index < kBuiltinCount ? builtins()[index].name : nullptr;
}

const InkFont *inkFontBuiltin(size_t index) {
  return index < kBuiltinCount ? &builtins()[index].font : nullptr;
}

const InkFont *inkFontNamed(const char *name) {
  if (name == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < kBuiltinCount; i++) {
    if (strcmp(builtins()[i].name, name) == 0) {
      return &builtins()[i].font;
    }
  }
  return nullptr;
}

const InkFont &inkFontDefault() { return builtins()[0].font; }

void inkFontStorageChanged(bool mounted) {
  InkFont &font = unicodeFont();
  if (!mounted) {
    attachFallback(nullptr);
    font.close();
    return;
  }
  if (!font.valid() && font.openFile(SD_MMC.open(INK_FONT_UNICODE_PATH))) {
    attachFallback(&font);
  }
}
