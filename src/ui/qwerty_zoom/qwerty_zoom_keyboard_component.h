#ifndef QWERTY_ZOOM_KEYBOARD_COMPONENT_H
#define QWERTY_ZOOM_KEYBOARD_COMPONENT_H

#include "ui/components/keyboard_component.h"
#include <Adafruit_GFX.h>

/// Draws and hit-tests a paged zoomed QWERTY keyboard.
class QwertyZoomKeyboardComponent : public KeyboardComponent {
public:
  /// Draws keyboard input and the active zoomed key page.
  void draw(Adafruit_GFX &gfx, const String &text) override;
  /// Draws keyboard input and active key page with a maximum input length.
  void draw(Adafruit_GFX &gfx, const String &text, int maxLength) override;
  /// Maps a touch point to a keyboard event.
  KeyboardEvent hitTest(const TouchPoint &point) override;
  /// Maps a touch point to a keyboard event with length limiting.
  KeyboardEvent hitTest(const TouchPoint &point, int currentLength,
                        int maxLength) override;
  /// Toggles letter casing.
  void toggleCaps() override;

private:
  bool caps = false;
  int pageColumn = 0;
  bool inputDisabled = false;

  void drawInput(Adafruit_GFX &gfx, const String &text, int maxLength);
  void movePage(int delta);
  char keyAt(int row, int col) const;
};

#endif
