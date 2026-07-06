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

/// Draws and hit-tests the compact QWERTY keyboard.
class KeyboardComponent {
public:
  /// Draws keyboard input and keys.
  void draw(Adafruit_GFX &gfx, const String &text);
  /// Draws keyboard input and keys with a maximum input length.
  void draw(Adafruit_GFX &gfx, const String &text, int maxLength);
  /// Draws only the input field.
  void drawInput(Adafruit_GFX &gfx, const String &text);
  /// Draws only the input field with a maximum input length.
  void drawInput(Adafruit_GFX &gfx, const String &text, int maxLength);
  /// Maps a touch point to a keyboard event.
  KeyboardEvent hitTest(const TouchPoint &point);
  /// Maps a touch point to a keyboard event with length limiting.
  KeyboardEvent hitTest(const TouchPoint &point, int currentLength,
                        int maxLength);
  /// Toggles letter casing.
  void toggleCaps();

private:
  bool caps = false;
  int inputLimit = 0;

  KeyboardEvent hitRow(const TouchPoint &point, const char *keys, int count,
                       int x, int y, int keyW) const;
};

#endif
