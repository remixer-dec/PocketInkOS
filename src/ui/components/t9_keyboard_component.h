#ifndef T9_KEYBOARD_COMPONENT_H
#define T9_KEYBOARD_COMPONENT_H

#include "ui/components/keyboard_component.h"
#include <Adafruit_GFX.h>

/// Draws and hit-tests a phone-style T9 keyboard.
class T9KeyboardComponent {
public:
  /// Available T9 keyboard layouts.
  enum class Layout : uint8_t { Text, Numbers, Numeric };

  /// Draws the T9 keyboard for String-backed input.
  void draw(Adafruit_GFX &gfx, const String &text);
  /// Draws the T9 keyboard with a maximum String length.
  void draw(Adafruit_GFX &gfx, const String &text, int maxLength);
  /// Draws the T9 keyboard for char-buffer input.
  void draw(Adafruit_GFX &gfx, const char *text, int maxLength);
  /// Handles touch input for String-backed text.
  KeyboardEvent hitTest(const TouchPoint &point, String &text);
  /// Handles touch input for String-backed text with length limiting.
  KeyboardEvent hitTest(const TouchPoint &point, String &text, int maxLength);
  /// Handles touch input for char-buffer text.
  KeyboardEvent hitTest(const TouchPoint &point, char *text, int capacity);
  /// Switches between text, digits-only, and signed-decimal numeric layouts.
  void setLayout(Layout nextLayout);
  /// Expires pending multi-tap state and returns true when redraw is needed.
  bool update();
  /// Toggles letter casing.
  void toggleCaps();

private:
  struct TextBuffer {
    String *stringText;
    char *charText;
    int length;
    int limit;

    void backspace();
    bool append(char value);
  };

  int pendingKey = -1;
  int pendingIndex = 0;
  unsigned long pendingAt = 0;
  bool caps = false;
  Layout layout = Layout::Text;

  void drawInput(Adafruit_GFX &gfx, const char *text, int maxLength);
  void drawKeyboard(Adafruit_GFX &gfx, int currentLength, int maxLength);
  void drawKey(Adafruit_GFX &gfx, int key, int x, int y, int w, int h,
               bool disabled);
  bool keyAt(const TouchPoint &point, int &key) const;
  KeyboardEvent handleKey(int key, TextBuffer &text);
  bool isPendingActive() const;
  void clearPending();
  bool expirePending();
  char currentChar() const;
  const char *sequenceForCurrentLayout(int key) const;

  static TextBuffer stringBuffer(String &text, int maxLength);
  static TextBuffer charBuffer(char *text, int capacity);
};

#endif
