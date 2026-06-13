#include "ui/text_input_controller.h"

bool TextInputController::isScreen(Screen screen) const {
  return screen == SCREEN_KEYBOARD || screen == SCREEN_T9_KEYBOARD ||
         screen == SCREEN_QWERTY_ZOOM_KEYBOARD;
}

bool TextInputController::toggleCaps(Screen screen) {
  switch (screen) {
  case SCREEN_KEYBOARD:
    keyboard.toggleCaps();
    return true;
  case SCREEN_T9_KEYBOARD:
    t9Keyboard.toggleCaps();
    return true;
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    qwertyZoomKeyboard.toggleCaps();
    return true;
  default:
    return false;
  }
}

Screen TextInputController::nextMode(Screen screen) const {
  if (screen == SCREEN_T9_KEYBOARD) {
    return SCREEN_QWERTY_ZOOM_KEYBOARD;
  }
  return SCREEN_T9_KEYBOARD;
}

void TextInputController::draw(AppDisplay &display, Screen screen) {
  switch (screen) {
  case SCREEN_KEYBOARD:
    keyboard.draw(display, text);
    break;
  case SCREEN_T9_KEYBOARD:
    t9Keyboard.draw(display, text);
    break;
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    qwertyZoomKeyboard.draw(display, text);
    break;
  default:
    break;
  }
}

bool TextInputController::handleTouch(Screen screen, const TouchPoint &point) {
  switch (screen) {
  case SCREEN_KEYBOARD:
    return applyEvent(keyboard.hitTest(point));
  case SCREEN_T9_KEYBOARD:
    return t9Keyboard.hitTest(point, text).action != KEY_NONE;
  case SCREEN_QWERTY_ZOOM_KEYBOARD:
    return applyEvent(qwertyZoomKeyboard.hitTest(point));
  default:
    return false;
  }
}

bool TextInputController::applyEvent(const KeyboardEvent &event) {
  switch (event.action) {
  case KEY_CHAR:
    if (text.length() < MAX_TEXT_LENGTH) {
      text += event.value;
    }
    break;
  case KEY_SPACE:
    if (text.length() < MAX_TEXT_LENGTH) {
      text += ' ';
    }
    break;
  case KEY_BACKSPACE:
    if (text.length() > 0) {
      text.remove(text.length() - 1);
    }
    break;
  case KEY_SHIFT:
  case KEY_NAV:
  case KEY_OK:
    break;
  case KEY_NONE:
    return false;
  }
  return true;
}
