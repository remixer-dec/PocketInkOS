#ifndef TEXT_INPUT_CONTROLLER_H
#define TEXT_INPUT_CONTROLLER_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/touch_input.h"
#include "ui/components/keyboard_component.h"
#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"
#include "ui/t9_keyboard/t9_keyboard_component.h"
#include <Arduino.h>

class TextInputController {
public:
  bool isScreen(Screen screen) const;
  bool toggleCaps(Screen screen);
  Screen nextMode(Screen screen) const;
  void draw(AppDisplay &display, Screen screen);
  bool handleTouch(Screen screen, const TouchPoint &point);

private:
  static const uint8_t MAX_TEXT_LENGTH = 64;

  KeyboardComponent keyboard;
  QwertyZoomKeyboardComponent qwertyZoomKeyboard;
  T9KeyboardComponent t9Keyboard;
  String text;

  bool applyEvent(const KeyboardEvent &event);
};

#endif
