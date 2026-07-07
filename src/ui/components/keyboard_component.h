#ifndef KEYBOARD_COMPONENT_H
#define KEYBOARD_COMPONENT_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>

/// Actions emitted by keyboard hit testing.
enum KeyboardAction {
  KEY_NONE,
  KEY_CHAR,
  KEY_BACKSPACE,
  KEY_SPACE,
  KEY_SHIFT,
  KEY_NAV,
  KEY_OK
};

/// Keyboard hit-test result.
struct KeyboardEvent {
  KeyboardAction action;
  char value;
};

/// Shared contract for keyboard components backed by external text state.
class KeyboardComponent {
public:
  virtual ~KeyboardComponent() = default;

  virtual void draw(Adafruit_GFX &gfx, const String &text) = 0;
  virtual void draw(Adafruit_GFX &gfx, const String &text, int maxLength) = 0;
  virtual KeyboardEvent hitTest(const TouchPoint &point) = 0;
  virtual KeyboardEvent hitTest(const TouchPoint &point, int currentLength,
                                int maxLength) = 0;
  virtual void toggleCaps() = 0;
};

#endif
