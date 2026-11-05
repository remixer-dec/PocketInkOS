#include "sys/system_fonts.h"

#include "ui/icon_ascii_font.h"
#include "ui/inkfont/inktext.h"

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

constexpr size_t kGfxFontCount = sizeof(kFonts) / sizeof(kFonts[0]);

// Text state for the one surface a runtime draws to. Adafruit_GFX keeps color
// and scale private, so the setters below mirror them for InkFont printing.
struct TextState {
  Adafruit_GFX *gfx = nullptr;
  const InkFont *ink = nullptr;
  uint16_t color = 1;
  uint8_t size = 1;
};

TextState textState;

TextState &stateFor(Adafruit_GFX &gfx) {
  if (textState.gfx != &gfx) {
    textState = TextState{};
    textState.gfx = &gfx;
  }
  return textState;
}

} // namespace

// GFX fonts come first so existing numeric font indices stay stable.
size_t systemFontCount() { return kGfxFontCount + inkFontBuiltinCount(); }

bool systemFontInfo(size_t index, SystemFontInfo &out) {
  if (index < kGfxFontCount) {
    out = kFonts[index].info;
    return true;
  }
  const InkFont *ink = inkFontBuiltin(index - kGfxFontCount);
  if (ink == nullptr) {
    return false;
  }
  out.name = inkFontBuiltinName(index - kGfxFontCount);
  out.nominalSize = ink->size();
  return true;
}

int systemFontIndex(const char *name) {
  if (name == nullptr) {
    return -1;
  }
  SystemFontInfo info;
  for (size_t i = 0; systemFontInfo(i, info); i++) {
    if (strcmp(info.name, name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool systemFontApply(Adafruit_GFX &gfx, size_t index) {
  if (index >= systemFontCount()) {
    return false;
  }
  const bool gfxFont = index < kGfxFontCount;
  gfx.setFont(gfxFont ? kFonts[index].font : nullptr);
  stateFor(gfx).ink = gfxFont ? nullptr : inkFontBuiltin(index - kGfxFontCount);
  return true;
}

bool systemFontApply(Adafruit_GFX &gfx, const char *name) {
  const int index = systemFontIndex(name);
  return index >= 0 && systemFontApply(gfx, static_cast<size_t>(index));
}

void systemFontSetTextColor(Adafruit_GFX &gfx, uint16_t color) {
  gfx.setTextColor(color);
  stateFor(gfx).color = color;
}

void systemFontSetTextSize(Adafruit_GFX &gfx, uint8_t size) {
  gfx.setTextSize(size);
  stateFor(gfx).size = size ? size : 1;
}

void systemFontPrint(Adafruit_GFX &gfx, const char *text, size_t length) {
  if (text == nullptr) {
    return;
  }
  const TextState &state = stateFor(gfx);
  if (state.ink == nullptr) {
    if (length == SIZE_MAX) {
      gfx.print(text);
    } else {
      for (size_t i = 0; i < length && text[i] != '\0'; i++) {
        gfx.print(text[i]);
      }
    }
    return;
  }
  InkTextStyle style;
  style.font = state.ink;
  style.color = state.color;
  style.size = state.size > 1 ? static_cast<uint8_t>(state.ink->size() * state.size) : 0;
  InkTextCursor cursor(gfx, style);
  cursor.setCursor(gfx.getCursorX(), gfx.getCursorY());
  cursor.print(text, length);
  gfx.setCursor(cursor.x(), cursor.y());
}
