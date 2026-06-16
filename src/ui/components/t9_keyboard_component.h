#ifndef T9_KEYBOARD_COMPONENT_H
#define T9_KEYBOARD_COMPONENT_H

#include "ui/components/keyboard_component.h"
#include <Adafruit_GFX.h>

class T9KeyboardComponent {
public:
  enum class Layout : uint8_t { Text, Numbers };

  void draw(Adafruit_GFX &gfx, const String &text);
  void draw(Adafruit_GFX &gfx, const String &text, int maxLength);
  KeyboardEvent hitTest(const TouchPoint &point, String &text);
  KeyboardEvent hitTest(const TouchPoint &point, String &text, int maxLength);
  void setLayout(Layout nextLayout);
  bool update();
  void toggleCaps();

private:
  int pendingKey = -1;
  int pendingIndex = 0;
  unsigned long pendingAt = 0;
  bool caps = false;
  Layout layout = Layout::Text;

  void drawInput(Adafruit_GFX &gfx, const String &text, int maxLength);
  void drawKey(Adafruit_GFX &gfx, int key, int x, int y, int w, int h,
               bool disabled);
  bool isPendingActive() const;
  void clearPending();
  bool expirePending();
  char currentChar() const;
};

#endif
