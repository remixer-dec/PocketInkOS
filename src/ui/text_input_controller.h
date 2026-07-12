#ifndef TEXT_INPUT_CONTROLLER_H
#define TEXT_INPUT_CONTROLLER_H

#include "sys/app_display.h"
#include "sys/app_runtime.h"
#include "sys/services/touch_input.h"
#include "ui/components/t9_keyboard_component.h"
#include "ui/qwerty_zoom/qwerty_zoom_keyboard_component.h"
#include <Arduino.h>

/// Shared controller for the firmware text-entry modes.
///
/// Apps switch to these screens instead of owning keyboard widgets directly.
/// The controller keeps the current draft text and routes draw/touch behavior
/// to the selected keyboard implementation.
class TextInputController {
public:
  /// Identifies screens that should be handled by this controller.
  bool isScreen(Screen screen) const;
  /// Toggles capitalization in the keyboard backing the given screen.
  bool toggleCaps(Screen screen);
  /// Cycles between text-entry modes for the shell keyboard shortcut.
  Screen nextMode(Screen screen) const;
  /// Renders the keyboard UI for the current text-entry screen.
  void draw(AppDisplay &display, Screen screen);
  /// Applies keyboard touches to the shared text buffer.
  bool handleTouch(Screen screen, const TouchPoint &point);

private:
  static const uint8_t MAX_TEXT_LENGTH = 64;

  QwertyZoomKeyboardComponent qwertyZoomKeyboard;
  T9KeyboardComponent t9Keyboard;
  String text;

  bool applyEvent(const KeyboardEvent &event);
};

#endif
