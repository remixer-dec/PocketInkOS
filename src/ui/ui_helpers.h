#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>
#include <cstring>

struct UiRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

inline bool uiContains(const UiRect &rect, const TouchPoint &point) {
  return point.x >= rect.x && point.x < rect.x + rect.w && point.y >= rect.y &&
         point.y < rect.y + rect.h;
}

inline void uiDrawButton(Adafruit_GFX &gfx, const UiRect &rect,
                         const char *label, bool selected = false,
                         uint8_t textSize = 1) {
  gfx.drawRect(rect.x, rect.y, rect.w, rect.h, 1);
  if (selected && rect.w > 2 && rect.h > 2) {
    gfx.fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 1);
  }

  gfx.setTextSize(textSize);
  gfx.setTextColor(selected ? 0 : 1);
  int16_t textX =
      rect.x + (rect.w - (int16_t)strlen(label) * 6 * textSize) / 2;
  int16_t textY = rect.y + (rect.h - 8 * textSize) / 2;
  gfx.setCursor(textX, textY);
  gfx.print(label);
  gfx.setTextColor(1);
}

#endif
