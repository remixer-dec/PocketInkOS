#include "sys/system_fonts.h"

#include "ui/icon_ascii_font.h"

#include <cstring>

namespace {

struct SystemFontEntry {
  SystemFontInfo info;
  const GFXfont *font;
};

// Only fonts already used by the firmware belong here. Generated game/art
// assets are intentionally excluded so this registry does not pull their
// bitmap data into every build.
const SystemFontEntry kFonts[] = {
    {{"default", 8}, nullptr},
    {{"icon-8", 8}, &iconASCII8pt7b},
    {{"icon-12", 12}, &iconASCII12pt7b},
};

} // namespace

size_t systemFontCount() { return sizeof(kFonts) / sizeof(kFonts[0]); }

bool systemFontInfo(size_t index, SystemFontInfo &out) {
  if (index >= systemFontCount()) {
    return false;
  }
  out = kFonts[index].info;
  return true;
}

int systemFontIndex(const char *name) {
  if (name == nullptr) {
    return -1;
  }
  for (size_t i = 0; i < systemFontCount(); i++) {
    if (strcmp(kFonts[i].info.name, name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool systemFontApply(Adafruit_GFX &gfx, size_t index) {
  if (index >= systemFontCount()) {
    return false;
  }
  gfx.setFont(kFonts[index].font);
  return true;
}

bool systemFontApply(Adafruit_GFX &gfx, const char *name) {
  const int index = systemFontIndex(name);
  return index >= 0 && systemFontApply(gfx, static_cast<size_t>(index));
}
