#ifndef QWERTY_ZOOM_KEYBOARD_COMPONENT_H
#define QWERTY_ZOOM_KEYBOARD_COMPONENT_H

#include "keyboard_component.h"
#include <Adafruit_GFX.h>

class QwertyZoomKeyboardComponent {
public:
  void draw(Adafruit_GFX &gfx, const String &text);
  void draw(Adafruit_GFX &gfx, const String &text, int maxLength);
  KeyboardEvent hitTest(const TouchPoint &point);
  KeyboardEvent hitTest(const TouchPoint &point, int currentLength,
                        int maxLength);

private:
  bool caps = false;
  int pageColumn = 0;
  bool inputDisabled = false;

  void drawInput(Adafruit_GFX &gfx, const String &text, int maxLength);
  void movePage(int delta);
  char keyAt(int row, int col) const;
};

#endif
