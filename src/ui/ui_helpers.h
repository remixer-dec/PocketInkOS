#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "ui/components/touch_button.h"
#include <Adafruit_GFX.h>
#include <Arduino.h>

static const unsigned long UI_PRESS_FEEDBACK_MS = 180;
static const uint8_t UI_TOUCH_ASSIST_PX = 2;

struct UiPressFeedback {
  int8_t id = -1;
  unsigned long until = 0;

  void clear() {
    id = -1;
    until = 0;
  }

  void press(int8_t nextId,
             unsigned long durationMs = UI_PRESS_FEEDBACK_MS) {
    id = nextId;
    until = millis() + durationMs;
  }

  bool isPressed(int8_t queryId) const {
    return id == queryId && millis() < until;
  }

  bool update() {
    if (id < 0 || millis() < until) {
      return false;
    }
    clear();
    return true;
  }
};

inline bool uiContains(const UiRect &rect, const TouchPoint &point) {
  return TouchButton(rect).contains(point);
}

inline bool uiTouchContains(const UiRect &rect, const TouchPoint &point,
                            uint8_t touchPadding = UI_TOUCH_ASSIST_PX) {
  TouchButtonContent content;
  TouchButtonStyle style;
  style.touchPadding = touchPadding;
  return TouchButton(rect, content, TouchButtonShape::rectangle(), style)
      .contains(point);
}

inline void uiDrawButton(Adafruit_GFX &gfx, const UiRect &rect,
                         const char *label, bool selected = false,
                         uint8_t textSize = 1) {
  TouchButtonContent content;
  content.text = label;
  content.textSize = textSize;
  TouchButton(rect, content).draw(gfx, selected);
}

#endif
