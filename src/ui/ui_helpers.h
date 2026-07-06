#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "ui/components/touch_button.h"
#include <Adafruit_GFX.h>
#include <Arduino.h>

static const unsigned long UI_PRESS_FEEDBACK_MS = 180;
static const uint8_t UI_TOUCH_ASSIST_PX = 2;

/// Small helper for pressed feedback in screens without a full component.
///
/// Screens store an integer control id, call press() from hit testing, and call
/// update() from their loop to know when to redraw the released state.
struct UiPressFeedback {
  int8_t id = -1;
  unsigned long until = 0;

  /// Clears any active pressed control immediately.
  void clear() {
    id = -1;
    until = 0;
  }

  /// Starts a short pressed interval for a control id.
  void press(int8_t nextId,
             unsigned long durationMs = UI_PRESS_FEEDBACK_MS) {
    id = nextId;
    until = millis() + durationMs;
  }

  /// Checks whether a control id should currently draw as pressed.
  bool isPressed(int8_t queryId) const {
    return id == queryId && millis() < until;
  }

  /// Expires feedback and reports whether the screen should redraw.
  bool update() {
    if (id < 0 || millis() < until) {
      return false;
    }
    clear();
    return true;
  }
};

/// Hit-tests a standard rectangular control using TouchButton semantics.
inline bool uiContains(const UiRect &rect, const TouchPoint &point) {
  return TouchButton(rect).contains(point);
}

/// Hit-tests a small visual control with extra invisible touch padding.
inline bool uiTouchContains(const UiRect &rect, const TouchPoint &point,
                            uint8_t touchPadding = UI_TOUCH_ASSIST_PX) {
  TouchButtonContent content;
  TouchButtonStyle style;
  style.touchPadding = touchPadding;
  return TouchButton(rect, content, TouchButtonShape::rectangle(), style)
      .contains(point);
}

/// Draws the common compact monochrome text button used across shell screens.
inline void uiDrawButton(Adafruit_GFX &gfx, const UiRect &rect,
                         const char *label, bool selected = false,
                         uint8_t textSize = 1) {
  TouchButtonContent content;
  content.text = label;
  content.textSize = textSize;
  TouchButton(rect, content).draw(gfx, selected);
}

#endif
