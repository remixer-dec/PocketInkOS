#ifndef KEYBOARD_COMPONENT_H
#define KEYBOARD_COMPONENT_H

#include "sys/touch_input.h"
#include <Adafruit_GFX.h>

enum KeyboardAction {
  KEY_NONE,
  KEY_CHAR,
  KEY_BACKSPACE,
  KEY_SPACE,
  KEY_SHIFT,
  KEY_NAV,
  KEY_OK
};

struct KeyboardEvent {
  KeyboardAction action;
  char value;
};

class KeyboardComponent {
public:
  void draw(Adafruit_GFX &gfx, const String &text);
  void draw(Adafruit_GFX &gfx, const String &text, int maxLength);
  void drawInput(Adafruit_GFX &gfx, const String &text);
  void drawInput(Adafruit_GFX &gfx, const String &text, int maxLength);
  KeyboardEvent hitTest(const TouchPoint &point);
  KeyboardEvent hitTest(const TouchPoint &point, int currentLength,
                        int maxLength);
  void toggleCaps();

private:
  bool caps = false;
  int inputLimit = 0;

  KeyboardEvent hitRow(const TouchPoint &point, const char *keys, int count,
                       int x, int y, int keyW) const;
};

#endif
