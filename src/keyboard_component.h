#ifndef KEYBOARD_COMPONENT_H
#define KEYBOARD_COMPONENT_H

#include "touch_input.h"
#include <Adafruit_GFX.h>

enum KeyboardAction {
  KEY_NONE,
  KEY_CHAR,
  KEY_BACKSPACE,
  KEY_SPACE,
  KEY_CLEAR
};

struct KeyboardEvent {
  KeyboardAction action;
  char value;
};

class KeyboardComponent {
public:
  void draw(Adafruit_GFX &gfx, const String &text);
  KeyboardEvent hitTest(const TouchPoint &point) const;

private:
  KeyboardEvent hitRow(const TouchPoint &point, const char *keys, int count,
                       int x, int y) const;
};

#endif
